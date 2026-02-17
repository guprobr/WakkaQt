#include "vocalenhancer.h"

#include <QDebug>
#include <cmath>
#include <complex>
#include <algorithm>
#include <cstring>
#include <fftw3.h>

static constexpr double kPi = 3.1415926535897932384626433832795;

// Wrap phase to (-pi..pi)
static inline double principalArg(double phase) {
    phase = std::fmod(phase + kPi, 2.0 * kPi);
    if (phase < 0) phase += 2.0 * kPi;
    return phase - kPi;
}

// ----------------------
// Constructor (QT6)
// ----------------------
VocalEnhancer::VocalEnhancer(const QAudioFormat& format, QObject *parent)
    : QObject(parent),
      banner("Begin Vocal Enhancement")
{
    m_sampleRate = format.sampleRate();
    m_channels   = format.channelCount();

    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        m_isFloat = false;
        m_isSignedInt = false;
        m_bytesPerSample = 1;
        break;

    case QAudioFormat::Int16:
        m_isFloat = false;
        m_isSignedInt = true;
        m_bytesPerSample = 2;
        break;

    case QAudioFormat::Int32:
        m_isFloat = false;
        m_isSignedInt = true;
        m_bytesPerSample = 4;
        break;

    case QAudioFormat::Float:
        m_isFloat = true;
        m_isSignedInt = false;
        m_bytesPerSample = 4;
        break;

    default:
        // Some backends may report 24-bit as sampleFormat=Unknown but bytesPerSample=3
        m_isFloat = false;
        m_isSignedInt = true;
        m_bytesPerSample = std::max(1, format.bytesPerFrame() / format.channelCount());
        break;
    }

    // Fix if backend gives 24-bit PCM
    if (!m_isFloat && format.bytesPerSample() == 3)
        m_bytesPerSample = 3;

    m_frameBytes = m_channels * m_bytesPerSample;

    // Fixed 40ms analysis window
    m_numSamples = qMax(1, (m_sampleRate * m_blockSizeMs) / 1000);
}

VocalEnhancer::~VocalEnhancer() {
    fftw_cleanup();
}

// ----------------------
// 24-bit LE helpers
// ----------------------
int32_t VocalEnhancer::readInt24LE(uint8_t b0, uint8_t b1, uint8_t b2) {
    int32_t v = (int32_t(b2) << 16) | (int32_t(b1) << 8) | int32_t(b0);
    return (v << 8) >> 8; // sign-extend
}

void VocalEnhancer::writeInt24LE(uint8_t* dst, int32_t v) {
    if (v >  0x7FFFFF) v =  0x7FFFFF;
    if (v < -0x800000) v = -0x800000;

    dst[0] = uint8_t(v & 0xFF);
    dst[1] = uint8_t((v >> 8) & 0xFF);
    dst[2] = uint8_t((v >> 16) & 0xFF);
}

// ----------------------
// PCM → double (MONO)
// ----------------------
QVector<double> VocalEnhancer::convertToDoubleArray(const QByteArray& input) {
    if (input.isEmpty() || m_frameBytes <= 0) return {};

    const int totalFrames = input.size() / m_frameBytes;
    QVector<double> mono(totalFrames, 0.0);

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(input.constData());

    for (int f = 0; f < totalFrames; ++f) {
        const uint8_t* frame = ptr + f * m_frameBytes;
        double sum = 0.0;

        for (int ch = 0; ch < m_channels; ++ch) {
            const uint8_t* s = frame + ch * m_bytesPerSample;
            double x = 0.0;

            if (m_isFloat && m_bytesPerSample == 4) {
                float v;
                memcpy(&v, s, 4);
                x = double(v);
            }
            else if (m_bytesPerSample == 1 && !m_isSignedInt) {
                uint8_t v = s[0];
                x = (double(v) / 255.0) * 2.0 - 1.0;
            }
            else if (m_bytesPerSample == 2 && m_isSignedInt) {
                int16_t v = int16_t(s[0] | (s[1] << 8));
                x = double(v) / 32768.0;
            }
            else if (m_bytesPerSample == 3) {
                int32_t v = readInt24LE(s[0], s[1], s[2]);
                x = double(v) / double(1 << 23);
            }
            else if (m_bytesPerSample == 4 && m_isSignedInt) {
                int32_t v = int32_t(s[0] | (s[1]<<8) | (s[2]<<16) | (s[3]<<24));
                x = double(v) / 2147483648.0;
            }

            sum += x;
        }

        mono[f] = sum / m_channels;
    }

    return mono;
}

