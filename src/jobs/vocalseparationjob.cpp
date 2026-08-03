#include "vocalseparationjob.h"
#include "vocalseparator.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QFile>
#include <QProcess>
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

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
    return m_exportWatcher && !m_exportWatcher->isFinished();
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

void VocalSeparationJob::waitForFinished()
{
    if (m_separateWatcher && !m_separateWatcher->isFinished())
        m_separateWatcher->waitForFinished();
    if (m_exportWatcher && !m_exportWatcher->isFinished())
        m_exportWatcher->waitForFinished();
}

// ── separate ─────────────────────────────────────────────────────────────
void VocalSeparationJob::separate(const QString &inputFile)
{
    if (isActive())
        return; // caller gates re-entry via MainWindow's Separating state

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
            emit separationFailed(err, err == "Cancelled");
            return;
        }
        emit separated(tempPath);
    });

    auto cancelledCopy = m_cancelled; // shared_ptr, safe to copy across threads
    auto future = QtConcurrent::run([this, inputFile, cancelledCopy]() -> SeparateResult {
        QString err;
        QString path = VocalSeparator::separate(inputFile, [this](int pct) {
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

        if (res.first)
            emit exported(savePath);
        else
            emit exportFailed(res.second);
    });

    const QString inputFile   = params.inputFile;
    const QString tempOut     = params.tempWavPath;
    const bool    saveAsVideo = params.saveAsVideo;

    auto future = QtConcurrent::run([this, inputFile, tempOut, savePath, saveAsVideo]() -> ExportResult {
        if (saveAsVideo) {
#ifdef WAKKAQT_FFMPEG_NATIVE
            const bool ok = FFmpegNative::muxVideoWithAudio(inputFile, tempOut, savePath,
                [this](int pct) { emit exportProgress(pct); });
            QFile::remove(tempOut);
            return {ok, ok ? QString()
                          : "Native video muxing failed. Check console debug log for details."};
#else
            emit exportProgress(-1);
            QProcess mux;
            mux.start("ffmpeg", {"-y", "-i", inputFile, "-i", tempOut,
                                  "-c:v", "copy", "-c:a", "aac",
                                  "-map", "0:v:0", "-map", "1:a:0",
                                  savePath});
            mux.waitForFinished(300000);
            const bool ok = mux.exitCode() == 0;
            const QString err = ok ? QString()
                : "ffmpeg video muxing failed.\n" + QString(mux.readAllStandardError()).left(300);
            QFile::remove(tempOut);
            return {ok, err};
#endif
        } else {
#ifdef WAKKAQT_FFMPEG_NATIVE
            const bool ok = FFmpegNative::transcodeAudio(tempOut, savePath,
                [this](int pct) { emit exportProgress(pct); });
            QFile::remove(tempOut);
            return {ok, ok ? QString()
                          : "Native MP3 encoding failed. Check log for details."};
#else
            emit exportProgress(-1);
            QProcess mp3;
            mp3.start("ffmpeg", {"-y", "-i", tempOut, "-q:a", "2", savePath});
            mp3.waitForFinished(120000);
            const bool ok = mp3.exitCode() == 0;
            const QString err = ok ? QString()
                : "ffmpeg MP3 encoding failed.\n" + QString(mp3.readAllStandardError()).left(300);
            QFile::remove(tempOut);
            return {ok, err};
#endif
        }
    });
    watcher->setFuture(future);
}
