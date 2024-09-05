#ifndef SNDWIDGET_H
#define SNDWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QAudioSource>
#include <QAudioBuffer>
#include <QBuffer>
#include <QMutex>
#include <QAudioDevice>

class SndWidget : public QWidget {
    Q_OBJECT

public:
    explicit SndWidget(QWidget *parent = nullptr);
    ~SndWidget();

    void setInputDevice(const QAudioDevice &device);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateWaveform();
    void onAudioStateChanged(QAudio::State state);

private:
    void processBuffer();

    QTimer *timer;

    QAudioSource *audioSource;
    QBuffer *audioBuffer;
    QAudioFormat format;

    QVector<qint16> audioData;
    QVector<float> waveformBuffer;
    QMutex bufferMutex;
    
};

#endif // SNDWIDGET_H
