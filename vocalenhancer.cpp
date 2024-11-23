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
    int targetSize = inputData.size();
    //normalizeAndApplyGain(inputData, 0.75); // sanitize for pich correction and effects
    qWarning() << "VocalEnhancer processing pitch correction";
    processPitchCorrection(inputData);
    //normalizeAndApplyGain(inputData, 0.84); // normalize again at the very end
    resizeOutputToMatchInput(inputData, targetSize); // fix sync issues 
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

void VocalEnhancer::processPitchCorrection(QVector<double>& data) {
    double detectedPitch = detectPitch(data);
    double targetFrequency = findClosestNoteFrequency(detectedPitch <= 0 ? A440 : detectedPitch);
    double pitchShiftRatio = detectedPitch > 0 ? targetFrequency / detectedPitch : targetFrequency / A440;

    // A large upward pitch shift followed by a downward shift!
    // significant pitch correction while mitigating the formant shift issue.
    double bigShift = 0.5;
    QVector<double> scaleUp = harmonicScale(data, bigShift ); // pitch way high, for strong pitch correction
    banner = QString("2nd Harmonic-scale pass: scale down");
    QVector<double> scaleDown = harmonicScale(scaleUp, 1.0 / bigShift ); // pitch down, back to Kansas
    banner = QString("Final Harmonic-scale pass: pitch shift");
    QVector<double> scale = harmonicScale(scaleDown, pitchShiftRatio );
    data = scale;
    
    compressDynamics(data, 1.5, 0.5);
    harmonicExciter(data, 1.8, 0.4);
    applyEcho(data, 0.8, 0.7, 110, 255, 0.25, 0.15);

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
    int windowSize = 24576;
    int hopSize = windowSize / 3;
    QVector<double> outputData(data.size(), 0.0);

    QVector<double> window = createHannWindow(windowSize);
    int numFrames = (data.size() + hopSize - 1) / hopSize;

    for (int frame = 0; frame < numFrames; ++frame) {
        int start = frame * hopSize;
        QVector<double> segment = extractSegment(data, window, start, windowSize);
        QVector<double> scaledSegment = timeStretch(segment, scaleFactor);

        // Ensure scaledSegment fits within data size on adding
        if (start + scaledSegment.size() <= outputData.size()) {
            addScaledSegment(outputData, scaledSegment, start, window);
        }
        
        progressValue = static_cast<double>(frame) / numFrames;
        
    }
    return outputData;
}


QVector<double> VocalEnhancer::createHannWindow(int size) const {
    QVector<double> window(size);
    for (int i = 0; i < size; ++i) {
        window[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (size - 1)));
    }
    return window;
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

void VocalEnhancer::compressDynamics(QVector<double>& inputData, double threshold, double ratio) {
    for (auto& sample : inputData) {
        if (sample > threshold) {
            sample = threshold + (sample - threshold) / ratio; // Compress above threshold
        }
    }
}

void VocalEnhancer::harmonicExciter(QVector<double>& inputData, double gain, double threshold) {
    for (int i = 0; i < inputData.size(); ++i) {
        // Apply a simple saturation effect
        double sample = inputData[i];

        // Generate harmonics using a simple distortion technique
        if (std::abs(sample) > threshold) {
            sample = threshold + (sample - threshold) * gain; // Apply gain above the threshold
            sample = std::min(sample, 1.0); // Clipping to prevent overflow
            sample = std::max(sample, -1.0);
        }

        // Blend the original and processed signal to create the harmonic enhancement
        inputData[i] += sample * 0.3; // Blend in 30% of the harmonic signal
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


double VocalEnhancer::detectPitch(const QVector<double>& inputData) const {
    const int sampleSize = inputData.size();
    if (sampleSize == 0) {
        return 0.0;  // Return 0 for empty input
    }
    
    banner = QString("Analyze frequency magnitudes");

    // Allocate memory for FFTW input and output
    fftw_complex* fftwOutput = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * (sampleSize / 2 + 1));
    double* fftwInput = (double*) fftw_malloc(sizeof(double) * sampleSize);

    // Copy input data to FFTW input array
    for (int i = 0; i < sampleSize; ++i) {
        fftwInput[i] = inputData[i];
    }

    // Create FFTW plan for forward FFT
    fftw_plan plan = fftw_plan_dft_r2c_1d(sampleSize, fftwInput, fftwOutput, FFTW_ESTIMATE);

    // Execute FFT
    fftw_execute(plan);

    // Variables for finding the maximum magnitude
    double maxMagnitude = 0.0;
    int maxIndex = 0;

    for (int i = 0; i < (sampleSize / 2 + 1); ++i) {
        double magnitude = std::sqrt(fftwOutput[i][0] * fftwOutput[i][0] + fftwOutput[i][1] * fftwOutput[i][1]); // Calculate magnitude
        if (magnitude > maxMagnitude) {
            maxMagnitude = magnitude;
            maxIndex = i; // Store the index of max magnitude
        }
    }

    // Calculate the frequency corresponding to max index
    double sampleRate = m_sampleRate;
    double detectedFrequency = (maxIndex * sampleRate) / sampleSize;

    // Clean up FFTW resources
    fftw_destroy_plan(plan);
    fftw_free(fftwOutput);
    fftw_free(fftwInput);

    qWarning() << "VocalEnhancer detected frequency: " << detectedFrequency << " Hz";
    banner = "Detected: " + QString::number(detectedFrequency) + " Hz\n1st harmonic-scale pass: scale up";
    return detectedFrequency;
}

void VocalEnhancer::resizeOutputToMatchInput(QVector<double>& outputData, int targetSize) {
    int currentSize = outputData.size();
    QVector<double> resizedOutput(targetSize, 0.0);

    if (currentSize == targetSize) {
        resizedOutput = outputData;
    } else if (currentSize < targetSize) {
        // Stretch output to match target size with interpolation
        for (int i = 0; i < targetSize; ++i) {
            double scale = static_cast<double>(i) * currentSize / targetSize;
            int index = static_cast<int>(scale);
            if (index < currentSize - 1) {
                double fraction = scale - index;
                resizedOutput[i] = outputData[index] * (1.0 - fraction) + outputData[index + 1] * fraction;
            } else {
                resizedOutput[i] = outputData[index];
            }
        }
    } else {
        // Shrink output to match target size with nearest-neighbor
        for (int i = 0; i < targetSize; ++i) {
            int index = static_cast<int>(i * currentSize / targetSize);
            if (index < currentSize) {
                resizedOutput[i] = outputData[index];
            }
        }
    }

    outputData = resizedOutput;
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