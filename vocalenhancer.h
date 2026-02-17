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
    int getProgress() const;
    QString getBanner() const;

private:
    // ========= Audio format (Qt6) =========
    int m_sampleRate = 0;
    int m_channels = 1;
    int m_bytesPerSample = 2;
    bool m_isFloat = false;
    bool m_isSignedInt = true;
    int m_frameBytes = 0;

    // Analysis window
    int m_blockSizeMs = 40;
    int m_numSamples = 0;

    // UI / state
    double progressValue = 0.0;
    mutable QString banner;

    // PCM conversion
    QVector<double> convertToDoubleArray(const QByteArray& input);
    void convertToQByteArray(const QVector<double>& inputData, QByteArray& output);

    static inline int32_t readInt24LE(uint8_t b0, uint8_t b1, uint8_t b2);
    static inline void writeInt24LE(uint8_t* dst, int32_t value);

    // Pitch
    double detectPitch(const QVector<double>& data) const;
    double findClosestNoteFrequency(double frequency) const;

    // Pitch correction
    void processPitchCorrection(QVector<double>& data);

    // Windowing
    QVector<double> createHannWindow(int size) const;
    double cubicInterpolate(double v0, double v1, double v2, double v3, double t) const;

    // Phase vocoder
    QVector<double> timeStretchPhaseVocoder(const QVector<double>& in, double stretch);
    QVector<double> pitchShiftPhaseVocoder(const QVector<double>& in, double ratio);

    // Dynamics / timbre
    void applyMakeupGain(QVector<double>& data, double gain);
    void applyLimiter(QVector<double>& data, double ceiling);
    void compressDynamics(QVector<double>& data, double threshold, double ratio);
    void harmonicExciter(QVector<double>& data, double drive, double mix);

    // Echo
    void applyEcho(QVector<double>& data,
                   double gainIn,
                   double gainOut,
                   double delayMs1,
                   double delayMs2,
                   double feedback1,
                   double feedback2);
};

#endif // VOCALENHANCER_H