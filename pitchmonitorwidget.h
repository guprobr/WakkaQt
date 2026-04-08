#ifndef PITCHMONITORWIDGET_H
#define PITCHMONITORWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QMutex>
#include <QString>

// Real-time pitch monitor shown during recording.
// Receives audio chunks from SndWidget::audioChunkReady(), runs YIN pitch
// detection, and displays the current note, octave, and cents deviation.
class PitchMonitorWidget : public QWidget {
    Q_OBJECT

public:
    explicit PitchMonitorWidget(int sampleRate = 44100, QWidget *parent = nullptr);

    // Call to reset state when recording starts/stops
    void reset();

public slots:
    // Connected to SndWidget::audioChunkReady()
    void onAudioChunk(const QVector<qint16> &samples);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // YIN pitch detection on a mono Int16 buffer — returns Hz or 0 if unvoiced
    double detectPitchYIN(const QVector<double> &data) const;

    // Convert Hz to note name + octave + cents deviation
    struct NoteInfo { QString name; int octave; double cents; bool valid; };
    static NoteInfo hzToNote(double hz);

    int     m_sampleRate;
    QMutex  m_mutex;

    // Accumulate incoming samples until we have enough for detection
    QVector<double> m_accumulator;

    // Last detection result (updated from audio thread, read from paint thread)
    double  m_detectedHz  = 0.0;
    NoteInfo m_note       = {"", 0, 0.0, false};

    // Smoothed cents for the deviation bar
    double  m_smoothedCents = 0.0;
};

#endif // PITCHMONITORWIDGET_H
