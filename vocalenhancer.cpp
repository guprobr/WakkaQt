#include "vocalenhancer.h"

#include <QDebug>
#include <cmath>
#include <complex>
#include <fftw3.h>

const double VocalEnhancer::A440 = 440.0; // Frequency of A4
const int VocalEnhancer::MIDI_NOTES = 128; // Total MIDI notes
const double VocalEnhancer::NOTE_FREQUENCIES[MIDI_NOTES] = {
    8.17579891564, 8.66195721803, 9.17702473395, 9.72271824124, 10.3008611537, 10.9133822323, 11.5623257097, 
    12.2498573744, 12.9782717994, 13.75, 14.5676175474, 15.4338531643, 16.3515978313, 17.3239144361, 
    18.3540474679, 19.4454364826, 20.6017223071, 21.8267644646, 23.1246514195, 24.4997147489, 25.9565435987, 
    27.5, 29.1352350949, 30.8677063285, 32.7031956626, 34.6478288721, 36.7080949358, 38.8908729653, 
    41.2034446141, 43.6535289291, 46.2493028390, 48.9994294977, 51.9130871975, 55.0, 58.2704701898, 
    61.7354126570, 65.4063913251, 69.2956577442, 73.4161898716, 77.7817459305, 82.4068892282, 87.3070578583, 
    92.4986056779, 97.9988589954, 103.826174395, 110.0, 116.54094038, 123.470825314, 130.812782650, 
    138.591315488, 146.832379743, 155.563491861, 164.813778456, 174.614115717, 184.997211356, 
    195.99771799, 207.65234879, 220.0, 233.081880759, 246.941650628, 261.625565301, 277.182630976, 
    293.664759484, 311.126983722, 329.627556912, 349.228231434, 369.994422712, 391.995435981, 
    415.304697579, 440.0, 466.163761518, 493.883301256, 523.251130601, 554.365261953, 587.329518995, 
    622.253967444, 659.255113825, 698.456462868, 739.988845423, 783.990871963, 830.609395158, 880.0, 
    932.327523036, 987.766602512, 1046.5022612, 1108.73052391, 1174.65903799, 1244.50793489, 1318.51022765, 
    1396.91292574, 1479.97769085, 1567.98174393, 1661.21879032, 1760.0, 1864.65504607, 1975.53320502, 
    2093.0045224, 2217.46104781, 2349.31807586, 2489.01586978, 2637.0204553, 2793.82585146, 2959.95538169, 
    3135.96348785, 3322.43758064, 3520.0, 3729.31009214, 3951.06641005, 4186.00904481
};

VocalEnhancer::VocalEnhancer(const QAudioFormat& format, QObject *parent)
    : QObject(parent),  
      banner("Begin Vocal Enhancement"),
      m_sampleSize(format.bytesPerSample()),
      m_sampleRate(format.sampleRate()),
      m_numSamples(format.sampleRate() * calculateDuration(format.sampleRate()) / 1000) { }

QByteArray VocalEnhancer::enhance(const QByteArray& input) {
    qWarning() << "VocalEnhancer Input Data Size:" << input.size();
    if (input.isEmpty()) return QByteArray();

    int sampleCount = input.size() / m_sampleSize;
    QByteArray output(sampleCount * m_sampleSize, 0);
    

    QVector<double> inputData = convertToDoubleArray(input, sampleCount);
    //int targetSize = inputData.size();
    //normalizeAndApplyGain(inputData, 0.8); // sanitize for pitch correction and effects
    qWarning() << "VocalEnhancer processing pitch correction";
    processPitchCorrection(inputData);
    //normalizeAndApplyGain(inputData, 0.8); // normalize again at the very end
    //resizeOutputToMatchInput(inputData, targetSize); // fix sync issues 
    convertToQByteArray(inputData, output);

    return output;
}

QVector<double> VocalEnhancer::convertToDoubleArray(const QByteArray& input, int sampleCount) {
    QVector<double> data(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        Q_ASSERT(i * m_sampleSize < input.size());
        data[i] = normalizeSample(input, i);
    }
    return data;
}

void VocalEnhancer::convertToQByteArray(const QVector<double>& inputData, QByteArray& output) {
    for (int i = 0; i < inputData.size(); ++i) {
        int sampleValue = denormalizeSample(inputData[i]);
        if (m_sampleSize == 2) {
            reinterpret_cast<int16_t*>(output.data())[i] = static_cast<int16_t>(sampleValue);
        } else if (m_sampleSize == 3) {
            uint8_t* sample = reinterpret_cast<uint8_t*>(output.data() + i * 3);
            sample[0] = static_cast<uint8_t>(sampleValue & 0xFF);
            sample[1] = static_cast<uint8_t>((sampleValue >> 8) & 0xFF);
            sample[2] = static_cast<uint8_t>((sampleValue >> 16) & 0xFF);
        } else if (m_sampleSize == 4) {
            reinterpret_cast<int32_t*>(output.data())[i] = static_cast<int32_t>(sampleValue);
        }
    }
}

