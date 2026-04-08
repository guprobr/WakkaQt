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

signals:
    // Emitted every timer tick with the latest batch of PCM samples (mono, 44100 Hz, Int16).
    // Connect to PitchMonitorWidget for real-time pitch feedback during recording.
    void audioChunkReady(QVector<qint16> samples);

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
