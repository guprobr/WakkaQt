#include "previewjob.h"
#include "complexes.h"
#include "playbackbleedfilter.h"
#include <QTemporaryFile>
#include <QProcess>

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
    // Keep the run active until its queued result has been published. A new
    // run must not reset the shared cancellation flag ahead of that callback.
    return m_enhanceWatcher != nullptr;
}

void PreviewJob::waitForIdle()
{
    if (m_enhanceWatcher) {
        m_enhanceCancelled.store(true);
        m_enhanceWatcher->waitForFinished();
    }
    if (m_extractWatcher) {
        // Requests FFmpegNative::extractAudio() to bail out of its decode
        // loop on the next iteration instead of just blocking here until it
        // runs to completion on its own.
        if (m_extractCancelled)
            m_extractCancelled->store(true);
        m_extractWatcher->waitForFinished();
    }
}

void PreviewJob::cancelEnhance()
{
    m_enhanceCancelled.store(true);
}

namespace {
bool stopped(const std::atomic<bool> *flag) { return flag && flag->load(); }

// CLI work runs on a worker thread, with cancellation checked while waiting.
bool runFfmpeg(const QStringList &arguments, const std::atomic<bool> *cancelled) {
    if (stopped(cancelled)) return false;
    QProcess process;
    process.start("ffmpeg", arguments);
    if (!process.waitForStarted()) return false;
    while (process.state() != QProcess::NotRunning && !process.waitForFinished(100)) {
        if (stopped(cancelled)) {
            process.kill();
            process.waitForFinished();
            return false;
        }
    }
    return !stopped(cancelled) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool decodeAudio(const QString &source, const QString &dest, qint64 trim,
                 const std::atomic<bool> *cancelled, const QAudioFormat &targetFormat = {}) {
#ifdef WAKKAQT_FFMPEG_NATIVE
    int rate = targetFormat.isValid() ? targetFormat.sampleRate()
                                      : FFmpegNative::getAudioSampleRate(source);
    // Keep ordinary recordings at their native rate. High-rate device
    // defaults (e.g. 192 kHz/Int32) need an actual resample before cleanup:
    // the echo canceller accepts 8–96 kHz and consumes Int16 samples.
    if (!targetFormat.isValid() && (rate < 8000 || rate > 96000)) rate = 48000;
    if (stopped(cancelled)) return false;
    return FFmpegNative::extractAudio(source, dest, trim,
        targetFormat.channelCount() == 1 ? QStringLiteral("mono") : QString(), cancelled,
        rate);
#else
    return runFfmpeg({"-v", "error", "-y", "-i", source, "-vn", "-af",
                     QString("atrim=start=%1,asetpts=PTS-STARTPTS").arg(trim / 1000.0),
                     "-ar", QString::number(targetFormat.isValid() ? targetFormat.sampleRate() : 44100),
                     "-ac", QString::number(targetFormat.isValid() ? targetFormat.channelCount() : 2),
                     "-c:a", "pcm_s16le", "-f", "wav", dest}, cancelled);
#endif
}

QByteArray masterAudio(const QByteArray &pcm, const QAudioFormat &format,
                       const std::atomic<bool> *cancelled) {
    if (stopped(cancelled)) return {};
#ifdef WAKKAQT_FFMPEG_NATIVE
    return FFmpegNative::applyFilterChainS16(pcm, format.sampleRate(), format.channelCount(), _audioMasterization);
#else
    QTemporaryFile input, output;
    if (!input.open() || !output.open()) return {};
    const QString source = input.fileName(), dest = output.fileName();
    input.close();
    output.close();
    QFile file(source);
    if (!file.open(QIODevice::WriteOnly)) return {};
    if (!writeWavHeader(file, format, pcm.size(), pcm)) return {};
    file.close();
    if (!runFfmpeg({"-v", "error", "-y", "-i", source, "-af", _audioMasterization,
                   "-ar", QString::number(format.sampleRate()), "-ac", QString::number(format.channelCount()),
                   "-c:a", "pcm_s16le", "-f", "wav", dest}, cancelled)) return {};
    QFile result(dest);
    if (!result.open(QIODevice::ReadOnly)) return {};
    return parseWavPcm(result.readAll()).samples;
#endif
}
} // namespace

void PreviewJob::extract(const ExtractParams &params)
{
    if (m_extractWatcher) {
        if (m_extractCancelled) m_extractCancelled->store(true);
        m_extractWatcher->waitForFinished();
        m_extractWatcher->deleteLater();
        m_extractWatcher = nullptr;
    }
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    m_extractCancelled = cancelled;
    auto *watcher = new QFutureWatcher<ExtractedAudio>(this);
    m_extractWatcher = watcher;
    connect(watcher, &QFutureWatcher<ExtractedAudio>::finished, this, [this, watcher, cancelled]() {
        const ExtractedAudio result = watcher->result();
        if (m_extractWatcher == watcher) m_extractWatcher = nullptr;
        watcher->deleteLater();
        if (cancelled->load()) {
            emit extractionFailed({}, true);
        } else if (!result.ok) {
            emit extractionFailed(result.error, false);
        } else {
            emit extracted(result.samples, result.format, result.playbackSamples);
        }
    });
    watcher->setFuture(QtConcurrent::run([params, cancelled]() -> ExtractedAudio {
        ExtractedAudio result;
        if (!decodeAudio(params.sourceFile, params.destTempFile, params.trimOffsetMs, cancelled.get())) {
            QFile::remove(params.destTempFile);
            result.error = "Audio extraction failed.";
            return result;
        }
        result = processExtractedFile(params.destTempFile);
        if (!result.ok || stopped(cancelled.get()) || params.playbackFile.isEmpty()) return result;
        QTemporaryFile reference;
        if (!reference.open()) {
            result.ok = false;
            result.error = "Could not create playback reference file.";
            return result;
        }
        const QString referencePath = reference.fileName();
        reference.close();
        // Decode reference at the vocal rate/layout, without mastering or
        // trimming: alignment uses the original song timeline even for a
        // trimmed vocal or later snippet.
        if (!decodeAudio(params.playbackFile, referencePath, 0, cancelled.get(), result.format)) {
            result.ok = false;
            result.error = "Could not decode playback reference.";
            return result;
        }
        ExtractedAudio ref = processExtractedFile(referencePath);
        if (!ref.ok) {
            result.ok = false;
            result.error = "Could not read playback reference: " + ref.error;
            return result;
        }
        if (ref.format.sampleRate() != result.format.sampleRate() ||
            ref.format.channelCount() != result.format.channelCount() ||
            ref.format.sampleFormat() != result.format.sampleFormat()) {
            result.ok = false;
            result.error = QString("Playback reference conversion failed: expected %1 Hz, %2 channels "
                                   "of 16-bit PCM; got %3 Hz, %4 channels.")
                .arg(result.format.sampleRate()).arg(result.format.channelCount())
                .arg(ref.format.sampleRate()).arg(ref.format.channelCount());
            return result;
        }
        result.playbackSamples = ref.samples;
        return result;
    }));
}

PreviewJob::ExtractedAudio PreviewJob::processExtractedFile(const QString &destTempFile)
{
    ExtractedAudio result;
    QFile file(destTempFile);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = "Failed to read extracted preview audio.";
        return result;
    }
    const PcmBuffer pcm = parseWavPcm(file.readAll());
    file.close();
    QFile::remove(destTempFile);
    if (!pcm.isValid()) {
        result.error = "Extracted preview audio could not be parsed.";
        return result;
    }
    result.ok = true;
    result.samples = pcm.samples;
    result.format = pcm.format;
    return result;
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
        format.channelCount() != m_enhancerFormat.channelCount() ||
        format.sampleFormat() != m_enhancerFormat.sampleFormat()) {
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
        emit enhanced(m_enhanceCancelled.load() ? QByteArray() : tunedData);
    });

    VocalEnhancer *enhancer = m_enhancer.data();
    auto future = QtConcurrent::run([enhancer, pcmData, format, params, this]() {
        QByteArray clean = pcmData;
        if (params.removePlaybackBleed) {
            const auto result = PlaybackBleedFilter::process(pcmData, params.playbackPcm, format,
                params.referenceStartFrame, &m_enhanceCancelled, [enhancer](int progress) {
                    enhancer->reportProcessingStatus("Removing speaker playback from microphones…", progress);
                });
            if (!result.error.isEmpty()) {
                emit enhancementFailed(result.error);
                return QByteArray();
            }
            clean = result.samples;
            if (!clean.isEmpty()) {
                emit cleanupStatus(result.applied
                    ? QString("Playback bleed removal applied (delay %1 ms).")
                        .arg(result.delaySamples * 1000.0 / format.sampleRate(), 0, 'f', 1)
                    : "No reliable playback bleed detected; microphone audio kept unchanged.");
            }
        } else {
            emit cleanupStatus("Playback bleed removal is off.");
        }
        if (clean.isEmpty() || m_enhanceCancelled.load()) return QByteArray();
        QByteArray tuned = enhancer->enhance(clean, &m_enhanceCancelled);
        if (tuned.isEmpty() || m_enhanceCancelled.load() || !params.masterAudio) return tuned;
        enhancer->reportProcessingStatus("Mastering cleaned vocals…", 99);
        tuned = masterAudio(tuned, format, &m_enhanceCancelled);
        if (tuned.isEmpty() && !m_enhanceCancelled.load())
            emit enhancementFailed("Vocal mastering failed.");
        return m_enhanceCancelled.load() ? QByteArray() : tuned;
    });
    watcher->setFuture(future);
    return true;
}