// ----------------------
// double → PCM (replica mono → canais)
// ----------------------
void VocalEnhancer::convertToQByteArray(const QVector<double>& inputData,
                                        QByteArray& output)
{
    const int totalFrames = inputData.size();
    if (output.size() < totalFrames * m_frameBytes)
        output.resize(totalFrames * m_frameBytes);

    uint8_t* out = reinterpret_cast<uint8_t*>(output.data());

    for (int f = 0; f < totalFrames; ++f) {
        double x = std::clamp(inputData[f], -1.0, 1.0);

        for (int ch = 0; ch < m_channels; ++ch) {
            uint8_t* s = out + f * m_frameBytes + ch * m_bytesPerSample;

            if (m_isFloat) {
                float v = float(x);
                memcpy(s, &v, 4);
            }
            else if (m_bytesPerSample == 1) {
                uint8_t v = uint8_t((x * 0.5 + 0.5) * 255.0);
                s[0] = v;
            }
            else if (m_bytesPerSample == 2) {
                int32_t v = int32_t(std::lround(x * 32767.0));
                s[0] = uint8_t(v & 0xFF);
                s[1] = uint8_t((v >> 8) & 0xFF);
            }
            else if (m_bytesPerSample == 3) {
                int32_t v = int32_t(std::llround(x * double(1 << 23)));
                writeInt24LE(s, v);
            }
            else if (m_bytesPerSample == 4) {
                int64_t v = int64_t(std::llround(x * 2147483647.0));
                uint32_t uv = uint32_t(int32_t(std::clamp<int64_t>(v,
                                  -2147483648LL, 2147483647LL)));

                s[0] = uint8_t(uv & 0xFF);
                s[1] = uint8_t((uv >> 8) & 0xFF);
                s[2] = uint8_t((uv >> 16) & 0xFF);
                s[3] = uint8_t((uv >> 24) & 0xFF);
            }
        }
    }
}

// ======================
// Public API (rest)
// ======================
QByteArray VocalEnhancer::enhance(const QByteArray& input) {
    qWarning() << "VocalEnhancer Input Data Size:" << input.size();
    if (input.isEmpty() || m_frameBytes <= 0) return QByteArray();

    // Convert to mono double [-1..+1]
    QVector<double> data = convertToDoubleArray(input);

    // Pitch correction + processing chain
    processPitchCorrection(data);

    // Convert back to PCM, replicating mono to all channels
    QByteArray output(data.size() * m_frameBytes, 0);
    convertToQByteArray(data, output);
    return output;
}

int VocalEnhancer::getProgress() const {
    return int(progressValue * 100.0);
}

QString VocalEnhancer::getBanner() const {
    return banner;
}

