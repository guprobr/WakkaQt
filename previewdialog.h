#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>
#include <QAudioFormat>
#include <QDial>
#include <QPushButton>
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
    QLabel *volumeLabel;           
    QTimer *volumeChangeTimer;
    QString audioFilePath;
    double volume;
    int pendingVolumeValue;        
};

#endif // PREVIEWDIALOG_H
