#include "audiovizmediaplayer.h"
#include "audiovisualizerwidget.h"

#include <QApplication>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QBuffer>
#include <QDir>
#include <QProcess>
#include <QThread>
#include <QDebug>

AudioVizMediaPlayer::AudioVizMediaPlayer(QMediaPlayer *m_player, AudioVisualizerWidget *visualizer, AudioVisualizerWidget *vizLeft, AudioVisualizerWidget *vizRight, QObject *parent)
    : QObject(parent)
    , m_mediaPlayer(m_player)
    , m_audioPosition(0)
    , m_audioTimer(new QTimer(this))
    , m_visualizer(visualizer)
    , m_visualizer_left(vizLeft)
    , m_visualizer_right(vizRight)
    , m_decodedAudioData(new QByteArray)  
    , m_framePositions(new QVector<qint64>)
{
    // Connect signals and slots
    connect(m_audioTimer, &QTimer::timeout, this, &AudioVizMediaPlayer::updateVisualizer);

    // Set a default audio format
    m_audioFormat.setSampleRate(44100);
    m_audioFormat.setChannelCount(1);
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);
}

AudioVizMediaPlayer::~AudioVizMediaPlayer()
{
    if (m_audioTimer) {
        m_audioTimer->stop();
        m_audioTimer->deleteLater();
    }

    m_visualizer->clear();
    m_visualizer_left->clear();
    m_visualizer_right->clear();
    m_decodedAudioData->detach();
    m_decodedAudioData->clear(); // Clear the audio data buffer
    m_framePositions->detach();
    m_framePositions->clear();

    m_decodedAudioData.reset();
    m_framePositions.reset();

    disconnect(m_audioTimer, &QTimer::timeout, this, &AudioVizMediaPlayer::updateVisualizer);
}

void AudioVizMediaPlayer::setMedia(const QString &source)
{
    m_mediaSource = source;

    //m_mediaPlayer->setSource(QUrl());

    // Reset the audio data
    m_decodedAudioData->detach();  
    m_decodedAudioData->clear();  // Clear the audio buffer
    m_framePositions->detach();    
    m_framePositions->clear();    // Clear frame positions

    QString audioFile = QDir::tempPath() + QDir::separator() + "WakkaQt_extracted_playback.wav";
    extractAudio(source, audioFile);
}