// ======================
// Pitch Detection (HPS on log spectrum)
// ======================
double VocalEnhancer::detectPitch(const QVector<double>& inputData) const {
    const int N = inputData.size();
    if (N < 2048) return 0.0;

    const double minFreq = 80.0;
    const double maxFreq = 1000.0;

    const int fftN = 4096;
    if (N < fftN) return 0.0;

    // Choose a central block with the highest RMS among a few candidates
    int start = (N - fftN) / 2;

    auto blockRMS = [&](int s) {
        long double sum = 0.0;
        for (int i = 0; i < fftN; ++i) {
            double v = inputData[s + i];
            sum += (long double)v * (long double)v;
        }
        return std::sqrt((double)(sum / fftN));
    };

    int bestStart = start;
    double bestRms = blockRMS(start);

    const int step = fftN / 2;
    for (int off = -3; off <= 3; ++off) {
        int s = start + off * step;
        if (s < 0 || s + fftN > N) continue;
        double r = blockRMS(s);
        if (r > bestRms) {
            bestRms = r;
            bestStart = s;
        }
    }

    if (bestRms < 1e-4) {
        // Too quiet to be reliable
        return 0.0;
    }
    start = bestStart;

    QVector<double> window = createHannWindow(fftN);

    // Remove DC and apply window
    double mean = 0.0;
    for (int i = 0; i < fftN; ++i) mean += inputData[start + i];
    mean /= fftN;

    fftw_complex* fftOut = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (fftN / 2 + 1));
    double* fftIn = (double*)fftw_malloc(sizeof(double) * fftN);

    for (int i = 0; i < fftN; ++i) {
        double x = inputData[start + i] - mean;
        fftIn[i] = x * window[i];
    }

    fftw_plan plan = fftw_plan_dft_r2c_1d(fftN, fftIn, fftOut, FFTW_ESTIMATE);
    fftw_execute(plan);

    const int bins = fftN / 2 + 1;
    QVector<double> logMag(bins);

    // Log magnitude for stability
    for (int k = 0; k < bins; ++k) {
        double re = fftOut[k][0];
        double im = fftOut[k][1];
        double mag = std::sqrt(re*re + im*im);
        logMag[k] = std::log(mag + 1e-12);
    }

    // Harmonic Product Spectrum in log-domain (sum of shifted logs)
    QVector<double> hps = logMag;
    const int maxHarmonics = 5;
    for (int h = 2; h <= maxHarmonics; ++h) {
        for (int k = 0; k < bins / h; ++k) {
            hps[k] += logMag[k * h];
        }
    }

    int minBin = int(std::floor(minFreq * fftN / m_sampleRate));
    int maxBin = int(std::ceil (maxFreq * fftN / m_sampleRate));
    minBin = std::clamp(minBin, 1, bins - 1);
    maxBin = std::clamp(maxBin, 1, bins - 1);

    double bestVal = -1e300;
    int bestBin = minBin;

    for (int k = minBin; k <= maxBin; ++k) {
        if (hps[k] > bestVal) {
            bestVal = hps[k];
            bestBin = k;
        }
    }

    // Parabolic refinement for bin accuracy
    double refinedBin = bestBin;
    if (bestBin > 1 && bestBin + 1 < bins) {
        double y0 = hps[bestBin - 1];
        double y1 = hps[bestBin];
        double y2 = hps[bestBin + 1];
        double denom = (y0 - 2.0*y1 + y2);
        if (std::abs(denom) > 1e-12) {
            double delta = 0.5 * (y0 - y2) / denom;
            refinedBin = bestBin + delta;
        }
    }

    double detectedFreq = (refinedBin * m_sampleRate) / fftN;

    fftw_destroy_plan(plan);
    fftw_free(fftOut);
    fftw_free(fftIn);

    qWarning() << "HPS(log) detected pitch:" << detectedFreq << "Hz";
    banner = QString("pitch detected: %1 Hz").arg(detectedFreq, 0, 'f', 2);
    return detectedFreq;
}

