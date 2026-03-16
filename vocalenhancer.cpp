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

// Zero-Crossing Rate — crude voicing cue (higher on unvoiced consonants/noise)
static double zeroCrossingRate(const QVector<double>& x) {
    if (x.size() < 2) return 0.0;
    int crosses = 0;
    double prev = x[0];
    for (int i = 1; i < x.size(); ++i) {
        const double cur = x[i];
        if ((prev >= 0.0 && cur < 0.0) || (prev < 0.0 && cur >= 0.0)) crosses++;
        prev = cur;
    }
    return double(crosses) / double(x.size() - 1);
}

static inline double lerpParam(double a, double b, double t) {
    return a + (b - a) * std::clamp(t, 0.0, 1.0);
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

    // ── Pre-allocate cached FFTW plans ────────────────────────────────────
    // Phase vocoder (N=2048)
    m_pvIn   = (double*)      fftw_malloc(sizeof(double)       * kPvN);
    m_pvOut  = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (kPvN/2 + 1));
    m_pvSpec = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (kPvN/2 + 1));
    m_pvIfft = (double*)      fftw_malloc(sizeof(double)       * kPvN);
    if (m_pvIn && m_pvOut && m_pvSpec && m_pvIfft) {
        m_pvFwd = fftw_plan_dft_r2c_1d(kPvN, m_pvIn,   m_pvOut,  FFTW_MEASURE);
        m_pvInv = fftw_plan_dft_c2r_1d(kPvN, m_pvSpec, m_pvIfft, FFTW_MEASURE);
    }

    // Noise gate (N=1024)
    m_ngIn   = (double*)      fftw_malloc(sizeof(double)       * kNgN);
    m_ngOut  = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (kNgN/2 + 1));
    m_ngSpec = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (kNgN/2 + 1));
    m_ngIfft = (double*)      fftw_malloc(sizeof(double)       * kNgN);
    if (m_ngIn && m_ngOut && m_ngSpec && m_ngIfft) {
        m_ngFwd = fftw_plan_dft_r2c_1d(kNgN, m_ngIn,   m_ngOut,  FFTW_MEASURE);
        m_ngInv = fftw_plan_dft_c2r_1d(kNgN, m_ngSpec, m_ngIfft, FFTW_MEASURE);
    }
}

VocalEnhancer::~VocalEnhancer() {
    // Destroy cached PV plans
    if (m_pvFwd)  fftw_destroy_plan(m_pvFwd);
    if (m_pvInv)  fftw_destroy_plan(m_pvInv);
    fftw_free(m_pvIn);   fftw_free(m_pvOut);
    fftw_free(m_pvSpec); fftw_free(m_pvIfft);

    // Destroy cached noise-gate plans
    if (m_ngFwd)  fftw_destroy_plan(m_ngFwd);
    if (m_ngInv)  fftw_destroy_plan(m_ngInv);
    fftw_free(m_ngIn);   fftw_free(m_ngOut);
    fftw_free(m_ngSpec); fftw_free(m_ngIfft);

    fftw_cleanup();
}

