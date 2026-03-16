#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include "vocalenhancer.h"
#include "complexes.h"
#include "audiovisualizerwidget.h"

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
#include <QScopedPointer>
#include <QFutureWatcher>

class AudioAmplifier;

class PreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreviewDialog(qint64 offset, qint64 sysLatency, QWidget *parent = nullptr);
    ~PreviewDialog();

    void setAudioFile(const QString &filePath);
    double getVolume() const;
    qint64 getOffset() const;
    double getPitchCorrectionAmount() const;
    double getNoiseReductionAmount() const;

private slots:
    void replayAudioPreview();
    void stopAudioPreview();
    void onDialValueChanged(int value);
    void onOffsetSliderChanged(int value);
    void updateVolume();
    void onPitchCorrectionChanged(int value);
    void onNoiseReductionChanged(int value);
    void startEnhancementJob();

private:
    void updateChronos();
    void seekForward();
    void seekBackward();
    void setPreviewControlsEnabled(bool enabled);
    void updateEnhancementLabels();

    QAudioFormat format;
    QScopedPointer<AudioAmplifier> amplifier;
    QScopedPointer<VocalEnhancer> vocalEnhancer;

    QLabel *volumeLabel = nullptr;
    QLabel *bannerLabel = nullptr;
    QLabel *offsetLabel = nullptr;
    QLabel *pitchCorrectionLabel = nullptr;
    QLabel *noiseReductionLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *seekForwardButton = nullptr;
    QPushButton *seekBackwardButton = nullptr;
    QPushButton *stopButton = nullptr;
    QCheckBox *playbackMute_option = nullptr;
    QDial *volumeDial = nullptr;
    QSlider *offsetSlider = nullptr;
    QSlider *pitchCorrectionSlider = nullptr;
    QSlider *noiseReductionSlider = nullptr;
    AudioVisualizerWidget *vocalVisualizer = nullptr;

    qint64 newOffset = 0;
    QTimer *progressTimer = nullptr;
    QTimer *volumeChangeTimer = nullptr;
    QTimer *chronosTimer = nullptr;
    QTimer *previewRebuildTimer = nullptr;
    QFutureWatcher<QByteArray> *enhanceWatcher = nullptr;

    QString audioFilePath;
    QByteArray previewInputAudioData;
    double volume = 1.0;
    int pendingVolumeValue = 100;
    qint64 audioOffset = 0;
    QString chronos;
    double pitchCorrectionAmount = 0.45;
    double noiseReductionAmount = 0.35;
    bool pendingPreviewRebuild = false;
};

#endif // PREVIEWDIALOG_H