// Equal-tempered note (12-TET) using formula (no static table)
static inline double midiToFreq(int midi) {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

double VocalEnhancer::findClosestNoteFrequency(double frequency) const {
    if (frequency <= 0.0) return 440.0;
    const double midiFloat = 69.0 + 12.0 * std::log2(frequency / 440.0);
    const int midi = std::clamp<int>(int(std::lround(midiFloat)), 0, 127);
    return midiToFreq(midi);
}

// ======================
// Processing pipeline
// ======================
void VocalEnhancer::processPitchCorrection(QVector<double>& data) {
    // Helper RMS
    auto computeRMS = [&](const QVector<double>& x) {
        if (x.isEmpty()) return 0.0;
        long double sum = 0.0;
        for (double s : x) sum += (long double)s * (long double)s;
        return std::sqrt((double)(sum / x.size()));
    };

    const double rmsIn = computeRMS(data);
    if (rmsIn < 1e-6) {
        banner = "Enhancer: silence (bypass)";
        return;
    }

    // Detect pitch
    double detectedPitch = detectPitch(data);
    if (detectedPitch <= 0.0) {
        banner = "Pitch: unknown (bypass PV)";
        detectedPitch = 440.0; // fallback for banner consistency
    }

    // Snap to nearest equal-tempered note (configurable)
    const double targetFrequency   = findClosestNoteFrequency(detectedPitch);
    double ratioRaw = targetFrequency / detectedPitch;

    // Tuning parameters (expose as needed)
    const double deadZoneCents      = 12.0;   // no correction within ±12 cents
    const double maxCorrectionCents = 150.0;  // limit correction to ±150 cents (~±1.5 semitones)
    const double strength           = 0.65;   // 0..1 (lower = more natural)
    const double bypassEps          = 1e-3;   // if final ratio ~1, skip vocoder

    // Convert ratio to cents and apply dead-zone & cap
    double cents = 1200.0 * std::log2(ratioRaw);
    if (std::abs(cents) < deadZoneCents) {
        ratioRaw = 1.0;
        cents = 0.0;
    } else {
        cents = std::clamp(cents, -maxCorrectionCents, maxCorrectionCents);
        ratioRaw = std::pow(2.0, cents / 1200.0);
    }

    // Apply "strength"
    double ratio = std::pow(ratioRaw, strength);

    // Gentle clamp within maxCorrectionCents bounds
    const double maxRatio = std::pow(2.0, maxCorrectionCents / 1200.0);
    ratio = std::clamp(ratio, 1.0 / maxRatio, maxRatio);

    // One-shot smoothing (block-level)
    const double alpha = 0.25;
    ratio = 1.0 + alpha * (ratio - 1.0);

    banner = QString("Pitch PV: det=%1Hz tgt=%2Hz cents=%3 r=%4")
                 .arg(detectedPitch, 0, 'f', 2)
                 .arg(targetFrequency, 0, 'f', 2)
                 .arg(cents, 0, 'f', 1)
                 .arg(ratio, 0, 'f', 4);

    // Pitch shift if needed
    if (std::abs(ratio - 1.0) > bypassEps) {
        data = pitchShiftPhaseVocoder(data, ratio);
    }

    // Loudness compensation
    const double rmsAfter = computeRMS(data);
    if (rmsAfter > 1e-9) {
        const double compensation = 1.35; // tune this to taste
        double gain = (rmsIn / rmsAfter) * compensation;
        gain = std::clamp(gain, 0.75, 3.0);
        applyMakeupGain(data, gain);
    }

    // Dynamics and timbre
    compressDynamics(data, 0.7, 3.0);                 // improved compressor below
    harmonicExciter(data, 1.2, 0.10);                 // gentle exciter
    applyEcho(data, 0.5, 0.18, 85, 125, 0.10, 0.08);  // subtle dual echo in mono

    // Safety
    applyLimiter(data, 0.98);
}

// ======================
// Dynamics / FX
// ======================
void VocalEnhancer::applyLimiter(QVector<double>& x, double ceiling) {
    // Soft limiter using tanh-based knee to avoid harsh clipping
    ceiling = std::clamp(ceiling, 0.1, 0.999);
    const double knee = 0.9 * ceiling;
    const double scale = 1.0 / std::tanh(1.0); // normalize tanh

    for (auto& s : x) {
        double a = std::abs(s);
        if (a <= knee) {
            s = std::clamp(s, -ceiling, ceiling);
        } else {
            double sign = (s >= 0.0) ? 1.0 : -1.0;
            double over = (a - knee) / (1.0 - knee); // 0..1
            double shaped = knee + (ceiling - knee) * std::tanh(over) * scale;
            s = sign * std::min(shaped, ceiling);
        }
    }
}

void VocalEnhancer::applyMakeupGain(QVector<double>& x, double gain) {
    if (gain <= 0.0) return;
    for (auto& s : x) s *= gain;
}

void VocalEnhancer::compressDynamics(QVector<double>& x, double threshold, double ratio) {
    // Feed-forward compressor with peak-like envelope follower
    threshold = std::clamp(threshold, 0.0, 0.999);
    ratio = std::max(1.0, ratio);

    const double atkMs = 5.0;
    const double relMs = 60.0;
    const double atk = std::exp(-1.0 / ((atkMs / 1000.0) * m_sampleRate));
    const double rel = std::exp(-1.0 / ((relMs / 1000.0) * m_sampleRate));

    double env = 0.0;
    for (auto& s : x) {
        double a = std::abs(s);
        env = (a > env) ? (atk * env + (1.0 - atk) * a)
                        : (rel * env + (1.0 - rel) * a);

        if (env > threshold) {
            double over = env / threshold;
            double gr = std::pow(over, 1.0 - 1.0 / ratio);
            s /= gr;
        }
    }
}

void VocalEnhancer::harmonicExciter(QVector<double>& x, double drive, double mix) {
    drive = std::max(0.0, drive);
    mix   = std::clamp(mix, 0.0, 1.0);

    for (auto& s : x) {
        double clean  = s;
        double driven = std::tanh(clean * drive); // soft harmonic saturation
        s = clean * (1.0 - mix) + driven * mix;
    }
}

void VocalEnhancer::applyEcho(QVector<double>& inputData,
                              double gainIn, double gainOut,
                              double delayMs1, double delayMs2,
                              double feedback1, double feedback2) {
    // Two independent delay lines with feedback on the delayed outputs
    gainIn    = std::clamp(gainIn,    0.0, 1.0);
    gainOut   = std::clamp(gainOut,   0.0, 1.0);
    feedback1 = std::clamp(feedback1, 0.0, 0.95);
    feedback2 = std::clamp(feedback2, 0.0, 0.95);

    const int d1 = std::max(1, int((delayMs1 / 1000.0) * m_sampleRate));
    const int d2 = std::max(1, int((delayMs2 / 1000.0) * m_sampleRate));

    QVector<double> delay1(d1, 0.0);
    QVector<double> delay2(d2, 0.0);
    int idx1 = 0, idx2 = 0;

    for (int i = 0; i < inputData.size(); ++i) {
        double dry = inputData[i];
        double y1  = delay1[idx1];
        double y2  = delay2[idx2];

        // Feedback writes use previous delayed outputs
        double in1 = dry * gainIn + feedback1 * y1;
        double in2 = dry * gainIn + feedback2 * y2;

        delay1[idx1] = in1;
        delay2[idx2] = in2;

        // Mix wet into dry
        double wet = (y1 + y2) * 0.5;
        inputData[i] = std::clamp(dry + wet * gainOut, -1.0, 1.0);

        // Advance pointers
        idx1 = (idx1 + 1) % d1;
        idx2 = (idx2 + 1) % d2;
    }
}

// ======================
// Window / math helpers
// ======================
QVector<double> VocalEnhancer::createHannWindow(int size) const {
    QVector<double> w(size);
    for (int i = 0; i < size; ++i) {
        w[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (size - 1)));
    }
    return w;
}

