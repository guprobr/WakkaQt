#include "playbackbleedfilter.h"
#include "fftwplannerlock.h"

#include <QLibrary>
#include <QtEndian>
#include <fftw3.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
// SpeexDSP's stable C API, loaded at runtime so existing installations can
// still tune recordings without the optional echo-cancellation library.
// API: https://github.com/xiph/speexdsp/blob/master/include/speex/speex_echo.h
struct SpeexEchoState_;
struct SpeexApi {
    QLibrary library;
    using Init = SpeexEchoState_* (*)(int, int, int, int);
    using Destroy = void (*)(SpeexEchoState_*);
    using Cancel = void (*)(SpeexEchoState_*, const qint16*, const qint16*, qint16*);
    using Ctl = int (*)(SpeexEchoState_*, int, void*);
    Init init = nullptr;
    Destroy destroy = nullptr;
    Cancel cancel = nullptr;
    Ctl ctl = nullptr;

    SpeexApi() {
        for (const QString &name : {QStringLiteral("speexdsp"),
                                   QStringLiteral("libspeexdsp.so.1"),
                                   QStringLiteral("libspeexdsp-1"),
                                   QStringLiteral("libspeexdsp")}) {
            library.setFileName(name);
            if (library.load()) break;
        }
        init = reinterpret_cast<Init>(library.resolve("speex_echo_state_init_mc"));
        destroy = reinterpret_cast<Destroy>(library.resolve("speex_echo_state_destroy"));
        cancel = reinterpret_cast<Cancel>(library.resolve("speex_echo_cancellation"));
        ctl = reinterpret_cast<Ctl>(library.resolve("speex_echo_ctl"));
    }
    bool valid() const { return init && destroy && cancel && ctl; }
};

qint16 sample(const QByteArray &pcm, qint64 frame, int channel, int channels) {
    if (frame < 0 || frame >= pcm.size() / (2 * channels)) return 0;
    return qFromLittleEndian<qint16>(pcm.constData() + (frame * channels + channel) * 2);
}

bool stopped(const std::atomic<bool> *flag) { return flag && flag->load(); }

// Speex removes DC with a speech-oriented notch. Replace its frequency
// response with a gentler 10 Hz notch for singing, using a stable biquad
// ratio of the two notch denominators (no unstable integration at DC).
// Coefficients follow SpeexDSP's documented implementation in mdf.c.
struct VocalEqualizer {
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double process(double x, int rate) {
        const double speechRadius = rate < 12000 ? 0.9 : rate < 24000 ? 0.982 : 0.992;
        const double vocalRadius = std::exp(-2 * 3.141592653589793 * 10 / rate);
        const auto denominator = [](double r) { return r * r + 0.7 * (1 - r) * (1 - r); };
        const double gain = vocalRadius / speechRadius;
        const double y = gain * (x - 2 * speechRadius * x1 + denominator(speechRadius) * x2)
            + 2 * vocalRadius * y1 - denominator(vocalRadius) * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return std::clamp(y, -32768.0, 32767.0);
    }
};

