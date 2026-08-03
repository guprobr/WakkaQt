#ifndef PREVIEWJOB_H
#define PREVIEWJOB_H

#include "vocalenhancer.h"

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QAudioFormat>
#include <QFutureWatcher>
#include <QProcess>
#include <QScopedPointer>
#include <atomic>

// Owns the vocal-extraction + VocalEnhancer DSP work that used to live
// inline in PreviewDialog::setAudioFile()/startEnhancementJob(). PreviewDialog
// keeps everything UI-specific (progress bar, banner text, sliders, playback);
// this class only extracts/enhances audio and reports results.
class PreviewJob : public QObject
{
    Q_OBJECT
public:
    struct ExtractParams {
        QString sourceFile;
        QString destTempFile; // scratch WAV path, caller-owned (e.g. tunedRecorded)
        qint64  trimOffsetMs = 0;
    };
    struct EnhanceParams {
        double  pitchCorrectionAmount = 0.45;
        double  noiseReductionAmount = 0.35;
        double  retuneSpeedMs = 300.0;
        bool    formantPreservation = true;
        double  reverbRoomSize = 0.5;
        double  reverbDecay = 0.5;
        double  reverbMix = 0.0;
        QString scalePreset = "chromatic";
        int     keyNote = 0;
    };

    explicit PreviewJob(QObject *parent = nullptr);
    ~PreviewJob() override;

    void extract(const ExtractParams &params);

    // Returns false without starting anything if an enhance() is already
    // running — caller decides whether to remember and retry.
    bool enhance(const QByteArray &pcmData, const QAudioFormat &format,
                 const EnhanceParams &params);
    void cancelEnhance();
    bool isEnhancing() const;
    void waitForIdle(); // blocks until any in-flight extract()/enhance() finishes

    // Exposed so PreviewDialog's existing progress-polling timer can keep
    // reading getProgress()/getBanner() directly, unchanged.
    VocalEnhancer *enhancer() const { return m_enhancer.data(); }

signals:
    void extracted(QByteArray pcmSamples, QAudioFormat format);
    void extractionFailed(QString reason);
    void enhanced(QByteArray tunedPcm);

private:
    void onExtractionFinished(bool ok, const QString &destTempFile);

    QScopedPointer<VocalEnhancer> m_enhancer;
    QAudioFormat m_enhancerFormat;
    bool m_hasEnhancerFormat = false;

    QFutureWatcher<bool> *m_extractWatcher = nullptr; // native path
    QProcess *m_extractProcess = nullptr;              // QProcess fallback path
    std::atomic<bool> m_extractCancelled{false};
    QFutureWatcher<QByteArray> *m_enhanceWatcher = nullptr;
    std::atomic<bool> m_enhanceCancelled{false};
};

#endif // PREVIEWJOB_H