double VocalEnhancer::normalizeSample(const QByteArray& input, int index) const {
    if (m_sampleSize == 2) { // 16-bit
        return static_cast<double>(reinterpret_cast<const int16_t*>(input.constData())[index]) / 32768.0;
    } else if (m_sampleSize == 3) { // 24-bit
        const uint8_t* sample = reinterpret_cast<const uint8_t*>(input.constData() + index * 3);
        int32_t sampleValue = (sample[2] << 16) | (sample[1] << 8) | sample[0];
        return static_cast<double>(sampleValue) / (1 << 23);
    } else if (m_sampleSize == 4) { // 32-bit
        return static_cast<double>(reinterpret_cast<const int32_t*>(input.constData())[index]) / 2147483648.0;
    }
    qWarning() << "Unsupported sample size:" << m_sampleSize;
    return 0.0;
}

int VocalEnhancer::denormalizeSample(double value) const {
    if (m_sampleSize == 2) {
        return static_cast<int>(value * 32767);
    } else if (m_sampleSize == 3) {
        return static_cast<int>(value * (1 << 23));
    } else if (m_sampleSize == 4) {
        return static_cast<int>(value * 2147483647);
    }
    return 0;
}

double VocalEnhancer::detectPitch(const QVector<double>& inputData) const {
    const int N = inputData.size();
    if (N < 2048) return 0.0;

    const double minFreq = 80.0;
    const double maxFreq = 1000.0;

    // Use um bloco fixo (melhor para pitch do que FFT do arquivo inteiro)
    const int fftN = 4096; // offline: melhor resolução
    if (N < fftN) return 0.0;

    // Pega um trecho “central” para evitar começo/fim com silêncio
    const int start = (N - fftN) / 2;

    QVector<double> window = createHannWindow(fftN);

    // Remove DC e aplica janela
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

    // log magnitude (evita underflow e estabiliza HPS)
    for (int k = 0; k < bins; ++k) {
        double re = fftOut[k][0];
        double im = fftOut[k][1];
        double mag = std::sqrt(re*re + im*im);
        logMag[k] = std::log(mag + 1e-12);
    }

    QVector<double> hps = logMag; // em log: multiplicação vira soma

    const int maxHarmonics = 5;
    for (int h = 2; h <= maxHarmonics; ++h) {
        for (int k = 0; k < bins / h; ++k) {
            hps[k] += logMag[k * h];
        }
    }

    int minBin = int(std::floor(minFreq * fftN / m_sampleRate));
    int maxBin = int(std::ceil (maxFreq * fftN / m_sampleRate));
    minBin = qBound(1, minBin, bins - 1);
    maxBin = qBound(1, maxBin, bins - 1);

    double bestVal = -1e300;
    int bestBin = minBin;

    for (int k = minBin; k <= maxBin; ++k) {
        if (hps[k] > bestVal) {
            bestVal = hps[k];
            bestBin = k;
        }
    }

    // Refinamento parabólico (melhora precisão do bin)
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
    return detectedFreq;
}

void VocalEnhancer::processPitchCorrection(QVector<double>& data) {

    auto computeRMS = [](const QVector<double>& x) {
        long double sum = 0.0;
        for (double s : x) sum += (long double)s * (long double)s;
        return x.isEmpty() ? 0.0 : std::sqrt((double)(sum / x.size()));
    };

    double rmsIn = computeRMS(data);

    double detectedPitch = detectPitch(data);
    if (detectedPitch <= 0.0) detectedPitch = A440;

    double targetFrequency = findClosestNoteFrequency(detectedPitch);
    double ratio = targetFrequency / detectedPitch;

    // Clamps conservadores para soar “natural”
    ratio = qBound(0.90, ratio, 1.12);

    banner = QString("Pitch shift PV ratio=%1").arg(ratio, 0, 'f', 4);

    // Pitch shift de qualidade
    data = pitchShiftPhaseVocoder(data, ratio);

    compressDynamics(data, 0.6, 4.0);
    harmonicExciter(data, 2.2, 0.25);
    applyEcho(data, 0.8, 0.5, 128, 269, 0.28, 0.16);

    double rmsOut = computeRMS(data);
    double compensation = 2.0;    // 1.6–2.2 tipical with PV + afftdn

    if (rmsOut > 1e-9 && rmsIn > 1e-9) {
        double gain = (rmsIn / rmsOut) * compensation;
        applyMakeupGain(data, gain);
    }

    applyLimiter(data, 0.98); // limiter always at the end
    
}

