#ifndef AUDIOVIZMEDIAPLAYER_H
#define AUDIOVIZMEDIAPLAYER_H

#include "audiovisualizerwidget.h"

#include <QObject>
#include <QMediaPlayer>
#include <QAudioDecoder>
#include <QBuffer>
#include <QProcess>
#include <QAudioFormat>
#include <QTimer>
#include <QVector>
#include <QString>
#include <QUrl>


class AudioVizMediaPlayer : public QObject
{
    Q_OBJECT

public:
    explicit AudioVizMediaPlayer(QMediaPlayer *m_player, AudioVisualizerWidget *visualizer, QObject *parent = nullptr);
    ~AudioVizMediaPlayer();

    void setMedia(const QString &source);
    void play();
    void pause();
    void stop();
    void seek(qint64 position);

private:
    QMediaPlayer *m_mediaPlayer; // Shadow QMediaPlayer
    QAudioDecoder *m_audioDecoder; // to decode extracted MP3 from medias
    QBuffer *m_audioBuffer;         
    QProcess *m_ffmpegProcess;
    QAudioFormat m_audioFormat;
    QByteArray m_decodedAudioData;
    qint64 m_audioPosition;
    QTimer *m_audioTimer;
    AudioVisualizerWidget *m_visualizer; // Pointer to the visualizer widget
    QVector<qint64> m_framePositions; // Store frame positions for seeking
    QString m_mediaSource;

    void updateVisualizer();
    qint64 findClosestFramePosition(qint64 targetFrame);
    void extractAudio(const QString &source, const QString &outputFile);
    void decodeAudio(const QString &audioFile);                         


private slots:
    void onAudioDecoderFinished();
    void onFfmpegFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onFfmpegError(QProcess::ProcessError error);
    void onAudioBufferReady();

signals:
    void visualizationDataUpdated(const QVector<qreal> &data);
    void ffmpegExtractionFinished(const QString &outputFile);
};

#endif // AUDIOVIZMEDIAPLAYER_H
