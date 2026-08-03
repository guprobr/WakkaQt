#include "audioamplifier.h"
#include "complexes.h"

#include <QMediaDevices>
#include <QTime>
#include <QDebug>
#include <algorithm>

AudioAmplifier::AudioAmplifier(const QAudioFormat &format, QObject *parent)
    : QObject(parent),
      audioFormat(format),
      volumeFactor(1.0),
      playbackPosition(0)
{
    playbackSink.reset(new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this));
    audioSink.reset(new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this));
    connect(audioSink.data(), &QAudioSink::stateChanged,
            this, &AudioAmplifier::handleStateChanged);

    audioBuffer.reset(new QBuffer());
    playbackBuffer.reset(new QBuffer());

    playbackFile.setFileName(extractedTmpPlayback);
    if (playbackFile.isOpen())
        playbackFile.close();
    if (!playbackFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open playback file!";
        return;
    }
    const QByteArray wavBytes = playbackFile.readAll();

    // parseWavPcm() strips the RIFF/WAV container entirely — playbackData is
    // pure PCM from here on. It used to keep the 44-byte header attached
    // (even re-prepending a hand-patched one after resampling below), and
    // since playbackData is handed straight to playbackBuffer/QAudioSink
    // with no other decoding step, those header bytes were being played as
    // an audible transient at the start of every backing track.
    const PcmBuffer pcm = parseWavPcm(wavBytes);
    if (!pcm.isValid()) {
        qWarning() << "AudioAmplifier: backing track is not a valid WAV file";
        return;
    }

    // The resample below reinterprets the raw bytes as int16_t and the mix
    // clamp in applyAmplification() hardcodes 16-bit range — both silently
    // produce garbage if either side isn't actually Int16. Skip the backing
    // track rather than feed noise into the sink; the vocal path (set up
    // below regardless) is unaffected.
    const int pbCh      = pcm.format.channelCount();
    const int pbRate     = pcm.format.sampleRate();
    const int targetRate = audioFormat.sampleRate();
    if (pcm.format.sampleFormat() != QAudioFormat::Int16 ||
        audioFormat.sampleFormat() != QAudioFormat::Int16) {
        qWarning() << "AudioAmplifier: backing track/vocal PCM is not Int16 (backing:"
                   << pcm.format.sampleFormat() << ", vocal:" << audioFormat.sampleFormat()
                   << ") — disabling backing playback";
    } else if (pbCh != audioFormat.channelCount()) {
        qWarning() << "AudioAmplifier: backing track channel count (" << pbCh
                   << ") does not match vocal format (" << audioFormat.channelCount()
                   << ") — disabling backing playback";
    } else {
        playbackData = pcm.samples;

        // Resample backing track if its rate doesn't match the vocal format
        // rate. Both streams go through the same QAudioSink; mismatched
        // rates cause chipmunk/slow-motion effects and byte-offset seek errors.
        if (pbRate != targetRate) {
            const int inFrames   = playbackData.size() / (pbCh * 2);
            const int outFrames  = int(qint64(inFrames) * targetRate / pbRate);
            QByteArray resampled(outFrames * pbCh * 2, 0);
            const int16_t *src = reinterpret_cast<const int16_t*>(playbackData.constData());
            int16_t *dst = reinterpret_cast<int16_t*>(resampled.data());
            for (int i = 0; i < outFrames; ++i) {
                const double pos = double(i) * pbRate / targetRate;
                const int i0 = std::min(static_cast<int>(pos), inFrames - 1);
                const int i1 = std::min(i0 + 1, inFrames - 1);
                const double f = pos - i0;
                for (int c = 0; c < pbCh; ++c)
                    dst[i * pbCh + c] = static_cast<int16_t>(
                        src[i0 * pbCh + c] * (1.0 - f) + src[i1 * pbCh + c] * f);
            }
            playbackData = resampled;
            qDebug() << "AudioAmplifier: resampled backing" << pbRate << "Hz ->" << targetRate << "Hz";
        }
    }

    dataPushTimer.reset(new QTimer(this));
    connect(dataPushTimer.data(), &QTimer::timeout,
            this, &AudioAmplifier::checkBufferState);
}

