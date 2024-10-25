#include "vocalenhancer.h"
#include <QDebug>
#include <cmath>
#include <complex>

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



VocalEnhancer::VocalEnhancer(const QAudioFormat& format)
    : m_sampleSize(format.bytesPerSample()),
      m_sampleRate(format.sampleRate()),
      m_numSamples(format.sampleRate() * calculateDuration(format.sampleRate()) / 1000),
      m_fftIn(m_numSamples),
      m_fftOut(m_numSamples)
{
    m_fftPlan = fftw_plan_dft_1d(m_numSamples, reinterpret_cast<fftw_complex*>(m_fftIn.data()),
                                  reinterpret_cast<fftw_complex*>(m_fftOut.data()), FFTW_FORWARD, FFTW_ESTIMATE);
    m_ifftPlan = fftw_plan_dft_1d(m_numSamples, reinterpret_cast<fftw_complex*>(m_fftOut.data()),
                                   reinterpret_cast<fftw_complex*>(m_fftIn.data()), FFTW_BACKWARD, FFTW_ESTIMATE);
}

QByteArray VocalEnhancer::enhance(const QByteArray& input) {
    qDebug() << "VocalEnhancer Input Data Size:" << input.size();
    
    if (input.isEmpty()) {
        qDebug() << "VocalEnhancer Input data is empty, returning empty output.";
        return QByteArray();
    }

    int m_numSamples = input.size() / m_sampleSize; 
    qDebug() << "Calculated m_numSamples:" << m_numSamples;

    QByteArray output(m_numSamples * m_sampleSize, 0);
    
     // Convert QByteArray to QVector<double>
    QVector<double> inputData(m_numSamples);
    for (int i = 0; i < m_numSamples; ++i) {
        // Ensure we're within bounds
        Q_ASSERT(i * m_sampleSize < input.size());
        
        // Read the sample and normalize based on the actual format
        if (m_sampleSize == 2) { // 16-bit
            inputData[i] = static_cast<double>(reinterpret_cast<const int16_t*>(input.constData())[i]) / 32768.0; 
        } else if (m_sampleSize == 3) { // 24-bit
            // Handle 24-bit audio
            const uint8_t* sample = reinterpret_cast<const uint8_t*>(input.constData() + i * 3);
            int32_t sampleValue = (sample[2] << 16) | (sample[1] << 8) | sample[0]; // Combine 3 bytes into a single int32_t
            inputData[i] = static_cast<double>(sampleValue) / (1 << 23); // Normalize for 24-bit
        } else if (m_sampleSize == 4) { // 32-bit
            inputData[i] = static_cast<double>(reinterpret_cast<const int32_t*>(input.constData())[i]) / 2147483648.0; 
        } else {
            qWarning() << "Unsupported sample size:" << m_sampleSize;
            return QByteArray(); // Return empty on unsupported format
        }
    }

    // Process pitch correction
    processPitchCorrection(inputData);

    for (const auto& value : inputData) {
        Q_ASSERT(!qIsNaN(value)); // Assert that the value is not NaN
    }

    // Normalize the output data
    double maxAmplitude = 0.0;
    for (const auto& value : inputData) {
        maxAmplitude = std::max(maxAmplitude, std::abs(value));
    }

    // Prevent division by zero
    if (maxAmplitude > 0) {
        double normalizationFactor = 1.0 / maxAmplitude; // Normalization factor
        for (auto& value : inputData) {
            value *= normalizationFactor; // Normalize
        }
    }

    // Apply fixed gain of normalized data
    double gain = 1.0;
    for (auto& value : inputData) {
        value *= gain; // Adjust volume with gain
        value = qBound(-1.0, value, 1.0); // Clamp
    }

    // Convert outputData back to QByteArray
    for (int i = 0; i < inputData.size(); ++i) {
        int sampleValue = 0;
        if (m_sampleSize == 2) { // 16-bit
            sampleValue = static_cast<int>(inputData[i] * 32767); // Denormalize
            sampleValue = qBound(-32768, sampleValue, 32767); // Clamp for 16-bit
            reinterpret_cast<int16_t*>(output.data())[i] = static_cast<int16_t>(sampleValue); // Write to output
        } else if (m_sampleSize == 3) { // 24-bit
            sampleValue = static_cast<int>(inputData[i] * (1 << 23)); // Denormalize
            sampleValue = qBound(-8388608, sampleValue, 8388607); // Clamp for 24-bit
            uint8_t* sample = reinterpret_cast<uint8_t*>(output.data() + i * 3);
            sample[0] = static_cast<uint8_t>(sampleValue & 0xFF);
            sample[1] = static_cast<uint8_t>((sampleValue >> 8) & 0xFF);
            sample[2] = static_cast<uint8_t>((sampleValue >> 16) & 0xFF);
        } else if (m_sampleSize == 4) { // 32-bit
            sampleValue = static_cast<int>(inputData[i] * 2147483647); // Denormalize
            sampleValue = qBound(-2147483648, sampleValue, 2147483647); // Clamp for 32-bit
            reinterpret_cast<int32_t*>(output.data())[i] = static_cast<int32_t>(sampleValue); // Write to output
        }
    }

    return output;
}

