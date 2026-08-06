#ifndef RENDERJOB_H
#define RENDERJOB_H

#include <QObject>
#include <QString>
#include <QFutureWatcher>
#include <QProcess>
#include <atomic>
#include <functional>
#include <memory>

// Owns the actual "run FFmpeg and produce the final mix" work that used to
// live inline in MainWindow::mixAndRender() — both the native
// QtConcurrent/FFmpegNative::renderVideo() path and the QProcess+ffmpeg-CLI
// fallback. MainWindow keeps all UI (progress bar, buttons, dialogs); this
// class only reports progress/result.
class RenderJob : public QObject
{
    Q_OBJECT
public:
    struct Params {
        QString tunedAudioPath;   // enhanced+mastered vocal WAV
        QString webcamPath;
        QString playbackPath;     // original karaoke video/audio
        QString rawVocalPath;     // raw vocal, for native pitch overlay
        QString outputPath;
        double  vocalVolume = 1.0;
        qint64  audioOffsetMs = 0;
        qint64  videoOffsetMs = 0;
        QString resolution;
        bool    hasWebcam = false;
        QString videoEffectChain;
        // Only used by the QProcess-fallback progress parser; the native
        // path reports fractional progress on its own.
        double  totalDurationSeconds = 0;
    };

    explicit RenderJob(QObject *parent = nullptr);
    ~RenderJob() override;

    void start(const Params &params);
    void cancel();
    bool isActive() const;
    void waitForFinished();

    // Test-only seam for startNative()'s call into FFmpegNative::renderVideo():
    // signature-compatible stand-in that RenderJob's tests substitute to
    // exercise cancellation / atomic-commit / finished-signal-shape logic
    // deterministically, without real FFmpeg or multi-minute renders. Left
    // unset (falls through to the real FFmpegNative call) in production —
    // this method exists purely for the same reason
    // SessionRepository::libraryRoot()'s WAKKAQT_LIBRARY_ROOT_OVERRIDE env
    // var does: a seam that costs nothing when untouched. Writes its output
    // to the given partial path (see sidecarPathFor()) and must honor
    // `cancelled` cooperatively, same contract as FFmpegNative::renderVideo().
    using Engine = std::function<bool(const Params &params,
                                       const QString &partialOutputPath,
                                       const std::function<void(double)> &progressCb,
                                       const std::atomic<bool> *cancelled)>;
    void setEngineForTesting(Engine engine) { m_testEngine = std::move(engine); }

signals:
    void progress(double fraction); // 0..1
    void finished(bool success, bool cancelled, QString errorMessage);

private:
    void startNative(const Params &params);
    void startFallback(const Params &params);

    std::shared_ptr<std::atomic<bool>> m_cancelled;
    QFutureWatcher<bool> *m_watcher = nullptr;
    QProcess *m_process = nullptr;
    QString m_lastOutputPath;
    Engine m_testEngine;
};

#endif // RENDERJOB_H