AudioAmplifier::~AudioAmplifier()
{
    stop();

    if (audioBuffer && audioBuffer->isOpen())
        audioBuffer->close();
    if (playbackBuffer && playbackBuffer->isOpen())
        playbackBuffer->close();

    if (audioBuffer)
        audioBuffer->setData(QByteArray());
    if (playbackBuffer)
        playbackBuffer->setData(QByteArray());

    if (dataPushTimer)
        dataPushTimer->stop();

    playbackSink.reset();
    audioSink.reset();
    audioBuffer.reset();
    playbackBuffer.reset();
}

QString AudioAmplifier::checkBufferState()
{
    if (!audioBuffer || !audioBuffer->isOpen() || !audioSink || audioSink->isNull())
        return "...Encoding...";

    playbackPosition = audioBuffer->pos();
    emitVocalPreviewChunk();

    const qint64 totalDuration = originalAudioData.size() * 1000000LL /
        (audioSink->format().sampleRate() *
         audioSink->format().channelCount() *
         audioSink->format().bytesPerSample());

    const qint64 playbackPositionUSecs = playbackPosition * 1000000LL /
        (audioSink->format().sampleRate() *
         audioSink->format().channelCount() *
         audioSink->format().bytesPerSample());

    const qint64 processedDuration = playbackPositionUSecs;
    const qint64 threshold = 500000;

    if (audioBuffer->bytesAvailable() < 1024) {
        qWarning() << "Audio buffer too small during playback. Restarting playback.";
        stop();
        resetAudioComponents();
        rewind();
        start();
        rewind();
        return "NaN";
    }

    if (processedDuration >= totalDuration - threshold) {
        qWarning() << "Buffer near the end. Restarting playback.";
        stop();
        resetAudioComponents();
        rewind();
        start();
        rewind();
        return "NaN";
    }

    const long long totalSeconds = processedDuration / 1000000LL;
    const int hours = int(totalSeconds / 3600);
    const int minutes = int((totalSeconds % 3600) / 60);
    const int seconds = int(totalSeconds % 60);
    return QTime(hours, minutes, seconds).toString("hh:mm:ss");
}

void AudioAmplifier::start()
{
    if (originalAudioData.isEmpty()) {
        qWarning() << "No audio data.";
        return;
    }

    setAudioOffset(audioFormat.durationForBytes(byteOffset) / 1000);

    if (amplifiedAudioData.size() < 512) {
        qWarning() << "Amplified buffer audio size is too small:" << amplifiedAudioData.size();
        return;
    }

    stop();

    if (audioBuffer->isOpen())
        audioBuffer->close();
    audioBuffer->setData(amplifiedAudioData);
    audioBuffer->open(QIODevice::ReadOnly);
    audioBuffer->seek(playbackPosition);

    if (playbackBuffer->isOpen())
        playbackBuffer->close();
    playbackBuffer->setData(playbackData);
    playbackBuffer->open(QIODevice::ReadOnly);
    playbackBuffer->seek(playbackPosition);

    playbackSink->start(playbackBuffer.data());
    playbackSink->setVolume(playbackVol ? 1.0 : 0.0);
    audioSink->start(audioBuffer.data());
    dataPushTimer->start(25);
    emitVocalPreviewChunk();
    // Note: checkBufferState() is intentionally NOT called here.
    // The dataPushTimer fires every 25 ms and will call it shortly,
    // avoiding a potential re-entry loop when checkBufferState() itself
    // calls start() on an underrun condition.
    qDebug() << "Start amplified vocals and backing track.";
}