void AudioVizMediaPlayer::play()
{
    if (m_mediaPlayer->playbackState() == QMediaPlayer::StoppedState || m_mediaPlayer->playbackState() == QMediaPlayer::PausedState) {
        m_mediaPlayer->play();

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
    m_audioTimer->stop();

    if (m_mediaPlayer)
        m_mediaPlayer->stop();
    
    if (m_visualizer)
        m_visualizer->clear();  // Clear visualizations

    if (m_visualizer_left)
        m_visualizer_left->clear();  // Clear visualizations
    
    if (m_visualizer_right)
        m_visualizer_right->clear();  // Clear visualizations
}

void AudioVizMediaPlayer::seek(qint64 position)
{
    if (!m_mediaPlayer) {
        qWarning() << "Media player is null";
        return;
    }

    // Ensure position is within valid range
    if (position < 0 || position > m_mediaPlayer->duration()) {
        qWarning() << "Seek position out of bounds:" << position;
        return;
    }

    // Clear the visualizer before updating the new position
    m_visualizer->clear();
    m_visualizer_left->clear();
    m_visualizer_right->clear();

    // Calculate the frame position corresponding to the seek position
    qint64 targetFrame = position * m_framePositions->size() / m_mediaPlayer->duration();
    qint64 closestFramePos = findClosestFramePosition(targetFrame);

    // Ensure closestFramePos is within valid range
    if (closestFramePos < 0 || closestFramePos >= m_decodedAudioData->size()) {
        qWarning() << "Calculated audio position out of bounds:" << closestFramePos;
        return;
    }

    if (!m_audioTimer->isActive()) {
            m_audioTimer->start(100);  // Start the timer to update the visualizer, if needed
        }

    // Update media player position
    m_mediaPlayer->setPosition(position);

    // Update the audio position to the closest frame position
    m_audioPosition = closestFramePos;

    // Log the updated information for debugging
    qDebug() << "Seeking to media position:" << position 
             << "with closest frame position:" << closestFramePos;

    // Update visualizer to match the new position
    updateVisualizer();
}


qint64 AudioVizMediaPlayer::findClosestFramePosition(qint64 targetFrame)
{
    if (m_framePositions->isEmpty()) {
        return 0;  // Return the start if positions are empty
    }

    if (targetFrame <= 0) {
        return m_framePositions->first();  // Return first frame
    }

    if (targetFrame >= m_framePositions->size() - 1) {
        return m_framePositions->last();  // Return last frame
    }

    // Locate nearest frames
    qint64 frame1 = targetFrame;
    qint64 frame2 = frame1 + 1;

    if (frame2 >= m_framePositions->size()) {
        frame2 = m_framePositions->size() - 1;  // Ensure frame2 is within bounds
    }

    qint64 bytePos1 = m_framePositions->at(frame1);
    qint64 bytePos2 = m_framePositions->at(frame2);

    // Calculate interpolation for smoother seeking, avoid any divide-by-zero errors
    if (frame2 == frame1 || bytePos2 == bytePos1) {
        return bytePos1;  // Return exact position if frames are equal
    }

    double ratio = static_cast<double>(targetFrame - frame1) / (frame2 - frame1);
    return bytePos1 + static_cast<qint64>(ratio * (bytePos2 - bytePos1));
}


void AudioVizMediaPlayer::updateVisualizer()
{
    if (!m_visualizer || !m_visualizer_left || !m_visualizer_right || m_decodedAudioData->isEmpty()) {
        return;
    }

    // Ensure m_audioPosition is within valid bounds
    if (m_audioPosition < 0 || m_audioPosition >= m_decodedAudioData->size()) {
        m_audioPosition = 0;  // Reset audio position if out of bounds
        m_visualizer->clear();  // Clear visualizations when out of bounds
        m_visualizer_left->clear(); 
        m_visualizer_right->clear(); 
        return;
    }

    const qint64 bytesPerFrame = m_audioFormat.bytesForDuration(100000);  // 100ms worth of audio
    qint64 bytesToVisualize = qMin(bytesPerFrame, m_decodedAudioData->size() - m_audioPosition);

    if (bytesToVisualize > 0) {
        QByteArray audioChunk = m_decodedAudioData->mid(m_audioPosition, bytesToVisualize);
        m_visualizer->updateVisualization(audioChunk, m_audioFormat);  // Send data to visualizer
        m_visualizer_left->updateVisualization(audioChunk, m_audioFormat);  
        m_visualizer_right->updateVisualization(audioChunk, m_audioFormat);  


        m_audioPosition += bytesToVisualize;

        // Ensure audioPosition doesn't exceed the audio data size
        if (m_audioPosition >= m_decodedAudioData->size()) {
            m_audioPosition = 0;  // Reset if we reach the end
            m_audioTimer->stop();
        }
    } else {
        m_visualizer->clear();  // Clear visualization if no data to visualize
        m_visualizer_left->clear();
        m_visualizer_right->clear();
    }
}


void AudioVizMediaPlayer::extractAudio(const QString &source, const QString &outputFile)
{
    QThread* ffmpegThread = new QThread(this);  // Thread owned by the current class
    QProcess* ffmpegProcess = new QProcess();   // Process owned by the thread

    ffmpegProcess->moveToThread(ffmpegThread);

    connect(ffmpegProcess, &QProcess::finished,
            this, [ffmpegProcess, outputFile, source, this]() {
                QFile file(outputFile);
                if (!file.exists()) {
                    qWarning() << "Audio Visualizer audio file was not created.";
                } else {
                    emit ffmpegExtractionFinished(outputFile, source);
                }

                ffmpegProcess->deleteLater();  // Clean up the process safely
            });

    connect(ffmpegProcess, &QProcess::errorOccurred, this, [ffmpegProcess]() {
        qWarning() << "FFmpeg process error occurred. Cleaning up.";
        ffmpegProcess->deleteLater();  // Cleanup on error
    });

    connect(ffmpegThread, &QThread::finished, [ffmpegThread]() {
        ffmpegThread->deleteLater();
    });

    connect(this, &AudioVizMediaPlayer::ffmpegExtractionFinished, this, &AudioVizMediaPlayer::loadAudioData, Qt::UniqueConnection);

    connect(ffmpegThread, &QThread::started, [ffmpegProcess, source, outputFile]() {
        QStringList ffmpegArgs;
        ffmpegArgs << "-y" << "-i" << source 
                    << "-vn" << "-ac" << "1" 
                    << "-acodec" << "pcm_s16le" << "-ar" << "44100" << outputFile;

        ffmpegProcess->start("ffmpeg", ffmpegArgs);
        if (!ffmpegProcess->waitForStarted()) {
            qWarning() << "Failed to start FFmpeg process for Audio Visualizer.";
        }
    });

    connect(ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [ffmpegThread]() {
                if (ffmpegThread->isRunning()) {
                    ffmpegThread->quit();
                    ffmpegThread->wait();
                }
            });

    ffmpegThread->start();
}

void AudioVizMediaPlayer::loadAudioData(const QString &audioFile, const QString &sourceFile)
{
    QFile file(audioFile);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open audio file for reading.";
        return;
    }

    *m_decodedAudioData = file.readAll();  // Read all audio data
    file.close();

    m_framePositions->clear();  // Clear any previous data
    for (int i = 0; i < m_decodedAudioData->size(); i += m_audioFormat.bytesForDuration(100000)) {
        m_framePositions->append(i);  // Append the byte position for visualization
    }

    // Now that everything is ready, load the true media player
    m_mediaPlayer->setSource(QUrl::fromLocalFile(sourceFile));

}
