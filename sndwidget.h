#ifndef SNDWIDGET_H
#define SNDWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QAudioSource>
#include <QIODevice>
#include <QMutex>

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

    QAudioFormat format;
    QAudioSource *audioSource = nullptr;
    QIODevice    *m_pullDevice = nullptr;   // pull-mode device owned by audioSource

    QVector<qint16> audioData;
    QMutex bufferMutex;
    
};

#endif // SNDWIDGET_H
