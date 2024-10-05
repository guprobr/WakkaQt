#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H

#include <QObject>
#include <QAudioSource>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QFile>

class AudioRecorder : public QObject
{
    Q_OBJECT

public:
    explicit AudioRecorder(QAudioDevice selectedDevice, QObject* parent = nullptr);
    ~AudioRecorder();

    void startRecording(const QString& outputFilePath);
    void stopRecording();
    bool isRecording() const; 
    void setSampleRate(int sampleRate);

private:
    void writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize, const QByteArray &pcmData);

    QAudioSource* m_audioSource;
    QAudioFormat m_audioFormat;
    QAudioDevice m_selectedDevice;
    QFile m_outputFile;
    bool m_isRecording;
};

#endif // AUDIORECORDER_H
