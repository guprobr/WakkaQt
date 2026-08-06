#include "renderjob.h"
#include "atomicfilecommit.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QRegularExpression>
#include <QFile>
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

static QString partialPathFor(const QString &finalPath)
{
    return sidecarPathFor(finalPath, "partial");
}

RenderJob::RenderJob(QObject *parent) : QObject(parent) {}

RenderJob::~RenderJob()
{
    if (m_watcher) {
        m_watcher->waitForFinished();
        delete m_watcher;
    }
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished();
        delete m_process;
    }
}

bool RenderJob::isActive() const
{
    return (m_watcher && !m_watcher->isFinished()) || (m_process != nullptr);
}

void RenderJob::cancel()
{
    if (m_cancelled)
        m_cancelled->store(true);
    if (m_process)
        m_process->kill();
}

void RenderJob::waitForFinished()
{
    if (m_watcher)
        m_watcher->waitForFinished();
    if (m_process)
        m_process->waitForFinished();
}

void RenderJob::start(const Params &params)
{
    if (isActive()) {
        emit finished(false, false, "Render already in progress.");
        return;
    }
#ifdef WAKKAQT_FFMPEG_NATIVE
    startNative(params);
#else
    startFallback(params);
#endif
}

#ifdef WAKKAQT_FFMPEG_NATIVE
void RenderJob::startNative(const Params &params)
{
    m_cancelled = std::make_shared<std::atomic<bool>>(false);

    if (m_watcher) {
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    // Captured by value below instead of read from m_cancelled/m_lastOutputPath
    // inside the callback: isActive() only reflects whether the QFuture
    // itself has finished, not whether this queued callback has actually run
    // yet, so a new start() could in principle slip in during that narrow
    // window and repoint the members before this callback reads them.
    auto cancelledForThisRun = m_cancelled;
    const QString outputPathForThisRun = params.outputPath;
    // FFmpeg writes here instead of straight to outputPathForThisRun, so a
    // failed/cancelled render (or a crash mid-write) never touches a
    // pre-existing valid file at the real destination — only committed onto
    // it, atomically, once rendering actually succeeds (see below).
    const QString partialPathForThisRun = partialPathFor(outputPathForThisRun);
    QFile::remove(partialPathForThisRun); // clear any leftover from a previous crashed attempt

    QFutureWatcher<bool> *watcher = new QFutureWatcher<bool>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, cancelledForThisRun, outputPathForThisRun, partialPathForThisRun]() {
        const bool ok = watcher->result();
        if (m_watcher == watcher)
            m_watcher = nullptr;
        watcher->deleteLater();

        if (cancelledForThisRun->load()) {
            QFile::remove(partialPathForThisRun);
            emit finished(false, true, QString());
            return;
        }
        if (!ok) {
            QFile::remove(partialPathForThisRun);
            emit finished(false, false, "Rendering failed. Check the logs.");
            return;
        }
        if (!QFile::exists(partialPathForThisRun)) {
            emit finished(false, false, "Output file was not created.");
            return;
        }
        const QString commitErr = commitPartialOverFinal(partialPathForThisRun, outputPathForThisRun);
        if (!commitErr.isEmpty()) {
            emit finished(false, false, commitErr);
            return;
        }
        emit finished(true, false, QString());
    });

    m_lastOutputPath = params.outputPath;

    const QString tunedAudioPath   = params.tunedAudioPath;
    const QString webcamPath       = params.webcamPath;
    const QString playbackPath     = params.playbackPath;
    const QString partialOutputPath = partialPathForThisRun;
    const QString rawVocalPath     = params.rawVocalPath;
    const QString resolution       = params.resolution;
    const QString videoEffectChain = params.videoEffectChain;
    const double  vocalVolume      = params.vocalVolume;
    const qint64  audioOffsetMs    = params.audioOffsetMs;
    const qint64  videoOffsetMs    = params.videoOffsetMs;
    auto cancelledCopy = m_cancelled; // shared_ptr, safe to copy across threads

    auto future = QtConcurrent::run([=]() {
        return FFmpegNative::renderVideo(
            tunedAudioPath,
            webcamPath,
            playbackPath,
            partialOutputPath,
            vocalVolume,
            audioOffsetMs,
            videoOffsetMs,
            resolution,
            rawVocalPath,
            cancelledCopy.get(),
            [this](double p) {
                emit progress(p); // emitted from a worker thread; Qt auto-queues to this' thread
            },
            videoEffectChain);
    });
    watcher->setFuture(future);
}
#endif

