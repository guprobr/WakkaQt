#include "audioamplifier.h"

#include <QDebug>
#include <QTimer>
#include <QAudioSink>
#include <cmath>  // amplification logic

AudioAmplifier::AudioAmplifier(const QAudioFormat &format, QObject *parent)
    : QObject(parent),
      audioFormat(format),
      audioSink(new QAudioSink(format, this)),
      volumeFactor(1.0),
      playbackPosition(0) // Initialize playback position
{
    connect(audioSink, &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);

    audioBuffer.setBuffer(new QByteArray()); // Initialize QBuffer with an empty QByteArray
}

AudioAmplifier::~AudioAmplifier() {

    stop();  // Ensure audio stops and resources are cleaned up
    delete audioSink;

}

void AudioAmplifier::start() {
    // Reset buffer for new playback
    if (!originalAudioData.isEmpty()) {
        applyAmplification();
        
        // Ensure the audio buffer is correctly reset and opened before playback
        if (audioBuffer.isOpen()) {
            audioBuffer.close();  // Close the buffer if it's still open
        }

        // Set the data and open the buffer
        audioBuffer.setData(amplifiedAudioData); // Set the data for QBuffer
        audioBuffer.open(QIODevice::ReadOnly); // Open the buffer for reading
        
        // Seek to the stored playback position or start from the beginning
        audioBuffer.seek(playbackPosition);

        audioSink->start(&audioBuffer); // Start the audio output with the buffer

        // Create a timer to periodically check the buffer state
        QTimer* bufferCheckTimer = new QTimer(this);
        connect(bufferCheckTimer, &QTimer::timeout, this, &AudioAmplifier::checkBufferState);
        bufferCheckTimer->start(50);  // Check every 50 ms

        qDebug() << "Started playback with amplified audio data.";
    } else {
        qWarning() << "No audio data to play.";
    }
}

void AudioAmplifier::checkBufferState() {
    qint64 totalDuration = audioBuffer.size() * 1000000 / (audioSink->format().sampleRate() * audioSink->format().channelCount() * audioSink->format().bytesPerSample()); // Total duration in microseconds
    qint64 processedDuration = audioSink->processedUSecs();  // How much has been processed

    qint64 threshold = 800000;  // Stop 800 ms (800,000 us) before the end

    // Check if we're near the end of the buffer
    if (processedDuration >= totalDuration - threshold) {
        qDebug() << "Buffer is almost finished. Restarting audio.";
        stop();
        rewind();
        start();
    }
}

void AudioAmplifier::stop() {
    // Stop the audio playback
    if (audioSink->state() == QAudio::ActiveState) {
        playbackPosition = audioBuffer.pos(); // Store current position before stopping

        if (audioSink && audioSink->state() != QAudio::StoppedState) 
            audioSink->stop(); 
        
        qDebug() << "Stopped playback.";
    }

    // Close the buffer if playback is fully stopped
    if (audioBuffer.isOpen() && !isPlaying()) {
        audioBuffer.close();
        qDebug() << "Closed audio buffer.";
    }
}

void AudioAmplifier::setVolumeFactor(double factor) {
    if (factor != volumeFactor) { // Only amplify if the factor has changed
        volumeFactor = factor;

        // Only restart playback if there is audio data
        if (!originalAudioData.isEmpty() && isPlaying()) {
            stop(); // Stop current playback
            start(); // Start playback with new volume
        }
    }
}

void AudioAmplifier::setAudioData(const QByteArray &data) {
    if (data != originalAudioData) { // Check if the new data is different
        originalAudioData = data;  // Store the new audio data
    } else {
        qDebug() << "New audio data is the same as the original; skipping amplification.";
    }
}

void AudioAmplifier::applyAmplification() {
    amplifiedAudioData.clear();
    amplifiedAudioData.reserve(originalAudioData.size());

    const char *data = originalAudioData.constData();
    for (int i = 0; i < originalAudioData.size(); i += 2) {
        // Assuming 16-bit audio (2 bytes per sample)
        qint16 sample = static_cast<qint16>((data[i + 1] << 8) | (data[i] & 0xFF));

        // Amplify sample and clip it to avoid overflow
        int amplifiedSample = static_cast<int>(sample * volumeFactor);
        amplifiedSample = std::min(std::max(amplifiedSample, -32768), 32767);

        // Write amplified sample back
        amplifiedAudioData.append(static_cast<char>(amplifiedSample & 0xFF));
        amplifiedAudioData.append(static_cast<char>((amplifiedSample >> 8) & 0xFF));
    }

    // Debug information
    ///qDebug() << "Amplified audio data size:" << amplifiedAudioData.size();
    if (amplifiedAudioData.isEmpty()) {
        qWarning() << "Amplified audio data is empty!";
    }
}

bool AudioAmplifier::isPlaying() const {
    return audioSink->state() == QAudio::ActiveState; // Check if audio is currently playing
}

void AudioAmplifier::rewind() {
    if (audioBuffer.isOpen()) {
        audioBuffer.seek(0);  // Seek the buffer to the start
    } else {
        qWarning() << "Audio buffer is not open. Cannot rewind.";
    }
    playbackPosition = 0; // Reset playback position
}

void AudioAmplifier::handleStateChanged(QAudio::State newState) {
    if (newState == QAudio::IdleState) {
        qDebug() << "Playback finished. Stopping and resetting...";
        stop();  // Ensure no more data is written to the stream
        playbackPosition = 0;  // Reset playback position
        qDebug() << "Playback finished.";
        
        // Optional: Reinitialize the audio sink if needed
        if (audioSink) {
            audioSink->reset(); // Or close and reinitialize if necessary
        }
    } 
    else if (newState == QAudio::StoppedState) {
        if (audioSink && audioSink->error() != QAudio::NoError) {
            // Handle errors that occur when audio is stopped
            qWarning() << "Audio playback error:" << audioSink->error();
        }
    }
}

