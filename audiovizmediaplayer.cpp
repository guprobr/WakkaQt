#include "audiovizmediaplayer.h"
#include "audiovisualizerwidget.h"

#include <QApplication>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QBuffer>
#include <QDir>
#include <QThread>
#include <QDebug>

AudioVizMediaPlayer::AudioVizMediaPlayer(QMediaPlayer *m_player, AudioVisualizerWidget *visualizer, QObject *parent)
    : QObject(parent)
    , m_mediaPlayer(m_player)
    , m_audioDecoder(new QAudioDecoder(this))
    , m_audioBuffer(new QBuffer(this))
    , m_ffmpegProcess(new QProcess(this))
    , m_audioPosition(0)
    , m_audioTimer(new QTimer(this))
    , m_visualizer(visualizer)
{
    // Connect signals and slots
    connect(m_audioDecoder, &QAudioDecoder::finished, this, &AudioVizMediaPlayer::onAudioDecoderFinished);
    connect(m_ffmpegProcess, &QProcess::finished, this, &AudioVizMediaPlayer::onFfmpegFinished);
    connect(m_ffmpegProcess, &QProcess::errorOccurred, this, &AudioVizMediaPlayer::onFfmpegError);
    connect(m_audioTimer, &QTimer::timeout, this, &AudioVizMediaPlayer::updateVisualizer);  // Update visualizer

    // Set a default audio format
    m_audioFormat.setSampleRate(44100);
    m_audioFormat.setChannelCount(2);
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);
}

AudioVizMediaPlayer::~AudioVizMediaPlayer()
{
    // Stop playback and clean up resources
    //stop();  

    // Ensure the QAudioDecoder and QProcess are cleaned up
    if (m_audioDecoder) {
        m_audioDecoder->stop();
        m_audioDecoder->deleteLater(); 
    }

    if (m_ffmpegProcess) {
        m_ffmpegProcess->terminate(); 
        m_ffmpegProcess->deleteLater();
    }

    if (m_audioTimer) {
        m_audioTimer->stop();
        m_audioTimer->deleteLater();
    }

    m_decodedAudioData.clear(); // Clear the audio data buffer
    // No need to delete m_mediaPlayer or m_visualizer as they are managed elsewhere
}

void AudioVizMediaPlayer::setMedia(const QString &source)
{
    m_mediaSource = source;

    // Set the source for the QMediaPlayer here
    if (m_mediaPlayer) { 
        m_mediaPlayer->setSource(QUrl::fromLocalFile(source)); 
    }

    QString audioFile = QDir::tempPath() + QDir::separator() + "temp_audio.mp3";
    extractAudio(source, audioFile);
}

void AudioVizMediaPlayer::play()
{
    if (m_mediaPlayer->playbackState() == QMediaPlayer::StoppedState || m_mediaPlayer->playbackState() == QMediaPlayer::PausedState) {
        m_mediaPlayer->play();
        m_mediaPlayer->pause();
        if (!m_audioTimer->isActive()) {
            m_audioTimer->start(100);  // Start the timer to update the visualizer, if needed
        }
    }
}

void AudioVizMediaPlayer::pause()
{
    m_mediaPlayer->pause();
    m_audioTimer->stop();
}

void AudioVizMediaPlayer::stop()
{
    m_audioPosition = 0;
    m_decodedAudioData.clear();
    m_audioTimer->stop();

    if ( m_mediaPlayer )
        m_mediaPlayer->stop();
    
    if ( m_visualizer )
        m_visualizer->clear();  // Clear visualizations
}

void AudioVizMediaPlayer::seek(qint64 position)
{
    if (!m_mediaPlayer) {
        qWarning() << "Audio Visualizer Media player is null";
        return;
    }

    m_audioTimer->stop();
           
    // Calculate the correct target frame based on the sample rate and position in time
    qint64 targetFrame = (position / 1000.0) * m_audioFormat.sampleRate();
    qint64 closestBytePosition = findClosestFramePosition(targetFrame);

    // Ensure closestBytePosition is valid
    if (closestBytePosition < 0 || closestBytePosition >= m_decodedAudioData.size()) {
        qDebug() << "Invalid byte position during seek:" << closestBytePosition;
        return;
    }

    // Update the audio position to match the new seeked position
    m_audioPosition = closestBytePosition;

    // Optionally clear previous visualizations
    m_visualizer->clear();  // Clear visualizations when seeking!
    qDebug() << "Seeked to position:" << position << ", audio position:" << m_audioPosition;

    // Resume the timer if needed
    if (!m_audioTimer->isActive()) {
        m_audioTimer->start(100);  
    }

    // Seek media player to the requested position
    m_mediaPlayer->setPosition(position);
    updateVisualizer(); // trigger immediate repaint
}

qint64 AudioVizMediaPlayer::findClosestFramePosition(qint64 targetFrame)
{
    if (m_framePositions.isEmpty()) {
        return 0;  // or handle this case appropriately?
    }

    // Clamp within bounds
    if (targetFrame < 0) {
        return m_framePositions.first();
    }
    if (targetFrame >= m_framePositions.size()) {
        return m_framePositions.last();
    }

    // Find frame positions
    qint64 frame1 = targetFrame;
    qint64 frame2 = frame1 + 1;

    if (frame2 >= m_framePositions.size()) {
        frame2 = frame1;  // Clamp frame2 to frame1 if its out of bound
    }

    qint64 bytePos1 = m_framePositions[frame1];
    qint64 bytePos2 = m_framePositions[frame2];

    double ratio = (targetFrame - frame1) / static_cast<double>(frame2 - frame1);
    qint64 closestBytePosition = bytePos1 + static_cast<qint64>(ratio * (bytePos2 - bytePos1));

    return closestBytePosition;
}

