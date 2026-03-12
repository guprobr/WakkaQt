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
    // Plans are created once in the constructor with FFTW_MEASURE and reused
    // across all chunks, avoiding repeated allocation on long recordings.

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

    // ── Persistent PV phase state ─────────────────────────────────────────
    // Kept as class members so phase accumulation survives across chunk calls.
    // Reset explicitly via resetPVState() before each new recording.
    QVector<double> m_pvPrevPhase;   // analysis phase from previous frame
    QVector<double> m_pvSumPhase;    // accumulated synthesis phase

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

    // Windowing / interpolation
    QVector<double> createHannWindow(int size) const;
    double cubicInterpolate(double v0, double v1, double v2, double v3, double t) const;

    // Phase vocoder
    void            resetPVState();   // call before each new recording
    QVector<double> pitchShiftContinuous(const QVector<double>& in, const QVector<double>& frameRatio);
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

    // Noise reduction — defaults match the enhance() call site in vocalenhancer.cpp
    void reduceNoiseSpectralGate(QVector<double>& x,
                    int    fftSize       = 1024,
                    int    hopSize       = 256,   // N/4
                    double overSub       = 0.65,  // gentle subtraction, keeps breath
                    double floorDb       = -10.0, // higher bed = more natural
                    double noiseLearnSec = 0.40,  // ~400 ms initial noise learn
                    double adaptivity    = 0.03,  // slow adaptation, less "twinkling"
                    double lowEnergyDb   = -45.0  // dBFS gate for noise model update
                    );

    inline double dbToLinear(double db) const {
        return std::pow(10.0, db / 20.0);
    }
};

#endif // VOCALENHANCER_H
// appended — will be merged manually