#ifndef VOCALSEPARATIONJOB_H
#define VOCALSEPARATIONJOB_H

#include <QObject>
#include <QString>
#include <QFutureWatcher>
#include <QPair>
#include <atomic>
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

    // Phase 1: runs VocalSeparator::separate() on a worker thread.
    void separate(const QString &inputFile);
    void cancelSeparate();
    bool isSeparating() const;

    // Phase 2: mux/transcode the separated WAV into its final destination.
    // No cancellation — matches the existing UX (the caller's save-progress
    // dialog already disables its close button during this stage).
    void exportResult(const ExportParams &params);
    bool isExporting() const;

    bool isActive() const;   // separating OR exporting
    void waitForFinished();  // blocks until both phases are idle

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

    std::shared_ptr<std::atomic<bool>> m_cancelled;
    QFutureWatcher<SeparateResult> *m_separateWatcher = nullptr;
    QFutureWatcher<ExportResult>   *m_exportWatcher = nullptr;
};

#endif // VOCALSEPARATIONJOB_H
