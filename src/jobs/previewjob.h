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
#include <memory>

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
    // wasCancelled distinguishes a deliberate cancel (extract() replacing a
    // still-running extraction, or waitForIdle() during shutdown/dialog
    // close) from a real decode error — mirrors RenderJob::finished()'s
    // (success, cancelled, errorMessage) shape. reason is empty when cancelled.
    void extractionFailed(QString reason, bool wasCancelled);
    void enhanced(QByteArray tunedPcm);

private:
    // Fallback (QProcess) path only — the native path folds the equivalent
    // work into processExtractedFile() below, run on the QtConcurrent worker
    // thread instead of here on the GUI thread (see extract()'s native branch).
    void onExtractionFinished(bool ok, const QString &destTempFile);

    struct ExtractedAudio {
        bool ok = false;
        QByteArray samples;
        QAudioFormat format;
        QString error; // only meaningful when !ok
    };
    // Reads destTempFile, parses it as WAV, and (native builds only) runs
    // audio masterization on it — the same steps onExtractionFinished() does
    // for the fallback path, but called from inside the native extraction's
    // QtConcurrent worker lambda so this file I/O and filter-graph work runs
    // off the GUI thread. A heavy masterization chain on a long recording
    // used to freeze the UI right as extraction hit 100%, since this all
    // used to run inside the future's GUI-thread finished-callback instead.
    static ExtractedAudio processExtractedFile(const QString &destTempFile);

    QScopedPointer<VocalEnhancer> m_enhancer;
    QAudioFormat m_enhancerFormat;
    bool m_hasEnhancerFormat = false;

    QFutureWatcher<ExtractedAudio> *m_extractWatcher = nullptr; // native path
    QProcess *m_extractProcess = nullptr;              // QProcess fallback path
    // Per-run flag (fresh shared_ptr each extract() call, not reused) so a
    // still-in-flight run's finished-callback can tell whether cancellation
    // was requested for THAT run specifically, even after a later extract()
    // call has moved on to a new run of its own.
    std::shared_ptr<std::atomic<bool>> m_extractCancelled;
    QFutureWatcher<QByteArray> *m_enhanceWatcher = nullptr;
    std::atomic<bool> m_enhanceCancelled{false};
};

#endif // PREVIEWJOB_H
