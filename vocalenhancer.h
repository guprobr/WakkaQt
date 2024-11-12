#ifndef VOCALENHANCER_H
#define VOCALENHANCER_H

#include <QAudioFormat>
#include <QByteArray>
#include <QString>
#include <QVector>

class VocalEnhancer
{
public:
    explicit VocalEnhancer(const QAudioFormat& format);
    QByteArray enhance(const QByteArray& input);
    int getProgress();
    QString getBanner();

private:
    // Constants
    static const double A440;
    static const int MIDI_NOTES;
    static const double NOTE_FREQUENCIES[128];

    // Member variables
    int m_sampleSize;
    int m_sampleRate;
    int m_numSamples;

    double progressValue = 0;
    mutable QString banner;
    
    double calculateDuration(int sampleRate) const;
    QVector<double> convertToDoubleArray(const QByteArray& input, int sampleCount);
    double normalizeSample(const QByteArray& input, int index) const;
    void processPitchCorrection(QVector<double>& data);
    double detectPitch(const QVector<double>& data) const;
    double findClosestNoteFrequency(double pitch) const;
    QVector<double> harmonicScale(const QVector<double>& data, double scaleFactor);
    void resizeOutputToMatchInput(QVector<double>& outputData, int targetSize);

    void normalizeAndApplyGain(QVector<double>& data, double gain);
    void convertToQByteArray(const QVector<double>& inputData, QByteArray& output);
    int denormalizeSample(double value) const;

    // Helper methods for harmonic scaling
    QVector<double> createHannWindow(int size) const;
    QVector<double> extractSegment(const QVector<double>& data, const QVector<double>& window, int start, int size) const;
    void addScaledSegment(QVector<double>& outputData, const QVector<double>& scaledSegment, int start, const QVector<double>& window) const;
    QVector<double> timeStretch(const QVector<double>& segment, double shiftRatio);
    double cubicInterpolate(double v0, double v1, double v2, double v3, double t) const;

    // Additional processing methods
    void compressDynamics(QVector<double>& data, double threshold, double ratio);
    void harmonicExciter(QVector<double>& data, double intensity, double mix);
    void applyEcho(QVector<double>& inputData, double gainIn, double gainOut, double delayMs1, double delayMs2, double feedback1, double feedback2);
};

#endif // VOCALENHANCER_H
