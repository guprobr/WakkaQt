#include "vocalseparationjob.h"
#include "vocalseparator.h"
#include "atomicfilecommit.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

static QString partialPathFor(const QString &finalPath)
{
    return sidecarPathFor(finalPath, "partial");
}

VocalSeparationJob::VocalSeparationJob(QObject *parent) : QObject(parent) {}

VocalSeparationJob::~VocalSeparationJob()
{
    waitForFinished();
}

bool VocalSeparationJob::isSeparating() const
{
    return m_separateWatcher && !m_separateWatcher->isFinished();
}

bool VocalSeparationJob::isExporting() const
{
    return (m_exportWatcher && !m_exportWatcher->isFinished()) || (m_exportProcess != nullptr);
}

bool VocalSeparationJob::isActive() const
{
    return isSeparating() || isExporting();
}

void VocalSeparationJob::cancelSeparate()
{
    if (m_cancelled)
        m_cancelled->store(true);
}

void VocalSeparationJob::cancelExport()
{
    // Checked cooperatively by FFmpegNative::muxVideoWithAudio()/
    // transcodeAudio() on the native path.
    if (m_exportCancelled)
        m_exportCancelled->store(true);
    // The QProcess fallback path has no cooperative-cancellation hook of its
    // own (it's just the ffmpeg CLI) — kill it directly instead.
    if (m_exportProcess)
        m_exportProcess->kill();
}

void VocalSeparationJob::waitForFinished()
{
    if (m_separateWatcher && !m_separateWatcher->isFinished())
        m_separateWatcher->waitForFinished();
    if (m_exportWatcher && !m_exportWatcher->isFinished())
        m_exportWatcher->waitForFinished();
    if (m_exportProcess)
        m_exportProcess->waitForFinished();
}

void VocalSeparationJob::discardWorkspace()
{
    if (!m_workspaceDir.isEmpty()) {
        QDir(m_workspaceDir).removeRecursively();
        m_workspaceDir.clear();
    }
}

// ── separate ─────────────────────────────────────────────────────────────
void VocalSeparationJob::separate(const QString &inputFile)
{
    if (isActive())
        return; // caller gates re-entry via MainWindow's Separating state

    discardWorkspace(); // defensive: clear any leftover from a prior run on this instance

    // Private per-run scratch directory instead of the fixed shared /tmp
    // names VocalSeparator::separate() used to hardcode — two overlapping
    // separations (different WakkaQt instances, or a stray leftover from a
    // crashed prior run) can no longer collide on the same path.
    m_workspaceDir = QDir::temp().filePath(
        "WakkaQt_separation_" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(m_workspaceDir)) {
        m_workspaceDir.clear();
        emit separationFailed("Failed to create a scratch workspace for separation.", false);
        return;
    }

    m_cancelled = std::make_shared<std::atomic<bool>>(false);

    if (m_separateWatcher) {
        m_separateWatcher->deleteLater();
        m_separateWatcher = nullptr;
    }
    QFutureWatcher<SeparateResult> *watcher = new QFutureWatcher<SeparateResult>(this);
    m_separateWatcher = watcher;
    connect(watcher, &QFutureWatcher<SeparateResult>::finished, this, [this, watcher]() {
        const SeparateResult res = watcher->result();
        if (m_separateWatcher == watcher)
            m_separateWatcher = nullptr;
        watcher->deleteLater();

        const QString &tempPath = res.first;
        const QString &err = res.second;
        if (tempPath.isEmpty()) {
            discardWorkspace(); // nothing usable came out of this run
            emit separationFailed(err, err == "Cancelled");
            return;
        }
        emit separated(tempPath);
    });

    auto cancelledCopy = m_cancelled; // shared_ptr, safe to copy across threads
    const QString workspaceDir = m_workspaceDir;
    auto future = QtConcurrent::run([this, inputFile, workspaceDir, cancelledCopy]() -> SeparateResult {
        QString err;
        QString path = VocalSeparator::separate(inputFile, workspaceDir, [this](int pct) {
            emit separationProgress(pct); // emitted from a worker thread; Qt auto-queues to this' thread
        }, err, cancelledCopy.get());
        return {path, err};
    });
    watcher->setFuture(future);
}

// ── exportResult ─────────────────────────────────────────────────────────
void VocalSeparationJob::exportResult(const ExportParams &params)
{
    if (isExporting())
        return;

    m_exportCancelled = std::make_shared<std::atomic<bool>>(false);

#ifdef WAKKAQT_FFMPEG_NATIVE
    exportNative(params);
#else
    exportFallback(params);
#endif
}

