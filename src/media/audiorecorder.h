#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H

#include <QObject>
#include <QAudioSource>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QFile>
#include <QIODevice>
#include <QScopedPointer>

class AudioRecorder : public QObject
{
    Q_OBJECT

public:
    explicit AudioRecorder(QAudioDevice selectedDevice, QObject* parent = nullptr);
    ~AudioRecorder();

    void initialize();
    void startRecording(const QString& outputFilePath);
    void stopRecording();
    bool isRecording() const;
    void setSampleRate(int sampleRate);

    // Sync mark: every captured buffer is discarded (not written to disk)
    // until this is called, so the recorded file has zero pre-roll by
    // construction instead of needing a post-hoc trim. Call once, as close
    // as possible to the instant playback is confirmed to actually be
    // producing audio (see MainWindow::onPlayerPositionChanged).
    void armSync();
    // Duration, in ms, of audio discarded before armSync() was called.
    // Also doubles as the webcam pre-roll: the camera and mic are started
    // within a couple of lines of each other in MainWindow::startRecording(),
    // so this same value applies to trimming the webcam file at render time.
    qint64 preRollMs() const;

signals:
    void deviceLabelChanged(const QString &label);
    // Fired when the capture device fails *while actively recording* (e.g.
    // unplugged mid-take) — QAudioSource surfaces this as a stateChanged()
    // to StoppedState with a non-NoError code, not an exception, so nothing
    // upstream would otherwise notice the take has gone silent.
    void captureError(const QString &message);

private:
    QString sampleFormatToString(QAudioFormat::SampleFormat format);
    static QString audioErrorToString(QAudio::Error error);

    QAudioSource* m_audioSource;
    QAudioFormat m_audioFormat;
    QAudioDevice m_selectedDevice;
    QFile m_outputFile;
    bool m_isRecording;
    // Concrete type (GatedFileDevice) lives in audiorecorder.cpp — only
    // AudioRecorder itself needs to know about the gating behaviour.
    QScopedPointer<QIODevice> m_gate;
};

#endif // AUDIORECORDER_H