void VocalEnhancer::resetPVState() {
    m_pvPrevPhase.clear();
    m_pvSumPhase.clear();
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
static double chunkRMS(const QVector<double>& x, int start, int len);

QByteArray VocalEnhancer::enhance(const QByteArray& input) {
    qWarning() << "VocalEnhancer Input Data Size:" << input.size();
    if (input.isEmpty() || m_frameBytes <= 0) return QByteArray();

    // Reset PV phase state so each recording starts with coherent phases
    resetPVState();

    // Convert to mono double [-1..+1]
    QVector<double> data = convertToDoubleArray(input);

    // ── Input normalisation ───────────────────────────────────────────────
    {
        const double rms = chunkRMS(data, 0, data.size());
        if (rms > 1e-6) {
            constexpr double kTargetRMS = 0.18;
            const double gain = std::clamp(kTargetRMS / rms, 1.0, 20.0);
            const double peak = *std::max_element(
                data.begin(), data.end(),
                [](double a, double b){ return std::abs(a) < std::abs(b); });
            const double safeGain = (std::abs(peak) > 1e-9)
                                  ? std::min(gain, 0.99 / std::abs(peak))
                                  : gain;
            if (safeGain > 1.01)
                applyMakeupGain(data, safeGain);
            qWarning() << "Input normalise: rms=" << rms
                       << " gain=" << safeGain;
        }
    }

    const double noiseAmount = std::clamp(m_noiseReductionAmount, 0.0, 1.0);
    const double overSub       = lerpParam(0.50, 0.85, noiseAmount);
    const double floorDb       = lerpParam(-8.0, -16.0, noiseAmount);
    const double noiseLearnSec = lerpParam(0.30, 0.50, noiseAmount);
    const double adaptivity    = lerpParam(0.015, 0.004, noiseAmount);
    const double lowEnergyDb   = lerpParam(-50.0, -46.0, noiseAmount);

    reduceNoiseSpectralGate(
        data,
        1024,
        256,
        overSub,
        floorDb,
        noiseLearnSec,
        adaptivity,
        lowEnergyDb
    );

    processPitchCorrection(data);

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

void VocalEnhancer::setPitchCorrectionAmount(double amount) {
    m_pitchCorrectionAmount = std::clamp(amount, 0.0, 1.0);
}

double VocalEnhancer::getPitchCorrectionAmount() const {
    return m_pitchCorrectionAmount;
}

void VocalEnhancer::setNoiseReductionAmount(double amount) {
    m_noiseReductionAmount = std::clamp(amount, 0.0, 1.0);
}

double VocalEnhancer::getNoiseReductionAmount() const {
    return m_noiseReductionAmount;
}

// ======================
// Pitch Detection (HPS on log spectrum)
// ======================
double VocalEnhancer::detectPitch(const QVector<double>& inputData) const {
    const int N = inputData.size();
    if (N < 1024) return 0.0;

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
// ======================
// Per-chunk pitch correction helpers
// ======================

// Pitch tuning parameters — single definition used by every chunk.
static constexpr double kDeadZoneCents      = 30.0;   // slightly wider: ignores tiny natural drift
static constexpr double kMaxCorrectionCents = 100.0;  // capped: avoids over-correction on large drifts
static constexpr double kStrength           = 0.30;   // slightly reduced: natural but audible
static constexpr double kBypassEps          = 4e-3;   // skip PV if ratio ~1

// Compute chunk RMS (used both here and in processPitchCorrection)
static double chunkRMS(const QVector<double>& x, int start, int len) {
    long double sum = 0.0;
    const int end = std::min<int>(start + len, x.size());
    for (int i = start; i < end; ++i)
        sum += (long double)x[i] * (long double)x[i];
    const int n = end - start;
    return n > 0 ? std::sqrt((double)(sum / n)) : 0.0;
}

// Pitch-correct a single chunk in-place.
// Returns the ratio that was actually applied (for inter-chunk smoothing).
// prevRatio: ratio applied to the previous chunk (smoothed into this one).
double VocalEnhancer::correctPitchChunk(QVector<double>& chunk, double prevRatio) {
    const double rmsIn = chunkRMS(chunk, 0, chunk.size());
    if (rmsIn < 5e-4) return 1.0;

    const double correctionAmount   = std::clamp(m_pitchCorrectionAmount, 0.0, 1.0);
    const double deadZoneCents      = lerpParam(30.0, 12.0, correctionAmount);
    const double maxCorrectionCents = lerpParam(100.0, 160.0, correctionAmount);
    const double strength           = lerpParam(0.30, 0.72, correctionAmount);
    const double bypassEps          = lerpParam(4e-3, 2e-3, correctionAmount);
    const double unvoicedReturn     = lerpParam(0.85, 0.70, correctionAmount);
    const double slewCentsPerSec    = lerpParam(200.0, 420.0, correctionAmount);
    const double smoothAlpha        = lerpParam(0.40, 0.10, correctionAmount);

    const double detectedPitch = detectPitch(chunk);
    const double zcr = zeroCrossingRate(chunk);
    const bool likelyUnvoiced = (detectedPitch <= 0.0)
                             || (zcr > 0.38 && rmsIn < 5e-3)
                             || (rmsIn < 5e-4);

    if (likelyUnvoiced) {
        return unvoicedReturn * prevRatio + (1.0 - unvoicedReturn) * 1.0;
    }

    const double targetFreq = findClosestNoteFrequency(detectedPitch);
    double ratioRaw = targetFreq / detectedPitch;

    double cents = 1200.0 * std::log2(ratioRaw);
    if (std::abs(cents) < deadZoneCents) {
        ratioRaw = 1.0;
        cents = 0.0;
    } else {
        cents = std::clamp(cents, -maxCorrectionCents, maxCorrectionCents);
        ratioRaw = std::pow(2.0, cents / 1200.0);
    }

    const double maxRatio = std::pow(2.0, maxCorrectionCents / 1200.0);
    double ratio = std::clamp(std::pow(ratioRaw, strength), 1.0 / maxRatio, maxRatio);

    const double chunkDur    = double(chunk.size()) / double(m_sampleRate);
    const double prevCents   = 1200.0 * std::log2(std::max(prevRatio, 1e-12));
    double       targetCents = 1200.0 * std::log2(std::max(ratio,     1e-12));
    const double maxDelta    = slewCentsPerSec * chunkDur;
    targetCents = prevCents + std::clamp(targetCents - prevCents, -maxDelta, +maxDelta);
    ratio = std::pow(2.0, targetCents / 1200.0);

    ratio = prevRatio * smoothAlpha + ratio * (1.0 - smoothAlpha);

    banner = QString("Chunk PV: det=%1Hz tgt=%2Hz cents=%3 r=%4 zcr=%5")
                 .arg(detectedPitch, 0, 'f', 2)
                 .arg(targetFreq,    0, 'f', 2)
                 .arg(targetCents,   0, 'f', 1)
                 .arg(ratio,         0, 'f', 4)
                 .arg(zcr,           0, 'f', 3);

    if (std::abs(ratio - 1.0) > bypassEps) {
        chunk = pitchShiftPhaseVocoder(chunk, ratio);
    }

    const double rmsAfter = chunkRMS(chunk, 0, chunk.size());
    if (rmsAfter > 1e-9) {
        const double compensation = 1.85;
        const double gain = std::clamp((rmsIn / rmsAfter) * compensation, 0.80, 2.50);
        applyMakeupGain(chunk, gain);
    }

    return ratio;
}

// ======================
// Main pitch correction pipeline (frame-by-frame)
// ======================
void VocalEnhancer::processPitchCorrection(QVector<double>& data) {

    const double globalRMS = chunkRMS(data, 0, data.size());
    if (globalRMS < 1e-6) {
        banner = "Enhancer: silence (bypass)";
        progressValue = 1.0;
        return;
    }

    const int totalSamples = data.size();
    const int chunkSamples = std::max(4096, (m_sampleRate * 140) / 1000);
    const int hopSamples   = std::max(1, chunkSamples / 4);  // 75% overlap

    QVector<double> window = createHannWindow(chunkSamples);
    QVector<double> out(totalSamples + chunkSamples, 0.0);
    QVector<double> wSum(totalSamples + chunkSamples, 0.0);

    double prevRatio = 1.0;

    const int totalChunks = std::max(1, (totalSamples + hopSamples - 1) / hopSamples);
    int chunkIndex = 0;

    for (int pos = 0; pos < totalSamples; pos += hopSamples, ++chunkIndex) {
        const int valid = std::min(chunkSamples, totalSamples - pos);

        QVector<double> dryChunk(chunkSamples, 0.0);
        for (int i = 0; i < valid; ++i)
            dryChunk[i] = data[pos + i];

        QVector<double> wetChunk = dryChunk;
        const double ratio = correctPitchChunk(wetChunk, prevRatio);
        prevRatio = ratio;

        const double wetMix = std::clamp(
            (std::abs(ratio - 1.0) - kBypassEps) / 0.015, 0.0, 1.0);
        const double dryMix = 1.0 - wetMix;

        // Banner dinâmico real do pipeline principal
        const double chunkStartSec = double(pos) / double(std::max(1, m_sampleRate));
        const double chunkEndSec   = double(pos + valid) / double(std::max(1, m_sampleRate));
        const double zcr           = zeroCrossingRate(dryChunk);
        const double chunkLevel    = chunkRMS(dryChunk, 0, valid);

        banner = QString(
            "Pitch correction %1/%2  [%3s-%4s]  r=%5  wet=%6  zcr=%7  rms=%8")
                     .arg(chunkIndex + 1)
                     .arg(totalChunks)
                     .arg(chunkStartSec, 0, 'f', 2)
                     .arg(chunkEndSec,   0, 'f', 2)
                     .arg(ratio,         0, 'f', 4)
                     .arg(wetMix,        0, 'f', 2)
                     .arg(zcr,           0, 'f', 3)
                     .arg(chunkLevel,    0, 'f', 4);

        progressValue = std::clamp(double(chunkIndex + 1) / double(totalChunks), 0.0, 1.0);

        for (int i = 0; i < valid; ++i) {
            const double w = window[i];
            const double s = dryChunk[i] * dryMix + wetChunk[i] * wetMix;
            out[pos + i]  += s * w;
            wSum[pos + i] += w * w;
        }
    }

    banner = "Reconstructing overlap-add...";
    progressValue = 0.96;

    for (int i = 0; i < totalSamples; ++i) {
        data[i] = (wSum[i] > 1e-12)
                ? std::clamp(out[i] / wSum[i], -1.0, 1.0)
                : std::clamp(data[i], -1.0, 1.0);
    }

    banner = "Applying dynamics / exciter / limiter...";
    progressValue = 0.98;

    compressDynamics(data, 0.78, 2.2);
    harmonicExciter(data, 1.12, 0.18);
    applyLimiter(data, 0.98);

    banner = QString("Done: %1 chunks, len=%2 hop=%3")
                 .arg(totalChunks)
                 .arg(chunkSamples)
                 .arg(hopSamples);
    progressValue = 1.0;
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

    // Measure RMS before compression for make-up gain calculation
    long double rmsAccPre = 0.0;
    for (double s : x) rmsAccPre += (long double)s * s;
    const double rmsPre = std::sqrt((double)(rmsAccPre / std::max<int>(1, x.size())));

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

    // Automatic make-up gain: restore loudness lost to gain reduction.
    // Clamped conservatively to avoid over-driving the limiter downstream.
    long double rmsAccPost = 0.0;
    for (double s : x) rmsAccPost += (long double)s * s;
    const double rmsPost = std::sqrt((double)(rmsAccPost / std::max<int>(1, x.size())));

    if (rmsPost > 1e-9 && rmsPre > 1e-9) {
        const double makeUp = std::clamp(rmsPre / rmsPost, 1.0, 2.5);
        for (auto& s : x) s *= makeUp;
    }
}

void VocalEnhancer::harmonicExciter(QVector<double>& x, double drive, double mix) {
    drive = std::max(0.0, drive);
    mix   = std::clamp(mix, 0.0, 1.0);

    // Band-limited exciter: only saturate the high-mid shelf (>= ~3 kHz).
    // Running tanh over the full spectrum adds harmonics to sibilants (S/T),
    // making them harsh and fatiguing. A first-order high-pass isolates the
    // upper partials that benefit from excitation, leaving the fundamental
    // and low harmonics clean.
    //
    // First-order IIR high-pass: y[n] = a*(y[n-1] + x[n] - x[n-1])
    //   a = 1 / (1 + 2π·fc/fs)  — bilinear approximation
    const double fc = 3000.0;  // Hz — crossover
    const double a  = 1.0 / (1.0 + 2.0 * kPi * fc / std::max(1, m_sampleRate));

    double hpPrev  = 0.0;  // HP filter state
    double xPrev   = 0.0;  // previous input sample

    for (auto& s : x) {
        // High-pass the input
        const double hp = a * (hpPrev + s - xPrev);
        hpPrev = hp;
        xPrev  = s;

        // Saturate only the high-passed signal
        const double excited = std::tanh(hp * drive);

        // Blend: original dry + band-limited saturation
        s = s + excited * mix;
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

/////////// NOISE REDUCTION (SPECTRAL GATE)

void VocalEnhancer::reduceNoiseSpectralGate(QVector<double>& x,
                                            int fftSize,
                                            int hopSize,
                                            double overSub,
                                            double floorDb,
                                            double noiseLearnSec,
                                            double adaptivity,
                                            double lowEnergyDb)
{
    if (x.isEmpty()) return;

    const int N = (fftSize > 0 ? fftSize : 1024);
    const int H = (hopSize > 0 ? hopSize : N / 4);
    if (N < 256 || H < 1) return;

    // Windows and overlap-add buffers
    QVector<double> window = createHannWindow(N);

    // Output will be same length as input (with a little padding)
    const int outCap = x.size() + N + H + 4;
    QVector<double> out(outCap, 0.0);
    QVector<double> winSum(outCap, 0.0);

    // Use cached plans when the caller requests the standard N=1024 size.
    // For any other fftSize fall back to a temporary allocation.
    const bool useCached = (N == kNgN) && m_ngFwd && m_ngInv;

    double*       fftIn  = useCached ? m_ngIn   : (double*)      fftw_malloc(sizeof(double)       * N);
    fftw_complex* fftOut = useCached ? m_ngOut  : (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (N/2+1));
    fftw_complex* spec   = useCached ? m_ngSpec : (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (N/2+1));
    double*       ifftOut= useCached ? m_ngIfft : (double*)      fftw_malloc(sizeof(double)       * N);

    if (!fftIn || !fftOut || !spec || !ifftOut) {
        if (!useCached) {
            fftw_free(fftIn); fftw_free(fftOut);
            fftw_free(spec);  fftw_free(ifftOut);
        }
        return;
    }

    fftw_plan fwd = useCached ? m_ngFwd : fftw_plan_dft_r2c_1d(N, fftIn, fftOut, FFTW_ESTIMATE);
    fftw_plan inv = useCached ? m_ngInv : fftw_plan_dft_c2r_1d(N, spec,  ifftOut, FFTW_ESTIMATE);

    const int bins = N / 2 + 1;

    // Noise profile (magnitude per bin)
    QVector<double> noiseMag(bins, 0.0);
    QVector<int>    noiseCnt(bins, 0);

    // Gain smoothing to reduce musical noise (per-bin)
    QVector<double> prevGain(bins, 1.0);

    // Floor in linear gain
    const double Gfloor = std::clamp(dbToLinear(floorDb), 0.0, 1.0);

    // How many frames to learn the initial noise profile
    const int learnFramesTarget = std::max(1, int((noiseLearnSec * m_sampleRate) / H));

    // =============== First pass: learn noise from the first frames ===============
    int inPos = 0;
    int frames = 0;

    while (inPos + N <= x.size() && frames < learnFramesTarget) {
        // Copy & window
        double frameRmsAcc = 0.0;
        for (int i = 0; i < N; ++i) {
            double s = x[inPos + i];
            frameRmsAcc += s * s;
            fftIn[i] = s * window[i];
        }

        const double frameRms = std::sqrt(frameRmsAcc / N);

        // Shortcuts: even if it's not purely noise, an average of a few frames
        // is fine as an initial guess; we’ll adapt slowly later on.
        fftw_execute(fwd);

        for (int k = 0; k < bins; ++k) {
            const double re = fftOut[k][0];
            const double im = fftOut[k][1];
            const double mag = std::sqrt(re * re + im * im) + 1e-12;

            noiseMag[k] += mag;
            noiseCnt[k] += 1;
        }

        inPos += H;
        frames += 1;
    }

    // Average the accumulated noise magnitude
    for (int k = 0; k < bins; ++k) {
        if (noiseCnt[k] > 0) noiseMag[k] /= double(noiseCnt[k]);
        if (noiseMag[k] <= 0.0) noiseMag[k] = 1e-12;
    }

    // Reset for full processing pass
    inPos = 0;
    int outPos = 0;
    frames = 0;

    // Temporal smoothing constants for gain (0..1). Larger = more smoothing.
    const double atk = 0.60;  // when gain needs to go down (more suppression)
    const double rel = 0.85;  // when gain can go up (less suppression)

    // A tiny 3-bin frequency smoothing kernel can tame isolated spikes
    auto smooth3 = [&](const QVector<double>& v, QVector<double>& y) {
        y.resize(v.size());
        if (v.size() == 1) { y[0] = v[0]; return; }
        y[0] = 0.75 * v[0] + 0.25 * v[1];
        for (int k = 1; k < v.size() - 1; ++k)
            y[k] = 0.25 * v[k - 1] + 0.5 * v[k] + 0.25 * v[k + 1];
        y[v.size() - 1] = 0.25 * v[v.size() - 2] + 0.75 * v[v.size() - 1];
    };

    QVector<double> magBuf(bins), magSm(bins);

    // =============== Second pass: apply spectral gating ===============
    while (inPos + N <= x.size() && outPos + N < out.size()) {
        double frameRmsAcc = 0.0;
        for (int i = 0; i < N; ++i) {
            double s = x[inPos + i];
            frameRmsAcc += s * s;
            fftIn[i] = s * window[i];
        }
        const double frameRms = std::sqrt(frameRmsAcc / N);
        const double frameDbFS = 20.0 * std::log10(std::max(frameRms, 1e-12)); // approximate dBFS

        fftw_execute(fwd);

        // Magnitudes
        for (int k = 0; k < bins; ++k) {
            const double re = fftOut[k][0];
            const double im = fftOut[k][1];
            magBuf[k] = std::sqrt(re * re + im * im) + 1e-12;
        }

        // Optional small frequency smoothing
        smooth3(magBuf, magSm);

        // Compute gains
        for (int k = 0; k < bins; ++k) {
            const double mag   = std::max(magSm[k], 1e-12);
            const double noise = std::max(noiseMag[k], 1e-12);

            // Simple spectral subtraction style gain
            double G = 1.0 - overSub * (noise / mag);
            G = std::clamp(G, Gfloor, 1.0);

            // Temporal smoothing (per bin)
            double gPrev = prevGain[k];
            double gSm   = (G < gPrev)
                           ? (atk * gPrev + (1.0 - atk) * G)   // faster down
                           : (rel * gPrev + (1.0 - rel) * G);  // slower up
            prevGain[k] = gSm;

            // Re-apply with phase
            const double re = fftOut[k][0];
            const double im = fftOut[k][1];

            // Avoid blowing away bins near DC completely
            double gUse = gSm;
            if (k <= 2) gUse = std::max(gUse, 0.5 * Gfloor);

            spec[k][0] = re * gUse;
            spec[k][1] = im * gUse;
        }

        // Slow noise model adaptation during low-energy frames
        // Helps when noise changes between sentences/phrases
        if (frameDbFS < lowEnergyDb && adaptivity > 1e-6) {
            const double a = std::clamp(adaptivity, 0.0, 0.5); // keep small
            for (int k = 0; k < bins; ++k) {
                // Smoothly track downwards and (very) slowly upwards
                noiseMag[k] = (1.0 - a) * noiseMag[k] + a * magSm[k];
                if (noiseMag[k] <= 1e-12) noiseMag[k] = 1e-12;
            }
        }

        // IFFT
        fftw_execute(inv);

        // Overlap-add with squared window compensation
        for (int i = 0; i < N; ++i) {
            double s = (ifftOut[i] / N) * window[i];
            int idx = outPos + i;
            out[idx]    += s;
            winSum[idx] += window[i] * window[i];
        }

        inPos += H;
        outPos += H;
        frames += 1;
    }

    // Normalize by window energy
    for (int i = 0; i < out.size(); ++i) {
        if (winSum[i] > 1e-12) out[i] /= winSum[i];
    }

    // Copy back to x (trim to original length)
    x.resize(std::min<int>(x.size(), out.size()));
    for (int i = 0; i < x.size(); ++i) x[i] = std::clamp(out[i], -1.0, 1.0);

    // Cleanup — only free if we allocated locally (not using cached plans)
    if (!useCached) {
        fftw_destroy_plan(fwd);
        fftw_destroy_plan(inv);
        fftw_free(fftIn);
        fftw_free(fftOut);
        fftw_free(spec);
        fftw_free(ifftOut);
    }

    // Optional banner update
    banner = "Noise reduction (spectral gate) applied";
}

// ======================
// Phase Vocoder
// ======================
QVector<double> VocalEnhancer::timeStretchPhaseVocoder(const QVector<double>& in, double stretch) {
    if (in.isEmpty() || stretch <= 0.0) return in;

    // N=2048, Ha=N/8 (87.5% overlap, 256-sample hop @ 44100 Hz = ~6ms).
    // More overlap = more frames per second = smoother phase accumulation
    // = dramatically less metallic/phasey artifacts vs the previous N/4.
    // The cached FFTW plans are sized for kPvN=2048 so this fits exactly.
    const int N  = kPvN;   // 2048
    const int Ha = N / 8;  // analysis hop = 256 (87.5% overlap)

    const int Hs = std::max(1, int(std::round(Ha * stretch)));  // synthesis hop

    QVector<double> window = createHannWindow(N);

    // (winSum per-sample normalization used instead of a COLA scalar)

    const int outCap = int(in.size() * stretch) + N * 2 + 4;
    QVector<double> out(outCap, 0.0);

    double* fftIn    = m_pvIn;
    fftw_complex* fftOut = m_pvOut;
    fftw_complex* spec   = m_pvSpec;
    double* ifftOut  = m_pvIfft;

    if (!fftIn || !fftOut || !spec || !ifftOut || !m_pvFwd || !m_pvInv) {
        return in;  // plans failed to initialise — safe fallback
    }

    fftw_plan fwd = m_pvFwd;
    fftw_plan inv = m_pvInv;

    const int bins = N / 2 + 1;

    // Per-bin state — prevPhase and sumPhase are class members so they
    // persist across consecutive chunk calls (no phase reset at boundaries).
    if (m_pvPrevPhase.size() != bins) { m_pvPrevPhase.assign(bins, 0.0); }
    if (m_pvSumPhase.size()  != bins) { m_pvSumPhase.assign(bins,  0.0); }
    QVector<double>& prevPhase = m_pvPrevPhase;
    QVector<double>& sumPhase  = m_pvSumPhase;
    QVector<double>  trueFreq(bins, 0.0);
    QVector<double>  mag     (bins, 0.0);

    // Expected phase advance per analysis hop (centre frequency of each bin)
    QVector<double> omega(bins);
    for (int k = 0; k < bins; ++k)
        omega[k] = 2.0 * kPi * k / N;

    // Per-sample OLA weight accumulator for accurate boundary normalization
    QVector<double> winSum(outCap, 0.0);

    int inPos  = 0;
    int outPos = 0;

    while (inPos + N <= in.size() && outPos + N < out.size()) {

        // ── Analysis FFT ──────────────────────────────────────────────────
        for (int i = 0; i < N; ++i)
            fftIn[i] = in[inPos + i] * window[i];

        fftw_execute(fwd);

        // ── Instantaneous frequency estimation ────────────────────────────
        for (int k = 0; k < bins; ++k) {
            const double re = fftOut[k][0];
            const double im = fftOut[k][1];

            mag[k]           = std::sqrt(re * re + im * im);
            const double ph  = std::atan2(im, re);

            // Deviation from expected phase advance at bin centre frequency
            double delta     = ph - prevPhase[k] - omega[k] * Ha;
            delta            = principalArg(delta);

            trueFreq[k]      = omega[k] + delta / Ha;
            prevPhase[k]     = ph;

            // Accumulate synthesis phase — every bin evolves independently.
            // No leader/follower snapping: that causes periodic phase resets
            // which manifest as flanging regardless of update interval.
            sumPhase[k]     += trueFreq[k] * Hs;
        }

        // ── Synthesis IFFT ────────────────────────────────────────────────
        for (int k = 0; k < bins; ++k) {
            spec[k][0] = mag[k] * std::cos(sumPhase[k]);
            spec[k][1] = mag[k] * std::sin(sumPhase[k]);
        }

        fftw_execute(inv);

        // Overlap-add with per-sample window weight tracking
        for (int i = 0; i < N; ++i) {
            const int idx    = outPos + i;
            out[idx]        += (ifftOut[i] / N) * window[i];
            winSum[idx]     += window[i] * window[i];
        }

        inPos  += Ha;
        outPos += Hs;
    }

    // Normalise by accumulated window energy — accurate at chunk boundaries
    const int validOut = std::min<int>(outPos + N, out.size());
    for (int i = 0; i < validOut; ++i)
        if (winSum[i] > 1e-10) out[i] /= winSum[i];

    out.resize(validOut);

    // Plans and buffers are owned by the class — do not free here.

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Continuous pitch shift — one PV pass, per-frame ratio curve, no restarts
// ─────────────────────────────────────────────────────────────────────────────
QVector<double> VocalEnhancer::pitchShiftContinuous(const QVector<double>& in,
                                                     const QVector<double>& frameRatio)
{
    if (in.isEmpty() || frameRatio.isEmpty()) return in;

    // ── Fixed analysis AND synthesis hop ──────────────────────────────────
    // Ha == Hs always. Duration is preserved exactly — no resample needed,
    // no time drift, no chipmunk effect from accumulated Hs rounding error.
    //
    // Pitch is shifted by scaling the instantaneous frequency before
    // accumulating into sumPhase:
    //   sumPhase[k] += trueFreq[k] * ratio * Ha
    // This rotates spectral bins upward/downward by `ratio` each frame
    // without moving any samples in time.
    const int N  = kPvN;
    const int Ha = N / 8;   // analysis hop = synthesis hop = 256

    QVector<double> window = createHannWindow(N);
    const int bins = N / 2 + 1;

    QVector<double> out   (in.size(), 0.0);
    QVector<double> winSum(in.size(), 0.0);

    QVector<double> prevPhase(bins, 0.0);
    QVector<double> sumPhase (bins, 0.0);
    QVector<double> mag      (bins, 0.0);

    QVector<double> omega(bins);
    for (int k = 0; k < bins; ++k)
        omega[k] = 2.0 * kPi * k / N;

    double*       fftIn  = m_pvIn;
    fftw_complex* fftOut = m_pvOut;
    fftw_complex* spec   = m_pvSpec;
    double*       ifftOut = m_pvIfft;

    if (!fftIn || !fftOut || !spec || !ifftOut || !m_pvFwd || !m_pvInv)
        return in;

    int inPos = 0;
    int frame = 0;

    while (inPos + N <= in.size()) {
        const double ratio = (frame < frameRatio.size())
                           ? frameRatio[frame] : frameRatio.last();

        // Analysis FFT
        for (int i = 0; i < N; ++i)
            fftIn[i] = in[inPos + i] * window[i];
        fftw_execute(m_pvFwd);

        // Instantaneous frequency → scale by ratio → accumulate phase
        for (int k = 0; k < bins; ++k) {
            const double re = fftOut[k][0];
            const double im = fftOut[k][1];
            mag[k]          = std::sqrt(re * re + im * im);
            const double ph = std::atan2(im, re);

            double delta    = ph - prevPhase[k] - omega[k] * Ha;
            delta           = principalArg(delta);
            const double trueFreq = omega[k] + delta / Ha;
            prevPhase[k]    = ph;

            // Multiply trueFreq by ratio: shifts pitch, preserves time.
            // Wrap sumPhase into [-π, π] every frame to prevent precision loss
            // in cos/sin as sumPhase grows to millions of radians over long songs.
            sumPhase[k] = principalArg(sumPhase[k] + trueFreq * ratio * Ha);
        }

        // Synthesis IFFT
        for (int k = 0; k < bins; ++k) {
            spec[k][0] = mag[k] * std::cos(sumPhase[k]);
            spec[k][1] = mag[k] * std::sin(sumPhase[k]);
        }
        fftw_execute(m_pvInv);

        // OLA — output position == input position (Ha == Hs)
        for (int i = 0; i < N; ++i) {
            const int idx = inPos + i;
            if (idx < in.size()) {
                out[idx]    += (ifftOut[i] / N) * window[i];
                winSum[idx] += window[i] * window[i];
            }
        }

        inPos += Ha;
        ++frame;
    }

    // Normalise by window weight; where coverage is low (edges of buffer)
    // blend with the original dry signal to avoid attenuation artifacts.
    // A well-covered sample has winSum ≥ ~0.5; below that we cross-fade in
    // the dry signal so the edges never go silent.
    const double minCoverage = 0.5;
    for (int i = 0; i < in.size(); ++i) {
        if (winSum[i] >= minCoverage) {
            out[i] /= winSum[i];
        } else if (winSum[i] > 1e-10) {
            const double wet = winSum[i] / minCoverage;   // 0→1 as coverage grows
            out[i] = (out[i] / winSum[i]) * wet + in[i] * (1.0 - wet);
        } else {
            out[i] = in[i];  // no coverage at all — pass dry
        }
    }

    // No resample needed — output is exactly in.size() samples
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

        out[i] = std::clamp(cubicInterpolate(v0, v1, v2, v3, frac), -1.0, 1.0);
    }

    return out;
}