double VocalEnhancer::cubicInterpolate(double v0, double v1, double v2, double v3, double t) const {
    // Standard cubic interpolation (Catmull-Rom-like without 0.5 factor)
    double a0 = v3 - v2 - v0 + v1;
    double a1 = v0 - v1 - a0;
    double a2 = v2 - v0;
    return (a0 * t * t * t + a1 * t * t + a2 * t + v1);
}

// ======================
// Phase Vocoder
// ======================
QVector<double> VocalEnhancer::timeStretchPhaseVocoder(const QVector<double>& in, double stretch) {
    if (in.isEmpty() || stretch <= 0.0) return in;

    const int N  = 2048;           // FFT size
    const int Ha = N / 4;          // analysis hop
    const int Hs = std::max(1, int(std::round(Ha * stretch))); // synthesis hop

    QVector<double> window = createHannWindow(N);

    const int outCap = int(in.size() * stretch) + N + 4;
    QVector<double> out(outCap, 0.0);
    QVector<double> winSum(outCap, 0.0);

    double* fftIn = (double*)fftw_malloc(sizeof(double) * N);
    fftw_complex* fftOut = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (N/2 + 1));
    fftw_complex* spec = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (N/2 + 1));
    double* ifftOut = (double*)fftw_malloc(sizeof(double) * N);

    fftw_plan fwd = fftw_plan_dft_r2c_1d(N, fftIn, fftOut, FFTW_ESTIMATE);
    fftw_plan inv = fftw_plan_dft_c2r_1d(N, spec, ifftOut, FFTW_ESTIMATE);

    const int bins = N/2 + 1;
    QVector<double> prevPhase(bins, 0.0);
    QVector<double> sumPhase(bins, 0.0);
    QVector<double> omega(bins);
    for (int k = 0; k < bins; ++k)
        omega[k] = 2.0 * kPi * k / N;

    int inPos = 0;
    int outPos = 0;

    while (inPos + N <= in.size() && outPos + N < out.size()) {
        for (int i = 0; i < N; ++i)
            fftIn[i] = in[inPos + i] * window[i];

        fftw_execute(fwd);

        for (int k = 0; k < bins; ++k) {
            double re = fftOut[k][0];
            double im = fftOut[k][1];

            double mag = std::sqrt(re*re + im*im);
            double phase = std::atan2(im, re);

            double delta = phase - prevPhase[k] - omega[k] * Ha;
            delta = principalArg(delta);

            double trueFreq = omega[k] + delta / Ha;

            sumPhase[k] += trueFreq * Hs;
            prevPhase[k] = phase;

            spec[k][0] = mag * std::cos(sumPhase[k]);
            spec[k][1] = mag * std::sin(sumPhase[k]);
        }

        fftw_execute(inv);

        for (int i = 0; i < N; ++i) {
            double s = (ifftOut[i] / N) * window[i];
            int idx = outPos + i;
            out[idx] += s;
            winSum[idx] += window[i] * window[i];
        }

        inPos += Ha;
        outPos += Hs;
    }

    for (int i = 0; i < out.size(); ++i) {
        if (winSum[i] > 1e-12) out[i] /= winSum[i];
    }

    out.resize(std::min<int>(out.size(), outPos + N));
    fftw_destroy_plan(fwd);
    fftw_destroy_plan(inv);
    fftw_free(fftIn);
    fftw_free(fftOut);
    fftw_free(spec);
    fftw_free(ifftOut);

    return out;
}

