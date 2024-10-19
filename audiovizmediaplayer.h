#ifndef AUDIOVIZMEDIAPLAYER_H
#define AUDIOVIZMEDIAPLAYER_H

#include <QObject>
#include <QMediaPlayer>
#include <QAudioFormat>
#include <QVector>
#include <QTimer>
#include <QScopedPointer>
#include <QByteArray>

class AudioVisualizerWidget; // Forward declaration

class AudioVizMediaPlayer : public QObject
{
    Q_OBJECT

public:
    explicit AudioVizMediaPlayer(QMediaPlayer *m_player, AudioVisualizerWidget *vizLeft, AudioVisualizerWidget *vizRight, QObject *parent = nullptr);
    ~AudioVizMediaPlayer();

    void setMedia(const QString &source);
    void play();
    void pause();
    void stop();
    void mute(bool toggle);
    void seek(qint64 position);

signals:
    void ffmpegExtractionFinished(const QString &audioFile, const QString &sourceFile);

private:
    void updateVisualizer();
    void extractAudio(const QString &source, const QString &outputFile);
    void loadAudioData(const QString &audioFile, const QString &sourceFile);
    qint64 findClosestFramePosition(qint64 targetFrame);

    QString m_mediaSource;

    QMediaPlayer *m_mediaPlayer;
    
    AudioVisualizerWidget *m_visualizer_left;
    AudioVisualizerWidget *m_visualizer_right;

    QAudioFormat m_audioFormat;
    qint64 m_audioPosition;

    QTimer *m_audioTimer;
    QScopedPointer<QByteArray> m_decodedAudioData; // Store extracted audio data
    QScopedPointer<QVector<qint64>> m_framePositions; // Store frame positions
};

#endif // AUDIOVIZMEDIAPLAYER_H
