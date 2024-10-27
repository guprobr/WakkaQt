#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include "complexes.h"

#include <QDialog>
#include <QAudioFormat>
#include <QFile>
#include <QDial>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QProcess>
#include <QTimer>

class AudioAmplifier;  

class PreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreviewDialog(qint64 offset, QWidget *parent = nullptr);
    ~PreviewDialog();

    void setAudioFile(const QString &filePath);
    double getVolume() const;
    bool getEcho() const;
    bool getTalent() const;
    bool getRubberband() const;
    bool getAuburn() const;
    
private slots:
    void replayAudioPreview();
    void stopAudioPreview();
    void onDialValueChanged(int value);
    void updateVolume();

private:
    QAudioFormat format;
    AudioAmplifier *amplifier;
    QDial *volumeDial;
    QPushButton *startButton;
    QPushButton *stopButton;
    QPushButton *seekForwardButton;
    QPushButton *seekBackwardButton;

    QCheckBox *playbackMute_option;

    QLabel *volumeLabel;           
    QTimer *volumeChangeTimer;
    QTimer *chronosTimer;
    QString audioFilePath;
    double volume;
    int pendingVolumeValue;
    qint64 audioOffset;

    QString chronos;

    void updateChronos();
    void seekForward();
    void seekBackward();
    void writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize, const QByteArray &pcmData);
};

#endif // PREVIEWDIALOG_H