QVector<double> VocalEnhancer::pitchShiftPhaseVocoder(const QVector<double>& in, double ratio) {
    if (in.isEmpty() || ratio <= 0.0) return in;

    // 1) Time-stretch by 1/ratio (slower if pitch up)
    QVector<double> stretched = timeStretchPhaseVocoder(in, 1.0 / ratio);

    // 2) Resample back to original length using cubic interpolation
    const int outN = in.size();
    QVector<double> out(outN, 0.0);

    if (stretched.size() < 2) return in;

    const double step = double(stretched.size() - 1) / double(outN - 1);
    for (int i = 0; i < outN; ++i) {
        double t = i * step;
        int idx = int(t);
        double frac = t - idx;

        int i0 = std::max(0, idx - 1);
        int i1 = std::max(0, idx);
        int i2 = std::min(int(stretched.size()) - 1, idx + 1);
        int i3 = std::min(int(stretched.size()) - 1, idx + 2);

        double v0 = stretched[i0];
        double v1 = stretched[i1];
        double v2 = stretched[i2];
        double v3 = stretched[i3];

        double a0 = v3 - v2 - v0 + v1;
        double a1 = v0 - v1 - a0;
        double a2 = v2 - v0;
        double y  = (a0 * frac * frac * frac + a1 * frac * frac + a2 * frac + v1);

        out[i] = std::clamp(y, -1.0, 1.0);
    }

    return out;
}