void VocalEnhancer::applyLimiter(QVector<double>& x, double ceiling) {
    ceiling = qBound(0.1, ceiling, 0.999);

    double maxA = 0.0;
    for (double s : x) maxA = std::max(maxA, std::abs(s));

    if (maxA < 1e-12) return;

    double g = ceiling / maxA;
    if (g < 1.0) {
        for (auto &s : x) s *= g;
    }
}

void VocalEnhancer::applyMakeupGain(QVector<double>& x, double gain) {
    if (gain <= 0.0) return;
    for (auto &s : x) s *= gain;
}

void VocalEnhancer::normalizeRMS(QVector<double>& x, double targetRMS) {
    double sum = 0.0;
    for (double s : x) sum += s * s;
    double rms = std::sqrt(sum / x.size());

    if (rms < 1e-9) return;

    double g = targetRMS / rms;
    for (auto &s : x) s *= g;
}


void VocalEnhancer::normalizeAndApplyGain(QVector<double>& data, double gain) {
    double maxAmplitude = 0.0;
    for (const auto& value : data) {
        maxAmplitude = std::max(maxAmplitude, std::abs(value));
    }
    if (maxAmplitude > 0) {
        double normalizationFactor = gain / maxAmplitude;
        for (auto& value : data) {
            value = qBound(-1.0, value * normalizationFactor, 1.0);
        }
    }
}

QVector<double> VocalEnhancer::harmonicScale(const QVector<double>& data, double scaleFactor) {
    const int windowSize = 2048;
    const int hopSize = windowSize / 4;

    QVector<double> output(data.size(), 0.0);
    QVector<double> windowSum(data.size(), 0.0);

    QVector<double> window = createHannWindow(windowSize);
    int numFrames = (data.size() - windowSize) / hopSize;

    for (int frame = 0; frame < numFrames; ++frame) {
        int start = frame * hopSize;

        QVector<double> segment = extractSegment(data, window, start, windowSize);
        QVector<double> stretched = timeStretch(segment, scaleFactor);

        for (int i = 0; i < stretched.size(); ++i) {
            int idx = start + i;
            if (idx < output.size() && i < window.size()) {
                double w = window[i];
                output[idx] += stretched[i] * w;
                windowSum[idx] += w;
            }
        }

        progressValue = double(frame) / numFrames;
    }

    // --- Normalização OLA ---
    for (int i = 0; i < output.size(); ++i) {
        if (windowSum[i] > 1e-9)
            output[i] /= windowSum[i];
    }

    return output;
}

QVector<double> VocalEnhancer::extractSegment(const QVector<double>& data, const QVector<double>& window, int start, int size) const {
    // Ensure size does not exceed the data or window bounds
    size = qMin(size, qMin(data.size() - start, window.size()));
    QVector<double> segment(size);

    for (int i = 0; i < size; ++i) {
        int idx = start + i;
        if (idx < data.size() && i < window.size()) {
            segment[i] = data[idx] * window[i];
        } else {
            segment[i] = 0.0; // Pad with 0 if out of range
        }
    }

    return segment;
}

QVector<double> VocalEnhancer::timeStretch(const QVector<double>& segment, double shiftRatio) {
    qint64 newSize = static_cast<qint64>(segment.size() * shiftRatio);
    QVector<double> output(newSize, 0.0);

    for (qint64 i = 1; i < newSize - 1; ++i) {
        double t = static_cast<double>(i) / shiftRatio;
        qint64 idx = static_cast<int>(t);

        // Boundary check for cubic interpolation
        if (idx > 0 && idx + 2 < segment.size()) {
            output[i] = cubicInterpolate(
                segment[idx - 1], segment[idx], segment[idx + 1], segment[idx + 2], t - idx
            );
        } else if (idx < segment.size()) {
            output[i] = segment[qBound(0, idx, segment.size() - 1)];
        }
    }

    return output;
}


void VocalEnhancer::addScaledSegment(QVector<double>& outputData, const QVector<double>& scaledSegment, int start, const QVector<double>& window) const {
    for (int i = 0; i < scaledSegment.size(); ++i) {
        int idx = start + i;
        if (idx < outputData.size() && i < window.size()) {
            outputData[idx] += scaledSegment[i] * window[i];
        }
    }
}

double VocalEnhancer::cubicInterpolate(double v0, double v1, double v2, double v3, double t) const {
    double a0 = v3 - v2 - v0 + v1;
    double a1 = v0 - v1 - a0;
    double a2 = v2 - v0;
    return (a0 * t * t * t + a1 * t * t + a2 * t + v1);
}

double VocalEnhancer::findClosestNoteFrequency(double frequency) const{
    double closestFreq = NOTE_FREQUENCIES[0];
    double minDiff = std::abs(frequency - closestFreq);

    for (int i = 1; i < MIDI_NOTES; ++i) {
        double diff = std::abs(frequency - NOTE_FREQUENCIES[i]);
        if (diff < minDiff) {
            minDiff = diff;
            closestFreq = NOTE_FREQUENCIES[i];
        }
    }
    return closestFreq;
}