void AudioAmplifier::stop()
{
    if (audioSink && audioSink->state() != QAudio::StoppedState) {
        playbackPosition = audioBuffer ? audioBuffer->pos() : playbackPosition;
        audioSink->stop();
        qDebug() << "Stopped vocals.";
    }

    if (playbackSink && playbackSink->state() != QAudio::StoppedState) {
        playbackSink->stop();
        qDebug() << "Stopped backingtrack.";
    }

    if (audioBuffer && audioBuffer->isOpen() && !isPlaying()) {
        audioBuffer->close();
        qDebug() << "Closed vocals buffer.";
    }
    if (playbackBuffer && playbackBuffer->isOpen() && !isPlayingPlayback()) {
        playbackBuffer->close();
        qDebug() << "Closed backing track buffer.";
    }

    if (dataPushTimer)
        dataPushTimer->stop();
}

// Was a hardcoded 514000-byte jump, which only skips ~2.7s at the format
// this class actually runs at (48kHz/stereo/Int16) and drifts to a different
// wall-clock duration for any other sample rate/channel count. Deriving it
// from the live QAudioFormat keeps the seek step at a fixed time regardless
// of format.
static constexpr qint64 kSeekStepMs = 3000;

void AudioAmplifier::seekForward()
{
    if (!audioBuffer || !playbackBuffer)
        return;
    const qint64 seekStepBytes = audioFormat.bytesForDuration(kSeekStepMs * 1000);
    if (audioBuffer->bytesAvailable() > seekStepBytes) {
        playbackPosition = audioBuffer->pos() + seekStepBytes;
        audioBuffer->seek(playbackPosition);
        playbackBuffer->seek(playbackPosition);
        emitVocalPreviewChunk();
        checkBufferState();
    }
}

void AudioAmplifier::seekBackward()
{
    if (!audioBuffer || !playbackBuffer)
        return;
    const qint64 seekStepBytes = audioFormat.bytesForDuration(kSeekStepMs * 1000);
    if (audioBuffer->pos() - seekStepBytes > 0) {
        playbackPosition = audioBuffer->pos() - seekStepBytes;
        audioBuffer->seek(playbackPosition);
        playbackBuffer->seek(playbackPosition);
        emitVocalPreviewChunk();
        checkBufferState();
    }
}

void AudioAmplifier::setVolumeFactor(double factor)
{
    if (qFuzzyCompare(factor, volumeFactor))
        return;

    volumeFactor = factor;
    if (!originalAudioData.isEmpty() && isPlaying()) {
        stop();
        start();
    } else {
        applyAmplification();
        emitVocalPreviewChunk();
    }
}

void AudioAmplifier::setAudioData(const QByteArray &data)
{
    QByteArray().swap(originalAudioData);
    originalAudioData = data;
    applyAmplification();
    emitVocalPreviewChunk();
}

void AudioAmplifier::setPlaybackVol(bool flag)
{
    playbackVol = flag;
    if (playbackSink)
        playbackSink->setVolume(flag ? 1.0 : 0.0);
}

void AudioAmplifier::applyAmplification()
{
    amplifiedAudioData.clear();
    amplifiedAudioData.reserve(originalAudioData.size());

    const char *data = originalAudioData.constData();
    for (int i = 0; i + 1 < originalAudioData.size(); i += 2) {
        const qint16 sample = static_cast<qint16>(
            static_cast<unsigned char>(data[i]) |
            (static_cast<qint16>(static_cast<unsigned char>(data[i + 1])) << 8));
        int amplifiedSample = static_cast<int>(sample * volumeFactor);
        amplifiedSample = std::min(std::max(amplifiedSample, -32768), 32767);
        amplifiedAudioData.append(static_cast<char>(amplifiedSample & 0xFF));
        amplifiedAudioData.append(static_cast<char>((amplifiedSample >> 8) & 0xFF));
    }

    if (amplifiedAudioData.isEmpty())
        qWarning() << "Amplified audio data is empty!";
}

bool AudioAmplifier::isPlaying() const
{
    return audioSink && audioSink->state() == QAudio::ActiveState;
}

bool AudioAmplifier::isPlayingPlayback() const
{
    return playbackSink && playbackSink->state() == QAudio::ActiveState;
}

