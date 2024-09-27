#ifndef AUDIOAMPLIFIER_H
#define AUDIOAMPLIFIER_H

#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer> // Include QBuffer
#include <QIODevice>
#include <QByteArray>

class AudioAmplifier : public QObject {
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

private:
    void applyAmplification();
    void handleStateChanged(QAudio::State newState);
    void checkBufferState();

    QAudioFormat audioFormat;
    QAudioSink *audioSink;
    QBuffer audioBuffer; // Use QBuffer to handle audio data
    QByteArray originalAudioData;
    QByteArray amplifiedAudioData;
    double volumeFactor;
    qint64 playbackPosition; // Store the playback position
};

#endif // AUDIOAMPLIFIER_H
