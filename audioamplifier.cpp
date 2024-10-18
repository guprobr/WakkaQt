#include "audioamplifier.h"

#include <QDebug>
#include <QDir>
#include <QTime>
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
    playbackSink.reset(new QAudioSink(format, this));
    audioSink.reset(new QAudioSink(format, this));
    connect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);
    
    playbackFile.setFileName(QDir::tempPath() + QDir::separator() + "WakkaQt_extracted_playback.wav");

    // Initialize QBuffer
    audioBuffer.reset(new QBuffer());
    audioBuffer->setBuffer(new QByteArray());

    playbackBuffer.reset(new QBuffer());
    playbackBuffer->setBuffer(new QByteArray());

    // Initialize timer for probing end of stream
    dataPushTimer.reset(new QTimer(this));
    connect(dataPushTimer.data(), &QTimer::timeout, this, &AudioAmplifier::checkBufferState);
}

AudioAmplifier::~AudioAmplifier() {
    stop();  // Ensure audio stops and resources are cleaned up
    playbackBuffer->reset();
    playbackSink->reset();
}

QString AudioAmplifier::checkBufferState() {
    if (!audioBuffer->isOpen() || !audioSink || audioSink->isNull()) return "NaN";

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
        return "NaN";
    }

    // When close to the end, restart playback
    if (processedDuration >= totalDuration - threshold) {
        qWarning() << "Buffer near the end. Restarting playback.";
        stop();
        resetAudioComponents();
        rewind();
        start();
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

        if (playbackBuffer->isOpen()) {
            playbackBuffer->close();
        }

        playbackBuffer->setData(playbackData);
        playbackBuffer->open(QIODevice::ReadOnly); // Open the buffer for reading
        playbackBuffer->seek(playbackPosition);
        playbackSink->start(playbackBuffer.data());

        ////////////////////////
        // now the vocals
        audioSink->start(audioBuffer.data()); // play amplified vocals
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
    /*if (playbackBuffer->isOpen() && !isPlaying()) {
        playbackBuffer->close();
        qDebug() << "Closed audio buffer.";
    }*/

    dataPushTimer->stop();  // Stop the timer when playback stops
}

void AudioAmplifier::seekForward() {
    if (audioBuffer->bytesAvailable() > 514000) {
        stop();
        resetAudioComponents();
        playbackPosition += 514000;
        start();
    }  
}

void AudioAmplifier::seekBackward() {
    if ( audioBuffer->pos() - 514000 > 0 ) {
        stop();
        resetAudioComponents();
        playbackPosition -= 514000;
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

    // Open the playback extracted .WAV
    if ( playbackFile.isOpen() )
        playbackFile.close();

    if (!playbackFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open playback file!";
        return;
    }

    playbackData.clear();
    // Read the entire contents of the file into a QByteArray
    playbackData = playbackFile.readAll();
    

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
        playbackBuffer->seek(0);
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

    //if (playbackBuffer && playbackBuffer->isOpen()) {
    //    playbackBuffer->close();
    //}

    //playbackBuffer.reset(new QBuffer(this));
    //playbackBuffer->setData(playbackData);
    //playbackBuffer->open(QIODevice::ReadOnly);

    playbackSink.reset(new QAudioSink(audioFormat, this));
    audioSink.reset(new QAudioSink(audioFormat, this));
    connect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);
    
}
