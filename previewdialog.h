#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include "vocalenhancer.h"
#include "complexes.h"
#include "audiovisualizerwidget.h"
#include "previewvideowidget.h"
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

#include <QProgressBar>
#include <QDialog>
#include <QTabWidget>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QAudioFormat>
#include <QFile>
#include <QDial>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QComboBox>
#include <QToolButton>
#include <QLabel>
#include <QProcess>
#include <QTimer>
#include <QScopedPointer>
#include <QFutureWatcher>
#include <atomic>

class AudioAmplifier;

class PreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreviewDialog(qint64 offset, QWidget *parent = nullptr);
    ~PreviewDialog();

    void setAudioFile(const QString &filePath);
    void setVideoFile(const QString &filePath, qint64 videoOffsetMs);
    double getVolume() const;
    qint64 getOffset() const;
    double getPitchCorrectionAmount() const;
    double getNoiseReductionAmount() const;
    QString getVideoEffectChain() const { return m_videoEffectChain; }

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void replayAudioPreview();
    void stopAudioPreview();
    void onDialValueChanged(int value);
    void onOffsetSliderChanged(int value);
    void updateVolume();
    void onPitchCorrectionChanged(int value);
    void onNoiseReductionChanged(int value);
    void onKeyChanged(int index);
    void onScaleChanged(int index);
    void onRetuneSpeedChanged(int value);
    void onFormantPreservationChanged(bool checked);
    void startEnhancementJob();
    void onVideoFrame(const QVideoFrame &frame);
    void updateVideoEffectChain();

private:
    void updateChronos();
    void seekForward();
    void seekBackward();
    void setPreviewControlsEnabled(bool enabled);
    void updateEnhancementLabels();
    void syncVideoToAudio();
    void buildEffectsUi(class QVBoxLayout *effectsLayout);

    QAudioFormat format;
    QScopedPointer<AudioAmplifier> amplifier;
    QScopedPointer<VocalEnhancer> vocalEnhancer;

    PreviewVideoWidget *videoRama = nullptr;
    QVideoSink *videoSink = nullptr;
    QMediaPlayer *mediaPlayer = nullptr;
    QTabWidget *tabWidget = nullptr;
#ifdef WAKKAQT_FFMPEG_NATIVE
    QScopedPointer<FFmpegNative::VideoEffectProcessor> videoEffectProcessor;
#endif
    // One row per available effect (only ones whose chain actually builds on
    // this machine — see buildEffectsUi()/isChainAvailable). Row i controls
    // videoEffectPresets[m_effectRows[i].presetIndex]; enabled effects are
    // joined (in row order) into m_videoEffectChain by updateVideoEffectChain().
    // Sliders live in a collapsible body, hidden until the row is expanded,
    // so a dozen effects don't all fight for space at once.
    struct EffectRow {
        int presetIndex = -1;
        QCheckBox *enabledCheck = nullptr; // checked == effect enabled
        QVector<QSlider*> sliders;
        QVector<QLabel*> valueLabels;
    };
    QVector<EffectRow> m_effectRows;
    QString m_videoEffectChain; // all currently-enabled effects joined with commas, empty = none

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
    QPushButton *applyButton = nullptr;
    QCheckBox *playbackMute_option = nullptr;
    QDial *volumeDial = nullptr;
    QSlider *offsetSlider = nullptr;
    QSlider *pitchCorrectionSlider = nullptr;
    QSlider   *noiseReductionSlider  = nullptr;
    QComboBox *keyCombo              = nullptr;
    QComboBox *scaleCombo            = nullptr;
    QSlider   *retuneSpeedSlider     = nullptr;
    QLabel    *retuneSpeedLabel      = nullptr;
    QCheckBox *formantCheckBox       = nullptr;
    QSlider   *reverbRoomSlider      = nullptr;
    QSlider   *reverbDecaySlider     = nullptr;
    QSlider   *reverbMixSlider       = nullptr;
    QLabel    *reverbLabel           = nullptr;
    AudioVisualizerWidget *vocalVisualizer = nullptr;

    qint64 newOffset = 0;
    QTimer *progressTimer = nullptr;
    QTimer *volumeChangeTimer = nullptr;
    QTimer *chronosTimer = nullptr;
    QTimer *previewRebuildTimer = nullptr;
    QFutureWatcher<QByteArray> *enhanceWatcher = nullptr;
    // Polled from inside VocalEnhancer::enhance()'s hot loops so closeEvent()
    // can abort a long-running enhance() promptly instead of
    // waitForFinished() blocking the GUI thread for the rest of its
    // (otherwise uninterruptible) runtime. Reset to false at the start of
    // every startEnhancementJob() call.
    std::atomic<bool> enhanceCancelled{false};

    QString audioFilePath;
    QByteArray previewInputAudioData;
    double volume = 1.0;
    int pendingVolumeValue = 100;
    qint64 audioOffset = 0;
    QString chronos;
    double pitchCorrectionAmount = 0.45;
    double noiseReductionAmount = 0.35;
    int    m_keyNote          = 0;
    int    m_scaleIndex       = 0;
    double m_retuneSpeedMs    = 300.0;
    bool   m_formantPreservation = true;
    bool   pendingPreviewRebuild = false;
    qint64 m_savedPlaybackPos    = 0;
    qint64 m_mediaPlayerOffset   = 0;
    bool   m_hasVideo            = false;
    // Last amplifier byte position seen by syncVideoToAudio(), used to
    // detect genuine playback by watching the position actually move —
    // AudioAmplifier::isPlaying() (QAudioSink::state()==ActiveState) proved
    // unreliable on at least one real audio backend, staying non-Active
    // forever despite audio audibly playing and elapsed time advancing.
    qint64 m_lastSyncPosBytes    = -1;
    double m_reverbRoomSize      = 0.5;
    double m_reverbDecay         = 0.5;
    double m_reverbMix           = 0.0;
};

#endif // PREVIEWDIALOG_H
