#include "previewjob.h"
#include "complexes.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QFile>
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

PreviewJob::PreviewJob(QObject *parent) : QObject(parent) {}

PreviewJob::~PreviewJob()
{
    waitForIdle();
}

bool PreviewJob::isEnhancing() const
{
    return m_enhanceWatcher && !m_enhanceWatcher->isFinished();
}

void PreviewJob::waitForIdle()
{
    if (m_enhanceWatcher && !m_enhanceWatcher->isFinished()) {
        m_enhanceCancelled.store(true);
        m_enhanceWatcher->cancel();
        m_enhanceWatcher->waitForFinished();
    }
    if (m_extractWatcher && !m_extractWatcher->isFinished())
        m_extractWatcher->waitForFinished();
    if (m_extractProcess) {
        m_extractProcess->kill();
        m_extractProcess->waitForFinished();
    }
}

void PreviewJob::cancelEnhance()
{
    m_enhanceCancelled.store(true);
}

// ── extract ───────────────────────────────────────────────────────────────
void PreviewJob::extract(const ExtractParams &params)
{
    const QString destTempFile = params.destTempFile;

#ifdef WAKKAQT_FFMPEG_NATIVE
    if (m_extractWatcher) {
        m_extractWatcher->deleteLater();
        m_extractWatcher = nullptr;
    }
    m_extractWatcher = new QFutureWatcher<bool>(this);
    connect(m_extractWatcher, &QFutureWatcher<bool>::finished, this, [this, destTempFile]() {
        const bool ok = m_extractWatcher->result();
        QFutureWatcher<bool> *finishedWatcher = m_extractWatcher;
        m_extractWatcher = nullptr;
        finishedWatcher->deleteLater();

        if (!ok) {
            emit extractionFailed("Native audio extraction failed.");
            return;
        }
        onExtractionFinished(true, destTempFile);
    });

    const QString sourceFile = params.sourceFile;
    const qint64 trimOffset  = params.trimOffsetMs;
    auto extractFuture = QtConcurrent::run([sourceFile, destTempFile, trimOffset]() {
        // Extract stereo (no mono hint — VocalEnhancer handles channel mixing internally)
        return FFmpegNative::extractAudio(sourceFile, destTempFile, trimOffset);
    });
    m_extractWatcher->setFuture(extractFuture);
#else
    if (m_extractProcess) {
        m_extractProcess->kill();
        m_extractProcess->deleteLater();
        m_extractProcess = nullptr;
    }
    m_extractProcess = new QProcess(this);
    QStringList arguments;
    arguments << "-y"
              << "-i" << params.sourceFile
              << "-vn"
              << "-filter_complex"
              // Masterization baked directly into this single ffmpeg call —
              // the native path applies it as a separate C++ step instead
              // (see onExtractionFinished()).
              << QString("%1%2,atrim=%3ms,asetpts=PTS-STARTPTS;")
                     .arg(_audioEnhance).arg(_audioMasterization).arg(params.trimOffsetMs)
              << "-ac" << "2"
              << "-acodec" << "pcm_s16le"
              << "-async" << "1"
              << destTempFile;

    connect(m_extractProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, destTempFile](int exitCode, QProcess::ExitStatus exitStatus) {
        QProcess *finishedProcess = m_extractProcess;
        m_extractProcess = nullptr;
        finishedProcess->deleteLater();

        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            emit extractionFailed("FFmpeg process failed.");
            return;
        }
        onExtractionFinished(true, destTempFile);
    });

    m_extractProcess->start("ffmpeg", arguments);
    if (!m_extractProcess->waitForStarted()) {
        m_extractProcess->deleteLater();
        m_extractProcess = nullptr;
        emit extractionFailed("Failed to start FFmpeg.");
    }
#endif
}

void PreviewJob::onExtractionFinished(bool /*ok*/, const QString &destTempFile)
{
    QFile audioFile(destTempFile);
    if (!audioFile.exists() || audioFile.size() <= 0) {
        emit extractionFailed("Audio extraction failed or file is empty.");
        return;
    }
    if (!audioFile.open(QIODevice::ReadOnly)) {
        emit extractionFailed("Failed to read extracted preview audio.");
        return;
    }
    const QByteArray wavBytes = audioFile.readAll();
    audioFile.close();
    QFile::remove(destTempFile);

    // parseWavPcm() walks the actual RIFF chunk structure instead of
    // assuming a fixed 44-byte header, and never leaves header bytes
    // attached to what everything downstream treats as raw PCM samples.
    PcmBuffer pcm = parseWavPcm(wavBytes);
    if (!pcm.isValid()) {
        emit extractionFailed("Extracted preview audio could not be parsed.");
        return;
    }

#ifdef WAKKAQT_FFMPEG_NATIVE
    // Apply audio masterization to the raw vocal extract BEFORE VocalEnhancer
    // runs, so the mastering filters and the enhancer don't compound. The
    // QProcess fallback bakes this into its ffmpeg invocation instead.
    pcm.samples = FFmpegNative::applyFilterChainS16(
        pcm.samples, pcm.format.sampleRate(), pcm.format.channelCount(),
        _audioMasterization);
#endif

    emit extracted(pcm.samples, pcm.format);
}

// ── enhance ───────────────────────────────────────────────────────────────
bool PreviewJob::enhance(const QByteArray &pcmData, const QAudioFormat &format,
                          const EnhanceParams &params)
{
    if (isEnhancing())
        return false;

    m_enhanceCancelled.store(false);

    if (m_enhanceWatcher) {
        m_enhanceWatcher->deleteLater();
        m_enhanceWatcher = nullptr;
    }

    if (!m_hasEnhancerFormat || format.sampleRate() != m_enhancerFormat.sampleRate() ||
        format.channelCount() != m_enhancerFormat.channelCount()) {
        m_enhancerFormat = format;
        m_hasEnhancerFormat = true;
        m_enhancer.reset(new VocalEnhancer(format, this));
    }

    m_enhancer->setPitchCorrectionAmount(params.pitchCorrectionAmount);
    m_enhancer->setNoiseReductionAmount(params.noiseReductionAmount);
    m_enhancer->setRetuneSpeed(params.retuneSpeedMs);
    m_enhancer->setFormantPreservation(params.formantPreservation);
    m_enhancer->setReverbRoomSize(params.reverbRoomSize);
    m_enhancer->setReverbDecay(params.reverbDecay);
    m_enhancer->setReverbMix(params.reverbMix);
    m_enhancer->setScalePreset(params.scalePreset, params.keyNote);

    m_enhanceWatcher = new QFutureWatcher<QByteArray>(this);
    connect(m_enhanceWatcher, &QFutureWatcher<QByteArray>::finished, this, [this]() {
        const QByteArray tunedData = m_enhanceWatcher->result();
        QFutureWatcher<QByteArray> *finishedWatcher = m_enhanceWatcher;
        m_enhanceWatcher = nullptr;
        finishedWatcher->deleteLater();

        // A cancelled enhance() returns an empty buffer early — still
        // forwarded as-is; the caller checks emptiness/cancellation the
        // same way it checked enhanceCancelled/tunedData before.
        emit enhanced(tunedData);
    });

    VocalEnhancer *enhancer = m_enhancer.data();
    auto future = QtConcurrent::run([enhancer, pcmData, this]() {
        return enhancer->enhance(pcmData, &m_enhanceCancelled);
    });
    m_enhanceWatcher->setFuture(future);
    return true;
}
