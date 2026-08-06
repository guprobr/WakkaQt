#ifndef VOCALSEPARATIONJOB_H
#define VOCALSEPARATIONJOB_H

#include <QObject>
#include <QString>
#include <QFutureWatcher>
#include <QProcess>
#include <QPair>
#include <atomic>
#include <functional>
#include <memory>

// Owns the background work behind "Generate Backing Track": running the
// UVR-MDX-NET vocal-separation model on a source file, then muxing/
// transcoding the separated instrumental into its final destination.
// MainWindow keeps everything UI-specific (the model-download prompt,
// progress dialogs, the save-file dialog); this class only runs the two
// async stages and reports results — same split as RenderJob/PreviewJob.
class VocalSeparationJob : public QObject
{
    Q_OBJECT
public:
    explicit VocalSeparationJob(QObject *parent = nullptr);
    ~VocalSeparationJob() override;

    struct ExportParams {
        QString tempWavPath;    // separated instrumental, from a separated() signal
        QString inputFile;      // original source (for video muxing)
        QString savePath;       // final destination
        bool    saveAsVideo = false;
    };

    // Phase 1: runs VocalSeparator::separate() on a worker thread, writing
    // into a private per-run workspace directory this job creates and owns
    // (see discardWorkspace()) instead of fixed shared /tmp paths.
    void separate(const QString &inputFile);
    void cancelSeparate();
    bool isSeparating() const;

    // Phase 2: mux/transcode the separated WAV into its final destination.
    void exportResult(const ExportParams &params);
    // Cancels an in-flight exportResult(): the native path's token is
    // checked cooperatively inside FFmpegNative::muxVideoWithAudio()/
    // transcodeAudio(); the QProcess fallback path is killed directly, since
    // the plain ffmpeg CLI has no way to be asked to cancel itself.
    void cancelExport();
    bool isExporting() const;

    bool isActive() const;   // separating OR exporting
    void waitForFinished();  // blocks until both phases are idle

    // Removes the private workspace directory (if any) and its contents —
    // call when the separated result is no longer needed (e.g. the user
    // cancelled the save-destination dialog) instead of deleting individual
    // files. Safe to call when no workspace is active. Not called
    // automatically on export failure: exportFailed()'s error message
    // preserves the workspace path on purpose so a failed mux/encode never
    // costs the user having to re-run the (expensive) separation.
    void discardWorkspace();

    // Test-only seams, same reasoning as RenderJob::setEngineForTesting():
    // unset in production (falls through to the real VocalSeparator::separate()
    // / FFmpegNative::muxVideoWithAudio()/transcodeAudio() calls), substituted
    // by this job's own tests to exercise cancellation/atomic-commit/
    // signal-shape logic without a real ONNX model or real FFmpeg encode.
    using SeparateEngine = std::function<QString(const QString &inputFile,
                                                  const QString &workspaceDir,
                                                  const std::function<void(int)> &progressFn,
                                                  QString &errorOut,
                                                  const std::atomic<bool> *cancelled)>;
    void setSeparateEngineForTesting(SeparateEngine engine) { m_testSeparateEngine = std::move(engine); }

    // Mirrors exportNative()'s branch on saveAsVideo: the real
    // implementation calls FFmpegNative::muxVideoWithAudio() when true,
    // transcodeAudio() when false — this seam stands in for whichever one
    // would have run. Writes its output to partialOutputPath and must honor
    // `cancelled` cooperatively, same contract as those two functions.
    using ExportEngine = std::function<bool(const QString &inputFile,
                                             const QString &tempWavPath,
                                             const QString &partialOutputPath,
                                             bool saveAsVideo,
                                             const std::function<void(int)> &progressCb,
                                             const std::atomic<bool> *cancelled)>;
    void setExportEngineForTesting(ExportEngine engine) { m_testExportEngine = std::move(engine); }

signals:
    void separationProgress(int percentage);
    void separated(QString tempWavPath);
    // wasCancelled distinguishes a user Abort from a real failure — mirrors
    // RenderJob::finished()'s (success, cancelled, errorMessage) shape.
    void separationFailed(QString error, bool wasCancelled);

    void exportProgress(int percentage); // -1 = indeterminate (QProcess fallback has no sub-progress)
    void exported(QString destinationPath);
    void exportFailed(QString error);

private:
    using SeparateResult = QPair<QString, QString>; // {tempPath, error}
    using ExportResult   = QPair<bool, QString>;     // {ok, error}

    // exportNative() is only actually defined behind #ifdef
    // WAKKAQT_FFMPEG_NATIVE in vocalseparationjob.cpp (mirroring RenderJob's
    // startNative()/startFallback() split) — exportResult() picks between
    // the two at compile time.
    void exportNative(const ExportParams &params);
    void exportFallback(const ExportParams &params);

    std::shared_ptr<std::atomic<bool>> m_cancelled;
    QFutureWatcher<SeparateResult> *m_separateWatcher = nullptr;

    std::shared_ptr<std::atomic<bool>> m_exportCancelled;
    QFutureWatcher<ExportResult> *m_exportWatcher = nullptr;
    // Owned only on the QProcess fallback path (no WAKKAQT_FFMPEG_NATIVE):
    // set right before mux/mp3 QProcess::start() so cancelExport() can
    // kill() it directly instead of waiting out its up-to-300s timeout.
    QProcess *m_exportProcess = nullptr;

    // Set by separate(); consumed by VocalSeparator::separate() as its
    // scratch directory. Removed by discardWorkspace() — called
    // automatically after a successful exportResult(), or explicitly by the
    // caller when the separated result is abandoned before exporting.
    QString m_workspaceDir;

    SeparateEngine m_testSeparateEngine;
    ExportEngine   m_testExportEngine;
};

#endif // VOCALSEPARATIONJOB_H
