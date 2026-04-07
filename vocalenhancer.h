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

    void setPitchCorrectionAmount(double amount);
    double getPitchCorrectionAmount() const;

    void setNoiseReductionAmount(double amount);
    double getNoiseReductionAmount() const;

    // Scale / key-aware correction
    void    setScale(int keyNote, const QVector<int>& intervals);
    void    setScalePreset(const QString& name, int keyNote = 0);
    int     getKeyNote()   const { return m_keyNote; }
    QString getScaleName() const { return m_scaleName; }

    // Retune speed: 0 ms = instant/robotic, 300 ms ≈ current natural default
    void   setRetuneSpeed(double ms);
    double getRetuneSpeed() const { return m_retuneSpeedMs; }

    // Formant preservation via LPC (true = preserve vocal timbre during pitch shift)
    void setFormantPreservation(bool enabled) { m_formantPreservation = enabled; }
    bool getFormantPreservation() const       { return m_formantPreservation; }

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
    QVector<double> m_pvPrevPhase;
    QVector<double> m_pvSumPhase;

    // UI / state
    double progressValue = 0.0;
    mutable QString banner;

    // User-tweakable behaviour (0.0 .. 1.0)
    double m_pitchCorrectionAmount = 0.45;
    double m_noiseReductionAmount  = 0.35;

    // ── Scale / key ────────────────────────────────────────────────────────
    int          m_keyNote        = 0;                               // 0=C … 11=B
    QVector<int> m_scaleIntervals = {0,1,2,3,4,5,6,7,8,9,10,11};   // default: chromatic
    QString      m_scaleName      = "Chromatic";

    // ── Retune speed ───────────────────────────────────────────────────────
    double m_retuneSpeedMs = 300.0;    // 0=instant, 300≈current natural default

    // ── Formant preservation ───────────────────────────────────────────────
    bool m_formantPreservation = true;

    // ── Vibrato EMA state ──────────────────────────────────────────────────
    double m_emaSmoothedPitch = 0.0;   // reset in resetPVState()

    // PCM conversion
    QVector<double> convertToDoubleArray(const QByteArray& input);
    void convertToQByteArray(const QVector<double>& inputData, QByteArray& output);

    static inline int32_t readInt24LE(uint8_t b0, uint8_t b1, uint8_t b2);
    static inline void writeInt24LE(uint8_t* dst, int32_t value);

    // Pitch detection
    double detectPitch(const QVector<double>& data) const;        // existing HPS
    double detectPitchYIN(const QVector<double>& data) const;     // new YIN
    double detectPitchBest(const QVector<double>& data) const;    // YIN→HPS fallback
    double computeAutocorrConfidence(const QVector<double>& data, double pitchHz) const;

    double findClosestNoteFrequency(double frequency) const;

    // Pitch correction
    double correctPitchChunk(QVector<double>& chunk, double prevRatio,
                             double pitchHzHint = 0.0);
    void processPitchCorrection(QVector<double>& data);

    // Windowing / interpolation
    QVector<double> createHannWindow(int size) const;
    double cubicInterpolate(double v0, double v1, double v2, double v3, double t) const;

    // LPC formant preservation
    QVector<double> computeLPCCoeffs(const QVector<double>& frame, int order) const;
    QVector<double> applyLPCInverseFilter(const QVector<double>& x,
                                          const QVector<double>& a) const;
    QVector<double> applyLPCSynthesisFilter(const QVector<double>& residual,
                                             const QVector<double>& a) const;

    // Phase vocoder
    void            resetPVState();
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

    // Noise reduction
    void reduceNoiseSpectralGate(QVector<double>& x,
                    int    fftSize       = 1024,
                    int    hopSize       = 256,
                    double overSub       = 0.65,
                    double floorDb       = -10.0,
                    double noiseLearnSec = 0.40,
                    double adaptivity    = 0.03,
                    double lowEnergyDb   = -45.0
                    );

    inline double dbToLinear(double db) const {
        return std::pow(10.0, db / 20.0);
    }
};

#endif // VOCALENHANCER_H
