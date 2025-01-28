#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include "vocalenhancer.h"
#include "complexes.h"

#include <QProgressBar>
#include <QDialog>
#include <QAudioFormat>
#include <QFile>
#include <QDial>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>
#include <QProcess>
#include <QTimer>

class AudioAmplifier;  

class PreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreviewDialog(qint64 offset, qint64 sysLatency, QWidget *parent = nullptr);
    ~PreviewDialog();

    void setAudioFile(const QString &filePath);
    double getVolume() const;
    qint64 getOffset() const;
    
private slots:
    void replayAudioPreview();
    void stopAudioPreview();
    void onDialValueChanged(int value);
    void onOffsetSliderChanged(int value);
    void updateVolume();

private:
    QAudioFormat format;
    QScopedPointer<AudioAmplifier> amplifier;
    QScopedPointer<VocalEnhancer> vocalEnhancer;
    
    QLabel *volumeLabel;
    QLabel *bannerLabel;
    QProgressBar *progressBar;       
    QPushButton *startButton;
    QCheckBox *playbackMute_option;
    QPushButton *seekForwardButton;
    QDial *volumeDial;
    QPushButton *seekBackwardButton;
    QPushButton *stopButton;

    QSlider *offsetSlider;
    QLabel *offsetLabel;
    qint64 newOffset = 0; 
    
    QTimer *progressTimer;
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
