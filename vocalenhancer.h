#ifndef VOCALENHANCER_H
#define VOCALENHANCER_H

#include <QObject>
#include <QAudioFormat>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <fftw3.h>

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

    // ── Cached FFTW plans ──────────────────────────────────────────────────
    // Phase vocoder uses N=2048 (fixed); noise gate uses N=1024 (fixed).
    // Plans are created once in the constructor and reused across all chunks,
    // avoiding repeated plan-creation overhead on long recordings.

    // PV plans (N=2048)
    static constexpr int kPvN = 2048;
    double*       m_pvIn    = nullptr;
    fftw_complex* m_pvOut   = nullptr;
    fftw_complex* m_pvSpec  = nullptr;
    double*       m_pvIfft  = nullptr;
    fftw_plan     m_pvFwd   = nullptr;
    fftw_plan     m_pvInv   = nullptr;

    // Noise-gate plans (N=1024)
    static constexpr int kNgN = 1024;
    double*       m_ngIn    = nullptr;
    fftw_complex* m_ngOut   = nullptr;
    fftw_complex* m_ngSpec  = nullptr;
    double*       m_ngIfft  = nullptr;
    fftw_plan     m_ngFwd   = nullptr;
    fftw_plan     m_ngInv   = nullptr;

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
    double correctPitchChunk(QVector<double>& chunk, double prevRatio);
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
    // Noise reduction
    void reduceNoiseSpectralGate(QVector<double>& x,
                    int fftSize = 1024,
                    int hopSize = -1,          // defaults to N/4 if < 1
                    double overSub = 1.1,      // 1.0–1.5 typical
                    double floorDb = -20.0,    // -12 .. -25 dB typical
                    double noiseLearnSec = 0.25, // 0.15–0.4 s
                    double adaptivity = 0.08,  // slow adaptation during low-energy frames
                    double lowEnergyDb = -35.0 // gate for adaptation (dBFS approx)
                    );
    inline double dbToLinear(double db) const {
        return std::pow(10.0, db / 20.0);
    }
};

#endif // VOCALENHANCER_H