#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include "complexes.h"

#include <QDialog>
#include <QAudioFormat>
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
    AudioAmplifier *amplifier;
    QDial *volumeDial;
    QPushButton *startButton;
    QPushButton *stopButton;
    QPushButton *seekForwardButton;
    QPushButton *seekBackwardButton;

    QCheckBox *echo_option;
    QCheckBox *talent_option;
    QCheckBox *rubberband_option;
    QCheckBox *auburn_option;
    QCheckBox *playbackMute_option;

    QString echo_filter = _echo_filter;
    QString talent_filter = "";
    QString rubberband_filter = "";
    QString auburn_filter = "";

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
};

#endif // PREVIEWDIALOG_H