void AudioVizMediaPlayer::updateVisualizer()
{
    if (m_audioPosition >= m_decodedAudioData.size()) {
        m_audioPosition = 0;  // Reset audio position
    }

    if (!m_visualizer || m_decodedAudioData.isEmpty()) {
        return;
    }

    // Calculate how much data we can visualize in this iteration
    const qint64 bytesPerFrame = m_audioFormat.bytesForDuration(100000);  // 100ms worth of audio
    qint64 bytesToVisualize = qMin(bytesPerFrame, m_decodedAudioData.size() - m_audioPosition);

    if (bytesToVisualize > 0) {
        QByteArray audioChunk = m_decodedAudioData.mid(m_audioPosition, bytesToVisualize);
        m_visualizer->updateVisualization(audioChunk, m_audioFormat);  // Send data to visualizer

        m_audioPosition += bytesToVisualize;
    }
}

void AudioVizMediaPlayer::extractAudio(const QString &source, const QString &outputFile)
{
    QThread* ffmpegThread = new QThread(this);  // Thread owned by the current class
    QProcess* ffmpegProcess = new QProcess();   // Process owned by the thread

    // worker thread
    ffmpegProcess->moveToThread(ffmpegThread);

    // Handle FFmpeg process finishing async
    connect(ffmpegProcess, &QProcess::finished,
            this, [ffmpegProcess, outputFile, this]() {
                QFile file(outputFile);
                if (!file.exists()) {
                    qWarning() << "Audio Visualizer audio file was not created.";
                } else {
                    emit ffmpegExtractionFinished(outputFile);
                }

                ffmpegProcess->deleteLater();  // Clean up the process safely
            });

    // Start the process in the thread
    connect(ffmpegThread, &QThread::started, [ffmpegProcess, source, outputFile]() {
        QStringList ffmpegArgs;
        ffmpegArgs << "-y" << "-i" << source << outputFile;

        ffmpegProcess->start("ffmpeg", ffmpegArgs);
        if (!ffmpegProcess->waitForStarted()) {
            qWarning() << "Failed to start FFmpeg process for Audio Visualizer.";
        }
    });

    // Handle thread completion and cleanup
    connect(ffmpegThread, &QThread::finished, [ffmpegThread]() {
        ffmpegThread->deleteLater(); 
    });

    // Start the worker thread
    ffmpegThread->start();

    // Handle when FFmpeg extraction finishes
    connect(this, &AudioVizMediaPlayer::ffmpegExtractionFinished, this, &AudioVizMediaPlayer::decodeAudio);

    // Ensure proper cleanup when switching videos or instantiating new class
    connect(ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [ffmpegThread, this]() {
                if (ffmpegThread->isRunning()) {
                    ffmpegThread->quit();  // Ask the thread to quit gracefully
                    ffmpegThread->wait();  // Ensure the thread has finished
                }
            });
}

void AudioVizMediaPlayer::decodeAudio(const QString &audioFile)
{
    m_audioDecoder->setSource(QUrl::fromLocalFile(audioFile));
    connect(m_audioDecoder, &QAudioDecoder::bufferReady, this, &AudioVizMediaPlayer::onAudioBufferReady);
    connect(m_audioDecoder, &QAudioDecoder::finished, this, &AudioVizMediaPlayer::onAudioDecoderFinished);

    m_audioDecoder->start();
}

void AudioVizMediaPlayer::onAudioDecoderFinished()
{
    qDebug() << "Audio Visualizer decoding finished";
    m_visualizer->clear();  // Clear visualizations
    m_audioPosition = 0;
    if (!m_audioTimer->isActive()) {
        m_audioTimer->start(100);
    }
    m_mediaPlayer->play();
    
}

void AudioVizMediaPlayer::onFfmpegFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "FFmpeg process finished with exit code:" << exitCode;
}

void AudioVizMediaPlayer::onFfmpegError(QProcess::ProcessError error)
{
    qWarning() << "Audio Visualizer FFmpeg process error occurred:" << error;
}

void AudioVizMediaPlayer::onAudioBufferReady()
{
    // Read the buffer from the audio decoder
    QAudioBuffer buffer = m_audioDecoder->read();  // Read the entire buffer

    // Ensure the buffer is valid
    if (buffer.isValid()) {
        // Create a QByteArray from the buffer data
        QByteArray data(reinterpret_cast<const char*>(buffer.data<char>()), buffer.byteCount());
        
        // Append to our audio data
        m_decodedAudioData.append(data);

        // Calculate the number of frames in the current buffer
        qint64 frameCount = buffer.frameCount(); // Get the number of frames from the buffer

        // Calculate the byte size per frame
        qint64 bytesPerFrame = m_audioFormat.bytesPerFrame();

        // Populate frame positions
        for (qint64 frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            qint64 bytePosition = m_decodedAudioData.size() - data.size() + (frameIndex * bytesPerFrame);
            m_framePositions.append(bytePosition);
        }

        //qDebug() << "Read audio buffer of size:" << data.size() << ", frame count:" << frameCount;
        
    } else {
        qDebug() << "Invalid audio buffer received.";
    }
}


