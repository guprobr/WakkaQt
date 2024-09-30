#ifndef AUDIOAMPLIFIER_H
#define AUDIOAMPLIFIER_H

#include <QObject>
#include <QAudio>
#include <QAudioFormat>
#include <QBuffer>
#include <QScopedPointer>
#include <QByteArray>
#include <QTimer>

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
    bool isPlaying() const;  
    void rewind();
    void seekForward();
    void seekBackward();

    void resetAudioComponents(); 

private:
    void applyAmplification();  
    void checkBufferState();  
    void handleStateChanged(QAudio::State newState); 

    QAudioFormat audioFormat;  
    QScopedPointer<QAudioSink> audioSink;  
    QScopedPointer<QTimer> dataPushTimer; 
    QScopedPointer<QBuffer> audioBuffer;  

    QByteArray originalAudioData;  
    QByteArray amplifiedAudioData;  

    double volumeFactor;  
    qint64 playbackPosition;  
};

#endif // AUDIOAMPLIFIER_H
