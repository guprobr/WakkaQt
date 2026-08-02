#include "renderjob.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QRegularExpression>
#include <QFile>
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

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
    m_watcher = new QFutureWatcher<bool>(this);
    connect(m_watcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool ok = m_watcher->result();
        QFutureWatcher<bool> *finishedWatcher = m_watcher;
        m_watcher = nullptr;
        finishedWatcher->deleteLater();

        if (m_cancelled->load()) {
            emit finished(false, true, QString());
            return;
        }
        if (!ok) {
            emit finished(false, false, "Rendering failed. Check the logs.");
            return;
        }
        if (!QFile::exists(m_lastOutputPath)) {
            emit finished(false, false, "Output file was not created.");
            return;
        }
        emit finished(true, false, QString());
    });

    m_lastOutputPath = params.outputPath;

    const QString tunedAudioPath   = params.tunedAudioPath;
    const QString webcamPath       = params.webcamPath;
    const QString playbackPath     = params.playbackPath;
    const QString outputPath       = params.outputPath;
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
            outputPath,
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
    m_watcher->setFuture(future);
}
#endif

void RenderJob::startFallback(const Params &params)
{
    m_cancelled = std::make_shared<std::atomic<bool>>(false);
    m_lastOutputPath = params.outputPath;

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
    arguments << params.outputPath;

    const int totalDuration = static_cast<int>(params.totalDurationSeconds);

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardError, this,
            [this, totalDuration]() {
        const QString out = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
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
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        QProcess *finishedProcess = m_process;
        m_process = nullptr;
        finishedProcess->deleteLater();

        if (m_cancelled->load()) {
            emit finished(false, true, QString());
            return;
        }
        if (!(exitStatus == QProcess::NormalExit && exitCode == 0)) {
            emit finished(false, false, "Rendering failed. Check the logs.");
            return;
        }
        if (!QFile::exists(m_lastOutputPath)) {
            emit finished(false, false, "Output file was not created.");
            return;
        }
        emit finished(true, false, QString());
    });

    m_process->start("ffmpeg", arguments);
    if (!m_process->waitForStarted()) {
        m_process->deleteLater();
        m_process = nullptr;
        emit finished(false, false,
            "Failed to start FFmpeg. Verify it is installed and available in PATH.");
        return;
    }
}