void RenderJob::startFallback(const Params &params)
{
    m_cancelled = std::make_shared<std::atomic<bool>>(false);
    m_lastOutputPath = params.outputPath;
    // See startNative()'s equivalent comment: ffmpeg writes to this sidecar
    // instead of straight to params.outputPath, only committed atomically
    // onto the real destination once the process actually succeeds.
    const QString partialOutputPath = partialPathFor(params.outputPath);
    QFile::remove(partialOutputPath); // clear any leftover from a previous crashed attempt

    const qint64 manualOffset = params.audioOffsetMs; // effectiveAudioOffset == effectiveVideoOffset == manualOffset
    const QString offsetFilter = (manualOffset < 0)
        ? QString("adelay=%1|%1").arg(-manualOffset)
        : QString("atrim=start=%1,asetpts=PTS-STARTPTS").arg(manualOffset / 1000.0);

    QString videorama;
    if (params.hasWebcam &&
        (params.outputPath.endsWith(".mp4", Qt::CaseInsensitive) ||
         params.outputPath.endsWith(".avi", Qt::CaseInsensitive) ||
         params.outputPath.endsWith(".mkv", Qt::CaseInsensitive) ||
         params.outputPath.endsWith(".webm", Qt::CaseInsensitive)))
    {
        videorama = QString("[1:v]scale=s=%1[videorama];").arg(params.resolution);
    }

    // No camera recording exists to open as an input in this case — the
    // playback track shifts from index 2 to index 1.
    const QString playbackIdx = params.hasWebcam ? "2" : "1";

    QStringList arguments;
    arguments << "-y"
              << "-i" << params.tunedAudioPath;
    // -ss applies to whichever -i immediately follows it — that must stay
    // the webcam input (it seeks past its pre-roll); with no camera there is
    // no such input to seek into, so drop it rather than let it silently
    // reassign to playbackPath below.
    if (params.hasWebcam) {
        arguments << "-ss" << QString("%1ms").arg(params.videoOffsetMs)
                  << "-i" << params.webcamPath;
    }
    arguments << "-i" << params.playbackPath
              << "-filter_complex"
              << QString("[0:a]%1,volume=%2[vocals];"
                         "[%4:a][vocals]amix=inputs=2:normalize=0,aresample=async=1[wakkamix];%3")
                     .arg(offsetFilter).arg(params.vocalVolume).arg(videorama).arg(playbackIdx)
              << "-map" << "[wakkamix]";
    if (!videorama.isEmpty())
        arguments << "-map" << "[videorama]";
    arguments << partialOutputPath;

    const int totalDuration = static_cast<int>(params.totalDurationSeconds);

    QProcess *process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::readyReadStandardError, this,
            [this, process, totalDuration]() {
        const QString out = QString::fromUtf8(process->readAllStandardError()).trimmed();
        if (out.isEmpty())
            return;
        static const QRegularExpression timeRegex("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
        const QRegularExpressionMatch match = timeRegex.match(out);
        if (!match.hasMatch() || totalDuration <= 0)
            return;
        const int hours        = match.captured(1).toInt();
        const int minutes      = match.captured(2).toInt();
        const int seconds      = match.captured(3).toInt();
        const int centiseconds = match.captured(4).toInt();
        const qint64 elapsedMs = (qint64(hours) * 3600 + minutes * 60 + seconds) * 1000
                                 + centiseconds * 10;
        const qint64 totalMs = qint64(totalDuration) * 1000;
        emit progress(qBound(0.0, double(elapsedMs) / double(totalMs), 1.0));
    });
    auto cancelledForThisRun = m_cancelled;
    const QString outputPathForThisRun = params.outputPath;
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, cancelledForThisRun, outputPathForThisRun, partialOutputPath]
            (int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_process == process)
            m_process = nullptr;
        process->deleteLater();

        if (cancelledForThisRun->load()) {
            QFile::remove(partialOutputPath);
            emit finished(false, true, QString());
            return;
        }
        if (!(exitStatus == QProcess::NormalExit && exitCode == 0)) {
            QFile::remove(partialOutputPath);
            emit finished(false, false, "Rendering failed. Check the logs.");
            return;
        }
        if (!QFile::exists(partialOutputPath)) {
            emit finished(false, false, "Output file was not created.");
            return;
        }
        const QString commitErr = commitPartialOverFinal(partialOutputPath, outputPathForThisRun);
        if (!commitErr.isEmpty()) {
            emit finished(false, false, commitErr);
            return;
        }
        emit finished(true, false, QString());
    });

    process->start("ffmpeg", arguments);
    if (!process->waitForStarted()) {
        if (m_process == process)
            m_process = nullptr;
        process->deleteLater();
        emit finished(false, false,
            "Failed to start FFmpeg. Verify it is installed and available in PATH.");
        return;
    }
}
