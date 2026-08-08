#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include "vocalenhancer.h"
#include "complexes.h"
#include "audiovisualizerwidget.h"
#include "previewvideowidget.h"
#include "previewjob.h"
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

#include <QProgressBar>
#include <QDialog>
#include <QTabWidget>
#include <QVideoSink>
#include <QVideoFrame>
#include <QImage>
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
    void onVocalsExtracted(QByteArray pcmSamples, QAudioFormat pcmFormat);
    void onVocalsEnhanced(QByteArray tunedData);
    void startSnippetPreview();
    void onSnippetEnhanced(QByteArray tunedSlice);
    void revertSnippetPreview();
    void onToggleOriginalVocals();

private:
    // Cancels/waits for previewJob, snippetJob and m_effectWatcher, stops
    // mediaPlayer and kills child QProcesses. Called from both closeEvent()
    // (the window-close/X-button path) and the destructor — accept()/reject()
    // (e.g. the Render Mix button) hide the dialog without ever routing
    // through closeEvent(), so relying on closeEvent() alone left in-flight
    // background work able to fire its finished-callback against an
    // already-destroyed PreviewDialog.
    void cancelPendingWork();
    void updateChronos();
    void seekForward();
    void seekBackward();
    void setPreviewControlsEnabled(bool enabled);
    void updateEnhancementLabels();
    void syncVideoToAudio();
    void buildEffectsUi(class QVBoxLayout *effectsLayout);

    QAudioFormat format;
    QScopedPointer<AudioAmplifier> amplifier;
    QScopedPointer<PreviewJob> previewJob;
    // Separate PreviewJob instance for the 10s snippet preview (see
    // startSnippetPreview()) so it can't collide with previewJob's own
    // enhanced()/extracted() signals when both are relevant at once — each
    // PreviewJob owns its own VocalEnhancer/QFutureWatcher, so a second
    // instance is cheap and fully independent.
    QScopedPointer<PreviewJob> snippetJob;

    PreviewVideoWidget *videoRama = nullptr;
    QVideoSink *videoSink = nullptr;
    QMediaPlayer *mediaPlayer = nullptr;
    QTabWidget *tabWidget = nullptr;
#ifdef WAKKAQT_FFMPEG_NATIVE
    QScopedPointer<FFmpegNative::VideoEffectProcessor> videoEffectProcessor;
    // Runs videoEffectProcessor->process() off the GUI thread (see
    // onVideoFrame()) — a chain of several effects can take longer than a
    // frame interval, and running that synchronously on the GUI thread used
    // to stall the whole UI (and, with it, AudioAmplifier's 25ms data-push
    // timer, causing audible glitches) for however long it took.
    QFutureWatcher<QImage> *m_effectWatcher = nullptr;
    // Sequences frame dispatch: only one frame is ever in flight on the
    // worker at a time. A new frame arriving while the previous one is still
    // processing is dropped (the on-screen image just doesn't update for
    // that tick) rather than queued — under sustained load, a preview that
    // skips frames stays responsive; one that queues them just delays the
    // freeze instead of avoiding it.
    bool m_effectFrameInFlight = false;
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
    // "position / total duration" clock, updated alongside chronos in
    // updateChronos() — independent of AudioAmplifier::checkBufferState()'s
    // own elapsed-time string, which volumeLabel already shows.
    QLabel *positionClockLabel = nullptr;
    QLabel *offsetLabel = nullptr;
    QLabel *pitchCorrectionLabel = nullptr;
    QLabel *noiseReductionLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *seekForwardButton = nullptr;
    QPushButton *seekBackwardButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *applyButton = nullptr;
    // Hidden until a 10s snippet preview finishes; applies the same
    // (still-live) slider settings to the whole recording — the pre-existing
    // full-track path, previously triggered directly by applyButton.
    QPushButton *applyFullTrackButton = nullptr;
    // A/B toggle: swaps the amplifier's active buffer between the raw
    // extracted vocal (previewInputAudioData) and the current tuned baseline
    // (m_committedAudioData) — see onToggleOriginalVocals().
    QPushButton *originalToggleButton = nullptr;
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
    // Fires ~10s after a snippet preview starts playing, reverting to
    // m_committedAudioData — see startSnippetPreview()/revertSnippetPreview().
    QTimer *snippetRevertTimer = nullptr;

    QString audioFilePath;
    QByteArray previewInputAudioData;
    // Current full-track baseline — raw extracted vocals until the first
    // full enhancement completes, then whatever was last applied to the
    // whole track. This is what plays outside of an active snippet preview,
    // and what a snippet preview reverts back to. A snippet preview never
    // mutates this directly (see startSnippetPreview()/onSnippetEnhanced()).
    QByteArray m_committedAudioData;
    qint64 m_snippetStartBytes = 0;
    qint64 m_snippetLengthBytes = 0;
    // How much lead-in context (immediately before m_snippetStartBytes) was
    // included in the slice sent to VocalEnhancer, purely so its noise-gate
    // learning and phase-vocoder reset have real audio to settle on before
    // the part the user actually asked to hear — trimmed back off the
    // enhanced result in onSnippetEnhanced() before it's played.
    qint64 m_snippetLeadInBytes = 0;
    bool m_snippetPreviewActive = false;
    // Which buffer onToggleOriginalVocals() last switched playback to —
    // false == tuned (m_committedAudioData), true == raw original
    // (previewInputAudioData).
    bool m_showingOriginal = false;
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