// Normalized cross-correlation over several excerpts, independently for each
// speaker channel (a mono sum could erase out-of-phase stereo playback).
// FFT buffers/plans are local; FFTW_ESTIMATE avoids measuring over user audio.
struct Alignment { int delay = 0; double confidence = 0; };
Alignment align(const QByteArray &mic, const QByteArray &ref, int rate, int channels,
                qint64 refStart, const std::atomic<bool> *cancelled) {
    const qint64 frames = mic.size() / (2 * channels);
    const int radius = rate / 2; // +/- 500 ms: device latency and capture gate
    const int length = int(std::min<qint64>(rate * 2, frames));
    Alignment best;
    if (length < rate / 5) return best;
    const int refLength = length + 2 * radius;
    int n = 1;
    while (n < length + refLength) n *= 2;
    std::vector<double> x(n), y(n), corr(n), energy(refLength + 1);
    auto freeFft = [](fftw_complex *p) { fftw_free(p); };
    std::unique_ptr<fftw_complex, decltype(freeFft)> a(fftw_alloc_complex(n / 2 + 1), freeFft);
    std::unique_ptr<fftw_complex, decltype(freeFft)> b(fftw_alloc_complex(n / 2 + 1), freeFft);
    if (!a || !b) return best;
    // FFTW planner is not thread safe. Qt's DSP jobs can overlap, so guard
    // all planner calls with the same lock used by VocalEnhancer (below).
    fftw_plan px, py, pi;
    {
        std::lock_guard<std::mutex> lock(wakkaFftwPlannerMutex());
        px = fftw_plan_dft_r2c_1d(n, x.data(), a.get(), FFTW_ESTIMATE);
        py = fftw_plan_dft_r2c_1d(n, y.data(), b.get(), FFTW_ESTIMATE);
        pi = fftw_plan_dft_c2r_1d(n, a.get(), corr.data(), FFTW_ESTIMATE);
    }
    if (px && py && pi) {
        for (int excerpt = 0; excerpt < 3 && !stopped(cancelled); ++excerpt) {
            const qint64 start = (frames - length) * excerpt / 2;
            for (int pair = 0; pair < channels * channels && !stopped(cancelled); ++pair) {
                const int micChannel = pair / channels, c = pair % channels;
                std::fill(x.begin(), x.end(), 0);
                std::fill(y.begin(), y.end(), 0);
                double micEnergy = 0;
                for (int i = 0; i < length; ++i) {
                    x[i] = sample(mic, start + i, micChannel, channels) / 32768.0;
                    micEnergy += x[i] * x[i];
                }
                energy[0] = 0;
                for (int i = 0; i < refLength; ++i) {
                    y[i] = sample(ref, refStart + start - radius + i, c, channels) / 32768.0;
                    energy[i + 1] = energy[i] + y[i] * y[i];
                }
                if (micEnergy < 1e-8) continue;
                fftw_execute(px);
                fftw_execute(py);
                for (int k = 0; k <= n / 2; ++k) {
                    const double re = a.get()[k][0] * b.get()[k][0] + a.get()[k][1] * b.get()[k][1];
                    const double im = a.get()[k][1] * b.get()[k][0] - a.get()[k][0] * b.get()[k][1];
                    a.get()[k][0] = re;
                    a.get()[k][1] = im;
                }
                fftw_execute(pi);
                for (int delay = -radius; delay <= radius; ++delay) {
                    const int shift = radius - delay;
                    const double refEnergy = energy[shift + length] - energy[shift];
                    if (refEnergy < 1e-8) continue;
                    const int idx = (n - shift) % n;
                    const double confidence = std::abs(corr[idx] / n) / std::sqrt(micEnergy * refEnergy);
                    if (confidence > best.confidence) best = {delay, confidence};
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(wakkaFftwPlannerMutex());
        if (px) fftw_destroy_plan(px);
        if (py) fftw_destroy_plan(py);
        if (pi) fftw_destroy_plan(pi);
    }
    return best;
}
} // namespace

bool PlaybackBleedFilter::isAvailable() { return SpeexApi().valid(); }

PlaybackBleedFilter::Result PlaybackBleedFilter::process(
    const QByteArray &microphone, const QByteArray &playback, const QAudioFormat &format,
    qint64 referenceStartFrame, const std::atomic<bool> *cancelled,
    const std::function<void(int)> &progress) {
    Result result;
    if (stopped(cancelled)) return result;
    const int channels = format.channelCount();
    int rate = format.sampleRate();
    if (format.sampleFormat() != QAudioFormat::Int16) {
        result.error = "Playback bleed removal requires decoded 16-bit PCM audio.";
    } else if (channels < 1 || channels > 2) {
        result.error = QString("Playback bleed removal supports mono or stereo audio; received %1 channels.").arg(channels);
    } else if (rate < 8000 || rate > 96000) {
        result.error = QString("Playback bleed removal supports 8000–96000 Hz; received %1 Hz. "
                               "Convert the recording to 48000 Hz before cleanup.").arg(rate);
    } else if (microphone.isEmpty() || playback.isEmpty()) {
        result.error = "Playback bleed removal requires both microphone audio and a playback reference.";
    } else if (microphone.size() % (channels * 2) || playback.size() % (channels * 2)) {
        result.error = "Playback bleed removal received incomplete PCM sample frames.";
    } else if (referenceStartFrame < 0) {
        result.error = "Playback bleed removal requires a nonnegative reference offset.";
    }
    if (!result.error.isEmpty()) {
        return result;
    }
    SpeexApi api;
    if (!api.valid()) {
        result.error = "Playback bleed removal requires the SpeexDSP runtime library.";
        return result;
    }
    if (progress) progress(0);
    const Alignment timing = align(microphone, playback, rate, channels, referenceStartFrame, cancelled);
    if (stopped(cancelled)) return result;
    result.confidence = timing.confidence;
    result.delaySamples = timing.delay;
    if (timing.confidence < 0.08) {
        // No reliable playback in the microphone: keep the take byte-for-byte.
        result.samples = microphone;
        if (progress) progress(100);
        return result;
    }

    const int block = 512; // efficient FFT size, ~12 ms at 44.1 kHz
    const int tail = rate * 3 / 10;
    std::unique_ptr<SpeexEchoState_, SpeexApi::Destroy> state(
        api.init(block, tail, channels, channels), api.destroy);
    if (!state || api.ctl(state.get(), 24 /* SPEEX_ECHO_SET_SAMPLING_RATE */, &rate) != 0) {
        result.error = "Could not initialize playback bleed removal.";
        return result;
    }
    // Leave 20 ms before the strongest reflection for a causal room filter.
    const int referenceDelay = timing.delay - rate / 50;
    const qint64 frames = microphone.size() / (channels * 2);
    std::vector<qint16> rec(block * channels), play(block * channels), out(block * channels);
    std::vector<VocalEqualizer> equalizers(channels);
    auto fillBlock = [&](qint64 pos) {
        for (int i = 0; i < block; ++i)
            for (int c = 0; c < channels; ++c) {
                rec[i * channels + c] = sample(microphone, pos + i, c, channels);
                play[i * channels + c] = sample(playback, referenceStartFrame + pos + i - referenceDelay, c, channels);
            }
    };
    // Offline processing can learn from an opening excerpt before emitting
    // audio, reducing the adaptation period at the start of the saved take.
    const qint64 learnFrames = std::min<qint64>(frames, rate * 3);
    for (qint64 pos = 0; pos < learnFrames; pos += block) {
        if (stopped(cancelled)) return result;
        fillBlock(pos);
        api.cancel(state.get(), rec.data(), play.data(), out.data());
        if (progress) progress(5 + int(10 * pos / learnFrames));
    }
    // Clear signal history while retaining the learned acoustic filter.
    std::fill(rec.begin(), rec.end(), 0);
    std::fill(play.begin(), play.end(), 0);
    for (int pos = 0; pos < tail + block; pos += block) {
        if (stopped(cancelled)) return result;
        api.cancel(state.get(), rec.data(), play.data(), out.data());
    }
    result.samples.resize(microphone.size());
    for (qint64 pos = 0; pos < frames; pos += block) {
        if (stopped(cancelled)) { result.samples.clear(); return result; }
        fillBlock(pos);
        api.cancel(state.get(), rec.data(), play.data(), out.data());
        for (int i = 0; i < std::min<qint64>(block, frames - pos); ++i)
            for (int c = 0; c < channels; ++c)
                qToLittleEndian<qint16>(qint16(std::lround(equalizers[c].process(out[i * channels + c], rate))),
                    result.samples.data() + ((pos + i) * channels + c) * 2);
        if (progress) progress(15 + int(85 * (pos + std::min<qint64>(block, frames - pos)) / frames));
    }
    result.applied = true;
    return result;
}
