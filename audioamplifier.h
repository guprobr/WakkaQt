#ifndef AUDIOAMPLIFIER_H
#define AUDIOAMPLIFIER_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QScopedPointer>
#include <QTimer>
#include <QString>

class AudioAmplifier : public QObject
{
    Q_OBJECT

public:
    explicit AudioAmplifier(const QAudioFormat &format, QObject *parent = nullptr);
    ~AudioAmplifier();

    void start();
    void stop();
    void setVolumeFactor(double factor);
    void setAudioData(const QByteArray &data);
    void setPlaybackVol(bool flag);
    bool isPlaying() const;
    bool isPlayingPlayback() const;
    void rewind();
    void seekForward();
    void seekBackward();
    void setAudioOffset(qint64 offset);
    QString checkBufferState();
    void resetAudioComponents();

signals:
    void vocalPreviewChunk(const QByteArray &audioData, const QAudioFormat &format);

private:
    void applyAmplification();
    void handleStateChanged(QAudio::State newState);
    void emitVocalPreviewChunk();

    QAudioFormat audioFormat;
    QScopedPointer<QAudioSink> audioSink;
    QScopedPointer<QAudioSink> playbackSink;
    QScopedPointer<QTimer> dataPushTimer;
    QScopedPointer<QBuffer> audioBuffer;
    QScopedPointer<QBuffer> playbackBuffer;
    QByteArray originalAudioData;
    QByteArray amplifiedAudioData;
    QByteArray playbackData;
    QFile playbackFile;
    double volumeFactor;
    qint64 playbackPosition;
    qint64 byteOffset = 0;
    bool playbackVol = true;
};

#endif // AUDIOAMPLIFIER_H
