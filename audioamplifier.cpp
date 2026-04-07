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
    playbackData = playbackFile.readAll();

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

void AudioAmplifier::seekForward()
{
    if (!audioBuffer || !playbackBuffer)
        return;
    if (audioBuffer->bytesAvailable() > 514000) {
        playbackPosition = audioBuffer->pos() + 514000;
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
    if (audioBuffer->pos() - 514000 > 0) {
        playbackPosition = audioBuffer->pos() - 514000;
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
