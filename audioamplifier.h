#ifndef AUDIOAMPLIFIER_H
#define AUDIOAMPLIFIER_H

#include <QObject>
#include <QAudio>
#include <QAudioFormat>
#include <QBuffer>
#include <QScopedPointer>
#include <QByteArray>
#include <QTimer>
#include <QFile>

class QAudioSink;

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
    void rewind();
    void seekForward();
    void seekBackward();
    QString checkBufferState();

    void resetAudioComponents(); 

private:
    void applyAmplification();  
    void handleStateChanged(QAudio::State newState); 

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
};

#endif // AUDIOAMPLIFIER_H
