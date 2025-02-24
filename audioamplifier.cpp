#include "audioamplifier.h"
#include "complexes.h"

#include <QDebug>
#include <QDir>
#include <QTime>
#include <QTimer>
#include <QAudioSink>
#include <QMediaDevices>
#include <cmath>  // Amplification logic

AudioAmplifier::AudioAmplifier(const QAudioFormat &format, QObject *parent)
    : QObject(parent),
      audioFormat(format),
      volumeFactor(1.0),
      playbackPosition(0)
{
    // Initialize audio sinks
    playbackSink.reset(new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this));
    audioSink.reset(new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this));
    connect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);

    // Initialize QBuffer
    audioBuffer.reset(new QBuffer(new QByteArray()));  // QBuffer owns QByteArray
    playbackBuffer.reset(new QBuffer(new QByteArray()));

    playbackFile.setFileName(extractedTmpPlayback);
    if (playbackFile.isOpen())
        playbackFile.close();
    if (!playbackFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open playback file!";
        return;
    }
    playbackData = playbackFile.readAll(); // Read all data

    // Initialize timer for probing end of stream
    dataPushTimer.reset(new QTimer(this));
    connect(dataPushTimer.data(), &QTimer::timeout, this, &AudioAmplifier::checkBufferState);
}

AudioAmplifier::~AudioAmplifier() {
    stop();

    if (audioBuffer && audioBuffer->isOpen()) {
        audioBuffer->close();
    }
    if (playbackBuffer && playbackBuffer->isOpen()) {
        playbackBuffer->close();
    }

    // Release QByteArrays
    if (audioBuffer) {
        audioBuffer->setData(QByteArray());
    }
    if (playbackBuffer) {
        playbackBuffer->setData(QByteArray());
    }

    dataPushTimer->stop();

    playbackSink.reset();
    audioSink.reset();
    audioBuffer.reset();
    playbackBuffer.reset();

    QObject::disconnect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);
    QObject::disconnect(dataPushTimer.data(), &QTimer::timeout, this, &AudioAmplifier::checkBufferState);

}

QString AudioAmplifier::checkBufferState() {
    if (!audioBuffer->isOpen() || !audioSink || audioSink->isNull()) return ".. .Encoding. ..";

    playbackPosition = audioBuffer->pos();

    // Total duration based on the original audio size
    qint64 totalDuration = originalAudioData.size() * 1000000 / (audioSink->format().sampleRate() * 
                           audioSink->format().channelCount() * audioSink->format().bytesPerSample());

    // Convert playbackPosition from bytes to microseconds
    qint64 playbackPositionUSecs = playbackPosition * 1000000 / (audioSink->format().sampleRate() * 
                              audioSink->format().channelCount() * audioSink->format().bytesPerSample());
    
    // Correct the processed duration to include the playback position (both in microseconds now)
    qint64 processedDuration = playbackPositionUSecs; // + audioSink->processedUSecs();
    
    qint64 threshold = 500000;  // stop 500 ms before the end

    // Verify buffer size!
    if (audioBuffer->bytesAvailable() < 1024) {  
        qWarning() << "Audio buffer too small during playback. Restarting playback.";
        stop();
        resetAudioComponents();
        rewind();
        start();
        rewind();
        return "NaN";
    }

    // When close to the end, restart playback
    if (processedDuration >= totalDuration - threshold) {
        qWarning() << "Buffer near the end. Restarting playback.";
        stop();
        resetAudioComponents();
        rewind();
        start();
        rewind();
        return "NaN";
    }

    long long totalSeconds = processedDuration / 1000000; // Convert microseconds to seconds
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    QTime time(hours, minutes, seconds);
    return time.toString("hh:mm:ss");

}