void AudioAmplifier::seekTo(qint64 bytePos)
{
    playbackPosition = std::clamp(bytePos, qint64(0),
                                  qint64(amplifiedAudioData.size()));
    if (audioBuffer && audioBuffer->isOpen())
        audioBuffer->seek(playbackPosition);
    if (playbackBuffer && playbackBuffer->isOpen())
        playbackBuffer->seek(playbackPosition);
    emitVocalPreviewChunk();
}

void AudioAmplifier::rewind()
{
    if (audioBuffer && audioBuffer->isOpen())
        audioBuffer->seek(0);
    else
        qDebug() << "Vocals buffer is not open. Cannot rewind.";

    if (playbackBuffer && playbackBuffer->isOpen())
        playbackBuffer->seek(0);
    else
        qDebug() << "Playback buffer is not open. Cannot rewind.";

    playbackPosition = 0;
    emitVocalPreviewChunk();
}

void AudioAmplifier::setAudioOffset(qint64 offsetMs)
{
    byteOffset = audioFormat.bytesForDuration(offsetMs * 1000);
    qDebug() << "Setting audio offset:" << offsetMs << "ms (" << byteOffset << "bytes)";

    applyAmplification();

    if (byteOffset < 0) {
        const qint64 silenceBytes = -byteOffset;
        qDebug() << "Negative offset detected. Prepending" << silenceBytes << "bytes of silence.";
        amplifiedAudioData.prepend(QByteArray(silenceBytes, 0));
    } else {
        const qint64 trimBytes = byteOffset;
        qDebug() << "Positive offset detected. Trimming" << trimBytes << "bytes from the start.";
        if (trimBytes < amplifiedAudioData.size())
            amplifiedAudioData = amplifiedAudioData.mid(trimBytes);
        else {
            qWarning() << "Trim exceeds audio size. Clearing buffer.";
            amplifiedAudioData.clear();
        }
    }

    if (audioSink && audioSink->state() != QAudio::StoppedState) {
        audioSink->stop();
        qDebug() << "Stopped vocals.";
    }

    if (audioBuffer->isOpen())
        audioBuffer->close();
    audioBuffer->setData(amplifiedAudioData);
    audioBuffer->open(QIODevice::ReadOnly);
    audioBuffer->seek(playbackPosition);

    if (audioSink)
        audioSink->start(audioBuffer.data());

    emitVocalPreviewChunk();
}

void AudioAmplifier::handleStateChanged(QAudio::State newState)
{
    if (newState == QAudio::StoppedState) {
        if (audioSink && audioSink->error() != QAudio::NoError) {
            qWarning() << "Audio playback error:" << audioSink->error();
            return;
        }
    }
}

void AudioAmplifier::resetAudioComponents()
{
    if (audioSink && audioSink->state() == QAudio::ActiveState)
        audioSink->stop();
    if (playbackSink && playbackSink->state() == QAudio::ActiveState)
        playbackSink->stop();

    playbackSink.reset();
    audioSink.reset();

    playbackSink.reset(new QAudioSink(QMediaDevices::defaultAudioOutput(), audioFormat, this));
    audioSink.reset(new QAudioSink(QMediaDevices::defaultAudioOutput(), audioFormat, this));

    connect(audioSink.data(), &QAudioSink::stateChanged,
            this, &AudioAmplifier::handleStateChanged);
}

void AudioAmplifier::emitVocalPreviewChunk()
{
    if (amplifiedAudioData.isEmpty())
        return;

    const int bytesPerFrame = std::max(1, audioFormat.channelCount() * audioFormat.bytesPerSample());
    const int chunkBytes = bytesPerFrame * 2048;
    const qint64 maxStart = std::max<qint64>(0, amplifiedAudioData.size() - chunkBytes);
    const qint64 start = std::clamp<qint64>(playbackPosition, 0, maxStart);
    const QByteArray chunk = amplifiedAudioData.mid(start, chunkBytes);
    if (!chunk.isEmpty())
        emit vocalPreviewChunk(chunk, audioFormat);
}
