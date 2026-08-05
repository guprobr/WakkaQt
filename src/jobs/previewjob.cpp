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
    if (m_extractWatcher && !m_extractWatcher->isFinished()) {
        // Requests FFmpegNative::extractAudio() to bail out of its decode
        // loop on the next iteration instead of just blocking here until it
        // runs to completion on its own.
        if (m_extractCancelled)
            m_extractCancelled->store(true);
        m_extractWatcher->waitForFinished();
    }
    if (m_extractProcess) {
        if (m_extractCancelled)
            m_extractCancelled->store(true);
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

    // Both paths below write to the same caller-owned destTempFile, so a
    // prior in-flight extraction must be stopped (not just abandoned) before
    // starting a new one — otherwise two writers could race on that path.
    // Cancel-and-wait instead of a plain reject: extract() replacing a
    // still-running extraction (e.g. the user reopens the preview on a new
    // file before the old one finished) is the normal, expected case here.
    if (m_extractWatcher && !m_extractWatcher->isFinished()) {
        if (m_extractCancelled)
            m_extractCancelled->store(true);
        m_extractWatcher->waitForFinished();
    }
    if (m_extractProcess) {
        if (m_extractCancelled)
            m_extractCancelled->store(true);
        m_extractProcess->kill();
        m_extractProcess->waitForFinished();
    }

    // Fresh per-run flag: the outgoing run's finished-callback (queued, not
    // yet delivered) keeps its own shared_ptr copy, so it can still tell it
    // was cancelled after this new run replaces m_extractCancelled here.
    auto cancelledForThisRun = std::make_shared<std::atomic<bool>>(false);
    m_extractCancelled = cancelledForThisRun;

#ifdef WAKKAQT_FFMPEG_NATIVE
    if (m_extractWatcher) {
        m_extractWatcher->deleteLater();
        m_extractWatcher = nullptr;
    }
    QFutureWatcher<bool> *watcher = new QFutureWatcher<bool>(this);
    m_extractWatcher = watcher;
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, destTempFile, cancelledForThisRun]() {
        const bool ok = watcher->result();
        if (m_extractWatcher == watcher)
            m_extractWatcher = nullptr;
        watcher->deleteLater();

        if (!ok) {
            if (cancelledForThisRun->load()) {
                emit extractionFailed(QString(), true);
                return;
            }
            emit extractionFailed("Native audio extraction failed.", false);
            return;
        }
        onExtractionFinished(true, destTempFile);
    });

    const QString sourceFile = params.sourceFile;
    const qint64 trimOffset  = params.trimOffsetMs;
    std::atomic<bool> *cancelFlag = cancelledForThisRun.get();
    auto extractFuture = QtConcurrent::run([sourceFile, destTempFile, trimOffset, cancelFlag]() {
        // Extract stereo (no mono hint — VocalEnhancer handles channel mixing internally)
        return FFmpegNative::extractAudio(sourceFile, destTempFile, trimOffset, {}, cancelFlag);
    });
    watcher->setFuture(extractFuture);
#else
    if (m_extractProcess) {
        m_extractProcess->deleteLater();
        m_extractProcess = nullptr;
    }
    QProcess *process = new QProcess(this);
    m_extractProcess = process;
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

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, destTempFile, cancelledForThisRun]
            (int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_extractProcess == process)
            m_extractProcess = nullptr;
        process->deleteLater();

        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            if (cancelledForThisRun->load()) {
                emit extractionFailed(QString(), true);
                return;
            }
            emit extractionFailed("FFmpeg process failed.", false);
            return;
        }
        onExtractionFinished(true, destTempFile);
    });

    process->start("ffmpeg", arguments);
    if (!process->waitForStarted()) {
        if (m_extractProcess == process)
            m_extractProcess = nullptr;
        process->deleteLater();
        emit extractionFailed("Failed to start FFmpeg.", false);
    }
#endif
}

void PreviewJob::onExtractionFinished(bool /*ok*/, const QString &destTempFile)
{
    QFile audioFile(destTempFile);
    if (!audioFile.exists() || audioFile.size() <= 0) {
        emit extractionFailed("Audio extraction failed or file is empty.", false);
        return;
    }
    if (!audioFile.open(QIODevice::ReadOnly)) {
        emit extractionFailed("Failed to read extracted preview audio.", false);
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
        emit extractionFailed("Extracted preview audio could not be parsed.", false);
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

    QFutureWatcher<QByteArray> *watcher = new QFutureWatcher<QByteArray>(this);
    m_enhanceWatcher = watcher;
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher]() {
        const QByteArray tunedData = watcher->result();
        if (m_enhanceWatcher == watcher)
            m_enhanceWatcher = nullptr;
        watcher->deleteLater();

        // A cancelled enhance() returns an empty buffer early — still
        // forwarded as-is; the caller checks emptiness/cancellation the
        // same way it checked enhanceCancelled/tunedData before.
        emit enhanced(tunedData);
    });

    VocalEnhancer *enhancer = m_enhancer.data();
    auto future = QtConcurrent::run([enhancer, pcmData, this]() {
        return enhancer->enhance(pcmData, &m_enhanceCancelled);
    });
    watcher->setFuture(future);
    return true;
}