void AudioAmplifier::start() {
    // Verify if there is original data to amplify
    if (!originalAudioData.isEmpty()) {
        setAudioOffset(audioFormat.durationForBytes(byteOffset)/1000);
        //applyAmplification();

        // Validate initial buffer size
        if (amplifiedAudioData.size() < 512) { 
            qWarning() << "Amplified buffer audio size is too small:" << amplifiedAudioData.size();
            return; 
        }

        stop();

        // Verify if buffer is open, then close before starting
        if (audioBuffer->isOpen()) {
            audioBuffer->close();
        }
        audioBuffer->setData(amplifiedAudioData); // Configure QBuffer data 
        audioBuffer->open(QIODevice::ReadOnly); // and open for reading
        audioBuffer->seek(playbackPosition);     // Resume prior position
        
        if (playbackBuffer->isOpen()) {
            playbackBuffer->close();
        }
        playbackBuffer->setData(playbackData);
        playbackBuffer->open(QIODevice::ReadOnly); // Open the buffer for reading
        playbackBuffer->seek(playbackPosition);

        playbackSink->start(playbackBuffer.data());
        playbackSink->setVolume(playbackVol);
        audioSink->start(audioBuffer.data()); // play amplified vocals

        dataPushTimer->start(25); // Probe buffer state paranoia style
        checkBufferState();

        qDebug() << "Start amplified vocals and backing track.";
    } else {
        qWarning() << "No audio data.";
    }
}

void AudioAmplifier::stop() {
    if (audioSink->state() != QAudio::State::StoppedState) {
        playbackPosition = audioBuffer->pos(); // Store current position before stopping
        audioSink->stop();  // Stop the audio sink
        qDebug() << "Stopped vocals.";
    }

    if (playbackSink->state() != QAudio::State::StoppedState) {
        playbackSink->stop();  // Stop the playback sink
        qDebug() << "Stopped backingtrack.";
    }

    // Close the buffer if playback is fully stopped
    if (audioBuffer->isOpen() && !isPlaying()) {
        audioBuffer->close();
        qDebug() << "Closed vocals buffer.";
    }
    if (playbackBuffer->isOpen() && !isPlayingPlayback()) {
        playbackBuffer->close();
        qDebug() << "Closed backing track buffer.";
    }

    dataPushTimer->stop();  // Stop the timer when playback stops
}

void AudioAmplifier::seekForward() {
    if (audioBuffer->bytesAvailable() > 514000) {
        //stop();
        //resetAudioComponents();
        playbackPosition = audioBuffer->pos() + 514000;
        audioBuffer->seek(playbackPosition);
        playbackBuffer->seek(playbackPosition);
        checkBufferState();
        //start();
    }  
}

void AudioAmplifier::seekBackward() {
    if ( audioBuffer->pos() - 514000 > 0 ) {
        //stop();
        //resetAudioComponents();
        playbackPosition = audioBuffer->pos() - 514000;
        audioBuffer->seek(playbackPosition);
        playbackBuffer->seek(playbackPosition);
        checkBufferState();
        //start();
    }
}

void AudioAmplifier::setVolumeFactor(double factor) {
    if (factor != volumeFactor) {
        volumeFactor = factor;

        // Only restart playback if there is audio data
        if (!originalAudioData.isEmpty() && isPlaying()) {
            stop();
            //resetAudioComponents();
            start();
        }
    }
}

void AudioAmplifier::setAudioData(const QByteArray &data) {
    // Release old data
    QByteArray().swap(originalAudioData);
    originalAudioData = data;  // Store the new audio data
}

 void AudioAmplifier::setPlaybackVol(bool flag) {
    playbackSink->setVolume(flag);
    playbackVol = flag;
 }

void AudioAmplifier::applyAmplification() {
    
    amplifiedAudioData.clear();
    amplifiedAudioData.reserve(originalAudioData.size());

    const char *data = originalAudioData.constData();
    for (int i = 0; i < originalAudioData.size(); i += 2) {
        // Convert two bytes to qint16 (16-bit audio)
        qint16 sample = static_cast<qint16>(static_cast<unsigned char>(data[i]) | 
                                            (static_cast<unsigned char>(data[i + 1]) << 8));

        // Amplify and clip to avoid overflow
        int amplifiedSample = static_cast<int>(sample * volumeFactor);
        amplifiedSample = std::min(std::max(amplifiedSample, -32768), 32767);

        // Rewrite amplified sample as little-endian
        amplifiedAudioData.append(static_cast<char>(amplifiedSample & 0xFF));
        amplifiedAudioData.append(static_cast<char>((amplifiedSample >> 8) & 0xFF));
    }

    if (amplifiedAudioData.isEmpty()) {
        qWarning() << "Amplified audio data is empty!";
    }
}

