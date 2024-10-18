#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

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
    explicit PreviewDialog(QWidget *parent = nullptr);
    ~PreviewDialog();

    void setAudioFile(const QString &filePath);
    double getVolume() const;
    bool getEcho() const;
    bool getTalent() const;
    bool getRubberband() const;
    bool getAuburn() const;
    void updateChronos();

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
    QString echo_filter = "aecho=0.84:0.48:69|96:0.21|0.13,";
    QString talent_filter = "";
    QString rubberband_filter = "";
    QString auburn_filter = "";

    QLabel *volumeLabel;           
    QTimer *volumeChangeTimer;
    QTimer *chronosTimer;
    QString audioFilePath;
    double volume;
    int pendingVolumeValue;

    QString chronos;       
};

#endif // PREVIEWDIALOG_H
