#ifndef VOCALENHANCER_H
#define VOCALENHANCER_H

#include <QObject>
#include <QAudioFormat>
#include <QByteArray>
#include <QString>
#include <QVector>

class VocalEnhancer : public QObject
{
    Q_OBJECT

public:
    explicit VocalEnhancer(const QAudioFormat& format, QObject *parent = nullptr);
    ~VocalEnhancer();

    QByteArray enhance(const QByteArray& input);
    int getProgress();
    QString getBanner();

private:
    // =======================
    // Constants
    // =======================
    static const double A440;
    static const int MIDI_NOTES;
    static const double NOTE_FREQUENCIES[128];

    // =======================
    // Audio format
    // =======================
    int m_sampleSize;
    int m_sampleRate;
    int m_numSamples;

    // =======================
    // UI / State
    // =======================
    double progressValue = 0.0;
    mutable QString banner;

    // =======================
    // Core pipeline
    // =======================
    void processPitchCorrection(QVector<double>& data);
    QVector<double> timeStretch(const QVector<double>& segment, double shiftRatio) ;
    QVector<double> harmonicScale(const QVector<double>& data, double scaleFactor);
    QVector<double> extractSegment(const QVector<double>& data, const QVector<double>& window, int start, int size) const;
    void addScaledSegment(QVector<double>& outputData, const QVector<double>& scaledSegment, int start, const QVector<double>& window) const;
    

    // =======================
    // Pitch detection (offline, robust)
    // =======================
    double detectPitch(const QVector<double>& data) const;
    double findClosestNoteFrequency(double pitch) const;

    // =======================
    // Phase Vocoder (quality pitch shift)
    // =======================
    QVector<double> timeStretchPhaseVocoder(const QVector<double>& in, double stretch);
    QVector<double> pitchShiftPhaseVocoder(const QVector<double>& in, double ratio);

    // =======================
    // Window / math helpers
    // =======================
    QVector<double> createHannWindow(int size) const;
    double cubicInterpolate(double v0, double v1, double v2, double v3, double t) const;

    // =======================
    // Dynamics / timbre
    // =======================
    void normalizeAndApplyGain(QVector<double>& data, double gain);
    void normalizeRMS(QVector<double>& x, double targetRMS);
    void compressDynamics(QVector<double>& data, double threshold, double ratio);
    void harmonicExciter(QVector<double>& data, double drive, double mix);
    void applyLimiter(QVector<double>& data, double ceiling);
    void applyMakeupGain(QVector<double>& x, double gain); 

    // =======================
    // Time effects
    // =======================
    void applyEcho(
        QVector<double>& data,
        double gainIn,
        double gainOut,
        double delayMs1,
        double delayMs2,
        double feedback1,
        double feedback2
    );

    // =======================
    // PCM conversion
    // =======================
    QVector<double> convertToDoubleArray(const QByteArray& input, int sampleCount);
    void convertToQByteArray(const QVector<double>& inputData, QByteArray& output);

    double normalizeSample(const QByteArray& input, int index) const;
    int denormalizeSample(double value) const;

    // =======================
    // Utils
    // =======================
    double calculateDuration(int sampleRate) const;
};

#endif // VOCALENHANCER_H