double VocalEnhancer::calculateDuration(int sampleRate) const {
    // Calculate the duration based on the sample rate and number of samples
    return static_cast<double>(m_numSamples) / sampleRate * 1000.0; // Duration in milliseconds
}

void VocalEnhancer::compressDynamics(QVector<double>& x, double threshold, double ratio) {
    threshold = qBound(0.0, threshold, 1.0);     // ex: 0.6
    ratio = std::max(1.0, ratio);                // ex: 4.0

    for (auto &s : x) {
        double a = std::abs(s);
        if (a > threshold) {
            double excess = a - threshold;
            double comp = threshold + excess / ratio;
            s = (s < 0 ? -comp : comp);
        }
    }
}


void VocalEnhancer::harmonicExciter(QVector<double>& x, double drive, double mix) {
    drive = std::max(0.0, drive);
    mix = qBound(0.0, mix, 1.0);

    for (auto &s : x) {
        double clean = s;
        double driven = std::tanh(clean * drive); // soft clip harmônico
        s = clean * (1.0 - mix) + driven * mix;
    }
}

void VocalEnhancer::applyEcho(QVector<double>& inputData, double gainIn, double gainOut, double delayMs1, double delayMs2, double feedback1, double feedback2) {
    int size = inputData.size();
    QVector<double> echoData(size, 0.0);

    // Convert delays from milliseconds to samples based on the sample rate
    int delaySamples1 = static_cast<int>((delayMs1 / 1000.0) * m_sampleRate);
    int delaySamples2 = static_cast<int>((delayMs2 / 1000.0) * m_sampleRate);

    // Apply two delayed echoes with feedback
    for (int i = 0; i < size; ++i) {
        double delayedSample1 = (i >= delaySamples1) ? echoData[i - delaySamples1] * feedback1 : 0.0;
        double delayedSample2 = (i >= delaySamples2) ? echoData[i - delaySamples2] * feedback2 : 0.0;

        echoData[i] = inputData[i] * gainIn + delayedSample1 + delayedSample2;
    }

    // Blend echoData into inputData with gainOut to add the echo effect
    for (int i = 0; i < size; ++i) {
        inputData[i] = qBound(-1.0, inputData[i] + echoData[i] * gainOut, 1.0);
    }
}

int VocalEnhancer::getProgress() {

    return progressValue * 100;

}

QString VocalEnhancer::getBanner() {

    return banner;

}

VocalEnhancer::~VocalEnhancer() {
    
    fftw_cleanup(); // Clean up any leftover resources managed by FFTW

}

static inline double princArg(double phase) {
    phase = std::fmod(phase + M_PI, 2.0 * M_PI);
    if (phase < 0) phase += 2.0 * M_PI;
    return phase - M_PI;
}


QVector<double> VocalEnhancer::createHannWindow(int size) const {
    QVector<double> w(size);
    for (int i = 0; i < size; ++i) {
        w[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (size - 1)));
    }
    return w;
}

QVector<double> VocalEnhancer::timeStretchPhaseVocoder(const QVector<double>& in, double stretch) {
    if (in.isEmpty() || stretch <= 0.0) return in;

    const int N = 2048;
    const int Ha = N / 4;
    const int Hs = std::max(1, int(std::round(Ha * stretch)));

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
        omega[k] = 2.0 * M_PI * k / N;

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
            delta = princArg(delta);

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

    out.resize(std::min(int(out.size()), outPos + N));
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

    // 1) time-stretch by 1/ratio (slower if pitch up)
    QVector<double> stretched = timeStretchPhaseVocoder(in, 1.0 / ratio);

    // 2) resample back to original length (simple high quality cubic)
    const int outN = in.size();
    QVector<double> out(outN, 0.0);

    const double step = double(stretched.size() - 1) / double(outN - 1);
    for (int i = 0; i < outN; ++i) {
        double t = i * step;
        int idx = int(t);
        double frac = t - idx;

        // cubic interpolation needs idx-1..idx+2
        int i0 = std::max(0, idx - 1);
        int i1 = std::max(0, idx);
        int i2 = std::min(int(stretched.size()) - 1, idx + 1);
        int i3 = std::min(int(stretched.size()) - 1, idx + 2);

        double v0 = stretched[i0];
        double v1 = stretched[i1];
        double v2 = stretched[i2];
        double v3 = stretched[i3];

        // same cubic as you already have
        double a0 = v3 - v2 - v0 + v1;
        double a1 = v0 - v1 - a0;
        double a2 = v2 - v0;
        double y  = (a0 * frac * frac * frac + a1 * frac * frac + a2 * frac + v1);

        out[i] = qBound(-1.0, y, 1.0);
    }

    return out;
}
