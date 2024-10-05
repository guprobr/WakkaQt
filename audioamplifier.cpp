#include "audioamplifier.h"

#include <QDebug>
#include <QTimer>
#include <QAudioSink>
#include <cmath>  // Amplification logic

AudioAmplifier::AudioAmplifier(const QAudioFormat &format, QObject *parent)
    : QObject(parent),
      audioFormat(format),
      volumeFactor(1.0),
      playbackPosition(0)
{
    // Initialize audio sink with scoped pointer
    audioSink.reset(new QAudioSink(format, this));
    connect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);

    // Initialize QBuffer
    audioBuffer.reset(new QBuffer());
    audioBuffer->setBuffer(new QByteArray());

    // Initialize timer for probing end of stream
    dataPushTimer.reset(new QTimer(this));
    connect(dataPushTimer.data(), &QTimer::timeout, this, &AudioAmplifier::checkBufferState);
}

AudioAmplifier::~AudioAmplifier() {
    stop();  // Ensure audio stops and resources are cleaned up
}

void AudioAmplifier::checkBufferState() {
    if (!audioBuffer->isOpen() || !audioSink || audioSink->isNull()) return;

    // Total duration based on the original audio size
    qint64 totalDuration = originalAudioData.size() * 1000000 / (audioSink->format().sampleRate() * 
                           audioSink->format().channelCount() * audioSink->format().bytesPerSample());

    // Convert playbackPosition from bytes to microseconds
    qint64 playbackPositionUSecs = playbackPosition * 1000000 / (audioSink->format().sampleRate() * 
                              audioSink->format().channelCount() * audioSink->format().bytesPerSample());
    
    // Correct the processed duration to include the playback position (both in microseconds now)
    qint64 processedDuration = playbackPositionUSecs + audioSink->processedUSecs();
    
    qint64 threshold = 500000;  // stop 500 ms before the end

    // Verify buffer size!
    if (audioBuffer->bytesAvailable() < 1024) {  
        qWarning() << "Audio buffer too small during playback. Restarting playback.";
        stop();
        resetAudioComponents();
        rewind();
        start();
        return;
    }

    // When close to the end, restart playback
    if (processedDuration >= totalDuration - threshold) {
        qWarning() << "Buffer near the end. Restarting playback.";
        stop();
        resetAudioComponents();
        rewind();
        start();
        return;
    }
}

void AudioAmplifier::start() {
    // Verify if there is original data to amplify
    if (!originalAudioData.isEmpty()) {
        applyAmplification();

        // Validate initial buffer size
        if (amplifiedAudioData.size() < 512) { 
            qWarning() << "Amplified buffer audio size is too small:" << amplifiedAudioData.size();
            return; 
        }

        // Verify if buffer is open, then close before starting
        if (audioBuffer->isOpen()) {
            audioBuffer->close();
        }

        // Configure QBuffer data and open for reading
        audioBuffer->setData(amplifiedAudioData);
        audioBuffer->open(QIODevice::ReadOnly);

        // Resume prior position
        audioBuffer->seek(playbackPosition);

        audioSink->start(audioBuffer.data()); // playback
        dataPushTimer->start(25); // Probe buffer state paranoia style
        checkBufferState();

        qDebug() << "Start amplified audio playback.";
    } else {
        qWarning() << "No audio data.";
    }
}

void AudioAmplifier::stop() {
    if (audioSink->state() == QAudio::ActiveState) {
        playbackPosition = audioBuffer->pos(); // Store current position before stopping
        audioSink->stop();  // Stop the audio sink
        qDebug() << "Stopped playback.";
    }

    // Close the buffer if playback is fully stopped
    if (audioBuffer->isOpen() && !isPlaying()) {
        audioBuffer->close();
        qDebug() << "Closed audio buffer.";
    }

    dataPushTimer->stop();  // Stop the timer when playback stops
}

void AudioAmplifier::seekForward() {
    if (audioBuffer->bytesAvailable() > 102400) {
        stop();
        resetAudioComponents();
        playbackPosition += 102400;
        start();
    }  
}

void AudioAmplifier::seekBackward() {
    if ( audioBuffer->pos() - 102400 > 0 ) {
        stop();
        resetAudioComponents();
        playbackPosition -= 102400;
        start();
    }
}

void AudioAmplifier::setVolumeFactor(double factor) {
    if (factor != volumeFactor) {
        volumeFactor = factor;

        // Only restart playback if there is audio data
        if (!originalAudioData.isEmpty() && isPlaying()) {
            stop();
            resetAudioComponents();
            start();
        }
    }
}

void AudioAmplifier::setAudioData(const QByteArray &data) {
    originalAudioData = data;  // Store the new audio data
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

void AudioAmplifier::rewind() {
    if (audioBuffer->isOpen()) {
        audioBuffer->seek(0);  // Seek the buffer to the start
    } else {
        qDebug() << "Audio buffer is not open. Cannot rewind.";
    }
    playbackPosition = 0;  // Reset playback position
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
    if (audioSink && audioSink->state() == QAudio::ActiveState) {
        audioSink->stop();
    }

    if (audioBuffer && audioBuffer->isOpen()) {
        audioBuffer->close();
    }

    audioBuffer.reset(new QBuffer(this));
    audioBuffer->setData(amplifiedAudioData);
    audioBuffer->open(QIODevice::ReadOnly);

    audioSink.reset(new QAudioSink(audioFormat, this));
    connect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);
    //audioSink->setBufferSize(8192); 

}