void VocalEnhancer::processPitchCorrection(QVector<double>& inputData) {
    // Step 1: (pitch detection)
    double detectedPitch = detectPitch(inputData);
    
    if (detectedPitch <= 0) {
        detectedPitch = A440; // Default to A4 if no pitch is detected
    }

    // Step 2: (quantize detected pitch to the closest note)
    double targetFrequency = findClosestNoteFrequency(detectedPitch);
    
    // Step 3: (Calculate pitch shifting ratio)
    double pitchShiftRatio = targetFrequency / detectedPitch;
    
    // Step 4: Harmonic scaling
    QVector<double> scaledData = harmonicScale(inputData, 0.99);

    // Step 5: Replace data with pitch-shifted data
    inputData = scaledData;

    // Step 6: Apply any post-processing
    compressDynamics(inputData, 0.3, 1.696);
    harmonicExciter(inputData, 1.15, 0.5);
    
}

QVector<double> VocalEnhancer::harmonicScale(const QVector<double>& inputData, double scaleFactor) {
    QVector<double> outputData(inputData.size(), 0.0);  // Initialize output with zeros

    // Define window size and hop size
    int windowSize = 1024; // Typical size for harmonic scaling, can be adjusted
    int hopSize = windowSize / 4; // 25% overlap

    // Hann window function
    QVector<double> window(windowSize);
    for (int i = 0; i < windowSize; ++i) {
        window[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (windowSize - 1)));
    }

    // Time-stretch using harmonic scaling
    int numFrames = (inputData.size() + hopSize - 1) / hopSize; // Ensure we cover all frames
    for (int frame = 0; frame < numFrames; ++frame) {
        int start = frame * hopSize;
        QVector<double> segment(windowSize);

        // Apply window and extract segment
        for (int i = 0; i < windowSize; ++i) {
            int idx = start + i;
            segment[i] = (idx < inputData.size()) ? inputData[idx] * window[i] : 0.0;
        }

        // Modify segment by applying harmonic scaling
        QVector<double> scaledSegment = timeStretch(segment, scaleFactor);

        // Add the scaled segment back to the output using overlap-add
        int outputStart = start; 
        for (int i = 0; i < scaledSegment.size(); ++i) {
            int idx = outputStart + i;
            if (idx < outputData.size()) {
                outputData[idx] += scaledSegment[i] * window[i]; // with windowing
            }
        }
    }

    return outputData;
}

double VocalEnhancer::cubicInterpolate(double v0, double v1, double v2, double v3, double t) {
    double a0, a1, a2, a3;
    a0 = v3 - v2 - v0 + v1;
    a1 = v0 - v1 - a0;
    a2 = v2 - v0;
    a3 = v1;

    return (a0 * t * t * t + a1 * t * t + a2 * t + a3);
}

QVector<double> VocalEnhancer::timeStretch(const QVector<double>& segment, double shiftRatio) {
    int newSize = static_cast<int>(segment.size() * shiftRatio);
    QVector<double> stretchedSegment(newSize);

    for (int i = 0; i < newSize; ++i) {
        double idx = static_cast<double>(i) / shiftRatio;
        int index1 = static_cast<int>(idx);
        int index0 = std::max(0, index1 - 1);
        int index2 = std::min(index1 + 1, static_cast<int>(segment.size() - 1));
        int index3 = std::min(index1 + 2, static_cast<int>(segment.size() - 1));

        double t = idx - index1;
        stretchedSegment[i] = cubicInterpolate(segment[index0], segment[index1], segment[index2], segment[index3], t);
    }

    return stretchedSegment;
}

double VocalEnhancer::detectPitch(QVector<double>& inputData) {
    const int sampleSize = inputData.size();
    if (sampleSize == 0) {
        return 0.0;  // Return 0 for empty input
    }

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

    // Analyze frequency magnitudes
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

    qDebug() << "VocalEnhancer detected frequency: " << detectedFrequency << " Hz";
    return detectedFrequency;
}

double VocalEnhancer::findClosestNoteFrequency(double frequency) {
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

double VocalEnhancer::calculateDuration(int sampleRate) {
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