bool AudioAmplifier::isPlaying() const {
    return audioSink->state() == QAudio::ActiveState; // Check if audio is currently playing
}

bool AudioAmplifier::isPlayingPlayback() const {
    return playbackSink->state() == QAudio::ActiveState; // Check if audio is currently playing
}

void AudioAmplifier::rewind() {
    if (audioBuffer->isOpen()) {
        audioBuffer->seek(0);  // Seek the buffer to the start
    } else {
        qDebug() << "Vocals buffer is not open. Cannot rewind.";
    }
    if (playbackBuffer->isOpen())
        playbackBuffer->seek(0);
    else
        qDebug() << "Playback buffer is not open. Cannot rewind.";

    playbackPosition = 0;  // Reset playback position
}

void AudioAmplifier::setAudioOffset(qint64 offsetMs) {
    // Calculate the byte offset corresponding to the time offset
    byteOffset = audioFormat.bytesForDuration(offsetMs * 1000); // Convert ms to µs
    qDebug() << "Setting audio offset:" << offsetMs << "ms (" << byteOffset << "bytes)";

    // Restore the original audio data
    applyAmplification();
        
    if (byteOffset < 0) {
        // Negative offset: Prepend silence
        qint64 silenceBytes = -byteOffset;

        qDebug() << "Negative offset detected. Prepending" << silenceBytes << "bytes of silence.";

        // Create a buffer with silence
        QByteArray silence(silenceBytes, 0);

        // Prepend silence to the audio data
        amplifiedAudioData.prepend(silence);
    
        //playbackPosition = 0;
    } else {
        // Positive offset: Trim the audio data
        qint64 trimBytes = byteOffset;

        qDebug() << "Positive offset detected. Trimming" << trimBytes << "bytes from the start.";

        if (trimBytes < amplifiedAudioData.size()) {
            amplifiedAudioData = amplifiedAudioData.mid(trimBytes);
        } else {
            qWarning() << "Trim exceeds audio size. Clearing buffer.";
            amplifiedAudioData.clear();
        }
        
        //playbackPosition = 0;
    }

    if (audioSink->state() != QAudio::State::StoppedState) {
        audioSink->stop();  // Stop the audio sink
        qDebug() << "Stopped vocals.";
    }
    // Verify if buffer is open, then close before starting
    if (audioBuffer->isOpen()) {
        audioBuffer->close();
    }
    audioBuffer->setData(amplifiedAudioData); // Configure with new QBuffer data 
    audioBuffer->open(QIODevice::ReadOnly); // and open for reading
    audioBuffer->seek(playbackPosition);     // Resume prior position
    audioSink->start(audioBuffer.data()); // play amplified vocals

}


void AudioAmplifier::handleStateChanged(QAudio::State newState) {
    if (newState == QAudio::StoppedState) {
        if (audioSink && audioSink->error() != QAudio::NoError) {
            qWarning() << "Audio playback error:" << audioSink->error();
            return;
        }
    }
}

void AudioAmplifier::resetAudioComponents() {
    // Stop any active sinks
    if (audioSink && audioSink->state() == QAudio::ActiveState) {
        audioSink->stop();
    }
    if (playbackSink && playbackSink->state() == QAudio::ActiveState) {
        playbackSink->stop();
    }

    // Properly release old sinks before creating new ones
    playbackSink.reset();
    audioSink.reset();

    // Recreate sinks
    playbackSink.reset(new QAudioSink(QMediaDevices::defaultAudioOutput(), audioFormat, this));
    audioSink.reset(new QAudioSink(QMediaDevices::defaultAudioOutput(), audioFormat, this));

    // Reconnect signals
    connect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);
}