#ifdef WAKKAQT_FFMPEG_NATIVE
void VocalSeparationJob::exportNative(const ExportParams &params)
{
    if (m_exportWatcher) {
        m_exportWatcher->deleteLater();
        m_exportWatcher = nullptr;
    }
    QFutureWatcher<ExportResult> *watcher = new QFutureWatcher<ExportResult>(this);
    m_exportWatcher = watcher;
    const QString savePath = params.savePath;
    connect(watcher, &QFutureWatcher<ExportResult>::finished, this, [this, watcher, savePath]() {
        const ExportResult res = watcher->result();
        if (m_exportWatcher == watcher)
            m_exportWatcher = nullptr;
        watcher->deleteLater();

        if (res.first) {
            // Only ever discarded on success: a failed export deliberately
            // preserves the workspace (see the error text below) so the
            // user doesn't have to re-run the expensive separation.
            discardWorkspace();
            emit exported(savePath);
        } else {
            emit exportFailed(res.second);
        }
    });

    const QString inputFile    = params.inputFile;
    const QString tempOut      = params.tempWavPath;
    const bool    saveAsVideo  = params.saveAsVideo;
    const QString workspaceDir = m_workspaceDir;
    const QString partialPath  = partialPathFor(savePath);
    auto exportCancelledCopy   = m_exportCancelled;

    auto future = QtConcurrent::run([this, inputFile, tempOut, savePath, partialPath,
                                      workspaceDir, saveAsVideo, exportCancelledCopy]() -> ExportResult {
        QFile::remove(partialPath); // clear any leftover from a previous crashed attempt
        const std::atomic<bool> *cancelled = exportCancelledCopy.get();

        // Validates the new file landed on disk, then commits it onto
        // savePath via commitPartialOverFinal() — which backs up any
        // existing file first, so a rename failure during the swap restores
        // it instead of leaving savePath missing.
        auto finalizeAtomic = [&]() -> ExportResult {
            if (!QFile::exists(partialPath) || QFileInfo(partialPath).size() <= 0) {
                QFile::remove(partialPath);
                return {false, "Export produced no output.\n"
                               "The separated instrumental was preserved in:\n" + workspaceDir};
            }
            const QString err = commitPartialOverFinal(partialPath, savePath);
            if (!err.isEmpty())
                return {false, err};
            return {true, QString()};
        };

        if (saveAsVideo) {
            const bool ok = FFmpegNative::muxVideoWithAudio(inputFile, tempOut, partialPath,
                [this](int pct) { emit exportProgress(pct); }, cancelled);
            if (!ok) {
                QFile::remove(partialPath);
                if (cancelled && cancelled->load())
                    return {false, "Cancelled"};
                return {false, "Native video muxing failed. Check console debug log for details.\n"
                               "The separated instrumental was preserved in:\n" + workspaceDir};
            }
            return finalizeAtomic();
        } else {
            const bool ok = FFmpegNative::transcodeAudio(tempOut, partialPath,
                [this](int pct) { emit exportProgress(pct); }, cancelled);
            if (!ok) {
                QFile::remove(partialPath);
                if (cancelled && cancelled->load())
                    return {false, "Cancelled"};
                return {false, "Native MP3 encoding failed. Check log for details.\n"
                               "The separated instrumental was preserved in:\n" + workspaceDir};
            }
            return finalizeAtomic();
        }
    });
    watcher->setFuture(future);
}
#endif

// QProcess fallback path (no WAKKAQT_FFMPEG_NATIVE): runs the ffmpeg CLI
// asynchronously via QProcess::finished, owned directly by this job (m_exportProcess)
// instead of blocked-on inside a QtConcurrent worker thread — the plain
// ffmpeg CLI has no cooperative-cancellation hook, so the only way to make
// cancelExport() actually responsive is to hold the QProcess itself and
// kill() it, which requires it to live on this object rather than as a
// throwaway local blocked on with waitForFinished(timeout) somewhere else.
void VocalSeparationJob::exportFallback(const ExportParams &params)
{
    if (m_exportProcess) {
        m_exportProcess->deleteLater();
        m_exportProcess = nullptr;
    }

    const QString savePath     = params.savePath;
    const QString inputFile    = params.inputFile;
    const QString tempOut      = params.tempWavPath;
    const bool    saveAsVideo  = params.saveAsVideo;
    const QString workspaceDir = m_workspaceDir;
    const QString partialPath  = partialPathFor(savePath);
    auto exportCancelledCopy   = m_exportCancelled;

    QFile::remove(partialPath); // clear any leftover from a previous crashed attempt

    QStringList arguments;
    if (saveAsVideo) {
        arguments << "-y" << "-i" << inputFile << "-i" << tempOut
                  << "-c:v" << "copy" << "-c:a" << "aac"
                  << "-map" << "0:v:0" << "-map" << "1:a:0"
                  << partialPath;
        emit exportProgress(-1); // indeterminate — the CLI gives no sub-step progress here
    } else {
        arguments << "-y" << "-i" << tempOut << "-q:a" << "2" << partialPath;
        emit exportProgress(-1);
    }

    QProcess *process = new QProcess(this);
    m_exportProcess = process;
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, savePath, tempOut, partialPath, workspaceDir,
             saveAsVideo, exportCancelledCopy](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_exportProcess == process)
            m_exportProcess = nullptr;
        const QByteArray stderrOutput = process->readAllStandardError();
        process->deleteLater();

        if (exportCancelledCopy->load()) {
            QFile::remove(partialPath);
            emit exportFailed("Cancelled");
            return;
        }
        if (!(exitStatus == QProcess::NormalExit && exitCode == 0)) {
            const QString what = saveAsVideo ? "ffmpeg video muxing failed.\n"
                                              : "ffmpeg MP3 encoding failed.\n";
            QFile::remove(partialPath);
            emit exportFailed(what + QString::fromUtf8(stderrOutput).left(300) +
                              "\nThe separated instrumental was preserved in:\n" + workspaceDir);
            return;
        }

        if (!QFile::exists(partialPath) || QFileInfo(partialPath).size() <= 0) {
            QFile::remove(partialPath);
            emit exportFailed("Export produced no output.\n"
                              "The separated instrumental was preserved in:\n" + workspaceDir);
            return;
        }
        const QString commitErr = commitPartialOverFinal(partialPath, savePath);
        if (!commitErr.isEmpty()) {
            emit exportFailed(commitErr);
            return;
        }

        discardWorkspace(); // only ever discarded on success — see exportFailed's messages above
        emit exported(savePath);
    });

    process->start("ffmpeg", arguments);
    if (!process->waitForStarted()) {
        if (m_exportProcess == process)
            m_exportProcess = nullptr;
        process->deleteLater();
        emit exportFailed("Failed to start FFmpeg. Verify it is installed and available in PATH.");
    }
}
