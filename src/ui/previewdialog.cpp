#include "complexes.h"
#include "previewdialog.h"
#include "audioamplifier.h"
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

#include <QtConcurrent/QtConcurrentRun>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFont>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>
#include <QFrame>
#include <QScrollArea>

PreviewDialog::PreviewDialog(qint64 offset, QWidget *parent)
    : QDialog(parent),
      audioOffset(offset),
      newOffset(offset),
      amplifier(nullptr),
      volume(1.0),
      pendingVolumeValue(100)
{
    setWindowTitle("Karaoke Studio");

    QVBoxLayout *layout = new QVBoxLayout(this);
    QHBoxLayout *controls = new QHBoxLayout();
    
    volumeDial = new QDial(this);
    volumeDial->setRange(0, 500);
    volumeDial->setValue(100);
    volumeDial->setNotchesVisible(true);
    volumeDial->setToolTip("Adjust the knob to amplify or lower volume level");
    volumeDial->setFixedSize(200, 100);

    QLabel *volumeBanner = new QLabel(
        "Here you can preview the performance and tweak the recording with effects.\n"
        "You must press APPLY ENHANCEMENTS before rendering to take effect",
        this);
    volumeBanner->setToolTip("You can adjust the final volume for the render output.");
    volumeBanner->setFont(QFont("", 11));
    volumeBanner->setWordWrap(true);

    bannerLabel = new QLabel("Enhancing Vocals", this);
    bannerLabel->setToolTip("VocalEnhancement");
    bannerLabel->setFont(QFont("", 16));

    volumeLabel = new QLabel("Current Volume: 100%", this);
    volumeLabel->setToolTip("Values above 100% amplifies, while values below reduce volume");
    volumeLabel->setFont(QFont("", 14, QFont::Bold));

    pitchCorrectionLabel = new QLabel(this);
    noiseReductionLabel = new QLabel(this);

    startButton = new QPushButton("REWIND", this);
    startButton->setToolTip("Restart playback");

    stopButton = new QPushButton("Render Mix", this);
    stopButton->setToolTip("Apply volume and effect changes and begin rendering");

    seekForwardButton = new QPushButton(">>", this);
    seekForwardButton->setToolTip("Seek forward");

    seekBackwardButton = new QPushButton("<<", this);
    seekBackwardButton->setToolTip("Seek backwards");

    offsetSlider = new QSlider(Qt::Horizontal, this);
    offsetSlider->setRange(-1500, 1500);
    offsetSlider->setValue(int(newOffset));
    offsetSlider->setTickPosition(QSlider::TicksBothSides);
    offsetSlider->setTickInterval(250);
    offsetSlider->setFixedWidth(480);
    offsetSlider->setTracking(false);
    offsetSlider->setSingleStep(50);

    offsetLabel = new QLabel(QString("Adjust sync offset: %1 ms").arg(newOffset), this);
    offsetLabel->setToolTip("Adjust (in ms) playback and vocals sync, if required; negative values cause delay while positive values cause to trim.");

    pitchCorrectionSlider = new QSlider(Qt::Horizontal, this);
    pitchCorrectionSlider->setRange(0, 100);
    pitchCorrectionSlider->setValue(int(pitchCorrectionAmount * 100.0));
    pitchCorrectionSlider->setTickPosition(QSlider::TicksBelow);
    pitchCorrectionSlider->setTickInterval(10);
    pitchCorrectionSlider->setToolTip("How strongly the preview pushes the vocal toward the nearest note.");

    noiseReductionSlider = new QSlider(Qt::Horizontal, this);
    noiseReductionSlider->setRange(0, 100);
    noiseReductionSlider->setValue(int(noiseReductionAmount * 100.0));
    noiseReductionSlider->setTickPosition(QSlider::TicksBelow);
    noiseReductionSlider->setTickInterval(10);
    noiseReductionSlider->setToolTip("How aggressively the preview removes steady background noise.");

    // Key selector
    keyCombo = new QComboBox(this);
    keyCombo->addItems({"C", "C#/Db", "D", "D#/Eb", "E", "F",
                        "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"});
    keyCombo->setCurrentIndex(0);
    keyCombo->setToolTip("Root key for scale-aware pitch correction");

    // Scale selector
    scaleCombo = new QComboBox(this);
    scaleCombo->addItems({"Chromatic", "Major", "Minor",
                          "Pentatonic Major", "Pentatonic Minor", "Blues"});
    scaleCombo->setCurrentIndex(0);
    scaleCombo->setToolTip("Musical scale to restrict pitch correction targets");

    // Retune speed
    retuneSpeedLabel = new QLabel(QString("Retune speed: %1 ms  (0=robotic, 500=natural)")
                                      .arg(int(m_retuneSpeedMs)), this);
    retuneSpeedSlider = new QSlider(Qt::Horizontal, this);
    retuneSpeedSlider->setRange(0, 500);
    retuneSpeedSlider->setValue(int(m_retuneSpeedMs));
    retuneSpeedSlider->setTickPosition(QSlider::TicksBelow);
    retuneSpeedSlider->setTickInterval(50);
    retuneSpeedSlider->setToolTip("0 = instant/robotic (T-Pain); 300 = natural; 500 = very gradual");

    // Formant preservation
    formantCheckBox = new QCheckBox("Preserve formants (natural timbre)", this);
    formantCheckBox->setChecked(m_formantPreservation);
    formantCheckBox->setToolTip(
        "Uses LPC analysis to keep vocal character unchanged during pitch correction");

    // Reverb controls
    reverbLabel = new QLabel("Reverb — Room: 50%  Decay: 50%  Mix: 0% (off)", this);
    reverbLabel->setToolTip("Schroeder/Freeverb reverb effect");

    reverbRoomSlider = new QSlider(Qt::Horizontal, this);
    reverbRoomSlider->setRange(0, 100);
    reverbRoomSlider->setValue(int(m_reverbRoomSize * 100.0));
    reverbRoomSlider->setTickPosition(QSlider::TicksBelow);
    reverbRoomSlider->setTickInterval(10);
    reverbRoomSlider->setToolTip("Room size: 0 = small/tight, 100 = large/cathedral");

    reverbDecaySlider = new QSlider(Qt::Horizontal, this);
    reverbDecaySlider->setRange(0, 100);
    reverbDecaySlider->setValue(int(m_reverbDecay * 100.0));
    reverbDecaySlider->setTickPosition(QSlider::TicksBelow);
    reverbDecaySlider->setTickInterval(10);
    reverbDecaySlider->setToolTip("Decay: 0 = dry/short, 100 = long reverb tail");

    reverbMixSlider = new QSlider(Qt::Horizontal, this);
    reverbMixSlider->setRange(0, 100);
    reverbMixSlider->setValue(int(m_reverbMix * 100.0));
    reverbMixSlider->setTickPosition(QSlider::TicksBelow);
    reverbMixSlider->setTickInterval(10);
    reverbMixSlider->setToolTip("Wet/dry mix: 0 = dry (off), 100 = full reverb");

    // Apply button — triggers enhancement with current settings
    applyButton = new QPushButton("Apply Enhancement", this);
    applyButton->setToolTip("Apply all current settings and re-process the vocals");
    applyButton->setFont(QFont("", 11, QFont::Bold));

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setFixedSize(QSize(600, 50));
    progressBar->setToolTip("VocalEnhancer progress bar");

    playbackMute_option = new QCheckBox("Preview vocals only", this);
    playbackMute_option->setToolTip("Check to mute backing track while previewing");
    playbackMute_option->setChecked(false);
    playbackMute_option->setFont(QFont("", 8));

    vocalVisualizer = new AudioVisualizerWidget(this);
    vocalVisualizer->setToolTip("Vocal Visualizer");
    vocalVisualizer->setMinimumHeight(90);

    // Video is muted at the QMediaPlayer level — audio playback is driven
    // entirely by AudioAmplifier's raw PCM sinks (vocals + backing track).
    // mediaPlayer only decodes/renders frames; syncVideoToAudio() keeps its
    // position locked to the amplifier's playback clock.
    // Height is capped so it can't push the rest of the tab's controls down.
    //
    // videoRama is a plain QWidget (not QVideoWidget) painted from onVideoFrame()
    // instead of being handed to mediaPlayer directly, so a selected video effect
    // (see the Effects tab) can be applied to each decoded frame before display.
    // (A prior version routed mediaPlayer through an extra hidden/stacked
    // QVideoWidget "anchor", theorizing a bare QVideoSink wasn't getting
    // scheduled on a real desktop. That turned out to be chasing a ghost —
    // the real bug was mediaPlayer->play() never being called at all, see
    // syncVideoToAudio() — and the anchor/stacking only added a second
    // widget that could obscure this one. Back to a single, unambiguous
    // widget.)
    videoRama = new PreviewVideoWidget(this);
    videoRama->setToolTip("Recorded performance video");
    videoRama->setMinimumHeight(160);
    videoRama->setMaximumHeight(240);
    videoRama->hide();

    mediaPlayer = new QMediaPlayer(this);
    videoSink = new QVideoSink(this);
    mediaPlayer->setVideoSink(videoSink);
    connect(videoSink, &QVideoSink::videoFrameChanged, this, &PreviewDialog::onVideoFrame);

    controls->addWidget(seekBackwardButton);
    controls->addWidget(volumeDial);
    controls->addWidget(seekForwardButton);

    // Status chrome, video preview and vocal visualizer all live above the
    // tabs so they stay visible no matter which tab is open; Render Mix
    // lives below for the same reason.
    layout->addWidget(volumeBanner);
    layout->addWidget(bannerLabel);
    layout->addWidget(progressBar);
    layout->addWidget(videoRama);
    layout->addWidget(vocalVisualizer);

    tabWidget = new QTabWidget(this);

    // ── "Preview && Sync" tab — the controls a casual user actually needs:
    // watch the take, adjust volume/sync, hit Render Mix. ──────────────────
    QWidget *previewTab = new QWidget(this);
    QVBoxLayout *previewLayout = new QVBoxLayout(previewTab);
    previewLayout->addWidget(volumeLabel);
    previewLayout->addWidget(startButton);
    previewLayout->addWidget(playbackMute_option);
    previewLayout->addLayout(controls);
    previewLayout->addWidget(offsetLabel);
    previewLayout->addWidget(offsetSlider);
    previewLayout->addStretch();
    tabWidget->addTab(previewTab, "🎬 Preview && Sync");

    // ── "Vocal Tuning" tab — power-user autotune/noise/reverb controls,
    // tucked out of the way by default. ────────────────────────────────────
    QWidget *tuneTab = new QWidget(this);
    QVBoxLayout *tuneLayout = new QVBoxLayout(tuneTab);
    tuneLayout->addWidget(pitchCorrectionLabel);
    tuneLayout->addWidget(pitchCorrectionSlider);
    tuneLayout->addWidget(noiseReductionLabel);
    tuneLayout->addWidget(noiseReductionSlider);

    // Autotune controls row
    QHBoxLayout *tuneRow = new QHBoxLayout();
    tuneRow->addWidget(new QLabel("Key:", this));
    tuneRow->addWidget(keyCombo);
    tuneRow->addSpacing(12);
    tuneRow->addWidget(new QLabel("Scale:", this));
    tuneRow->addWidget(scaleCombo);
    tuneLayout->addLayout(tuneRow);
    tuneLayout->addWidget(retuneSpeedLabel);
    tuneLayout->addWidget(retuneSpeedSlider);
    tuneLayout->addWidget(formantCheckBox);
    tuneLayout->addWidget(reverbLabel);

    QHBoxLayout *reverbRow = new QHBoxLayout();
    reverbRow->addWidget(new QLabel("Room:", this));
    reverbRow->addWidget(reverbRoomSlider);
    reverbRow->addSpacing(8);
    reverbRow->addWidget(new QLabel("Decay:", this));
    reverbRow->addWidget(reverbDecaySlider);
    reverbRow->addSpacing(8);
    reverbRow->addWidget(new QLabel("Mix:", this));
    reverbRow->addWidget(reverbMixSlider);
    tuneLayout->addLayout(reverbRow);

    tuneLayout->addWidget(applyButton);
    tuneLayout->addStretch();
    tabWidget->addTab(tuneTab, "🎚️ Vocal Tuning");

    // ── "Effects" tab — visual filters applied to the webcam video, live in
    // the preview above and identically in the final render. Any number of
    // effects can be enabled at once, each with its own tunable parameters.
    // Rows are collapsed by default (click a name to expand its sliders) and
    // scrollable, since a dozen effects' worth of controls don't fit at once. ──
    QWidget *effectsTab = new QWidget(this);
    QVBoxLayout *effectsLayout = new QVBoxLayout(effectsTab);
    QLabel *effectsHelp = new QLabel(
        "Check any effects you'd like to combine, click a name to adjust its "
        "parameters. The preview above updates live, and the same effects are "
        "applied to the final Render Mix.", this);
    effectsHelp->setWordWrap(true);
    effectsLayout->addWidget(effectsHelp);

    QScrollArea *effectsScroll = new QScrollArea(effectsTab);
    effectsScroll->setWidgetResizable(true);
    effectsScroll->setFrameShape(QFrame::NoFrame);
    QWidget *effectsScrollContent = new QWidget(effectsScroll);
    QVBoxLayout *effectsScrollLayout = new QVBoxLayout(effectsScrollContent);
    buildEffectsUi(effectsScrollLayout);
    effectsScrollLayout->addStretch();
    effectsScroll->setWidget(effectsScrollContent);
    effectsLayout->addWidget(effectsScroll);

    tabWidget->addTab(effectsTab, "✨ Effects");

    layout->addWidget(tabWidget);
    layout->addWidget(stopButton);
    layout->setAlignment(Qt::AlignHCenter);
    setMinimumSize(680, 620);
    resize(1024, 768);

    updateEnhancementLabels();

    format.setSampleRate(48000);  // default; overridden from WAV header in onExtracted
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::SampleFormat::Int16);

    amplifier.reset(new AudioAmplifier(format, this));
    previewJob.reset(new PreviewJob(this));
#ifdef WAKKAQT_FFMPEG_NATIVE
    videoEffectProcessor.reset(new FFmpegNative::VideoEffectProcessor());
#endif

    connect(previewJob.data(), &PreviewJob::extracted, this, &PreviewDialog::onVocalsExtracted);
    connect(previewJob.data(), &PreviewJob::extractionFailed, this,
            [this](const QString &reason, bool wasCancelled) {
        // A cancelled extraction means this run was deliberately superseded
        // (a newer extract() call, or the dialog closing) — the run that
        // actually matters is still in flight or the dialog is going away
        // either way, so there is nothing useful to show the user here.
        if (wasCancelled)
            return;
        QMessageBox::critical(this, "Extraction failed", reason);
        setPreviewControlsEnabled(true);
    });
    connect(previewJob.data(), &PreviewJob::enhanced, this, &PreviewDialog::onVocalsEnhanced);

    connect(amplifier.data(), &AudioAmplifier::vocalPreviewChunk,
            vocalVisualizer, &AudioVisualizerWidget::updateVisualization);

    connect(startButton, &QPushButton::clicked, this, &PreviewDialog::replayAudioPreview);
    connect(stopButton, &QPushButton::clicked, this, &PreviewDialog::stopAudioPreview);
    connect(seekBackwardButton, &QPushButton::clicked, this, &PreviewDialog::seekBackward);
    connect(seekForwardButton, &QPushButton::clicked, this, &PreviewDialog::seekForward);
    connect(volumeDial, &QDial::valueChanged, this, &PreviewDialog::onDialValueChanged);
    connect(offsetSlider, &QSlider::valueChanged, this, &PreviewDialog::onOffsetSliderChanged);
    connect(pitchCorrectionSlider, &QSlider::valueChanged,
            this, &PreviewDialog::onPitchCorrectionChanged);
    connect(noiseReductionSlider, &QSlider::valueChanged,
            this, &PreviewDialog::onNoiseReductionChanged);
    connect(keyCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::onKeyChanged);
    connect(scaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::onScaleChanged);
    connect(retuneSpeedSlider, &QSlider::valueChanged,
            this, &PreviewDialog::onRetuneSpeedChanged);
    connect(formantCheckBox, &QCheckBox::toggled,
            this, &PreviewDialog::onFormantPreservationChanged);
    connect(applyButton, &QPushButton::clicked,
            this, &PreviewDialog::startEnhancementJob);
    connect(reverbRoomSlider, &QSlider::valueChanged, this, [this](int v) {
        m_reverbRoomSize = v / 100.0;
        reverbLabel->setText(QString("Reverb — Room: %1%  Decay: %2%  Mix: %3%")
            .arg(v).arg(int(m_reverbDecay*100)).arg(int(m_reverbMix*100)));
    });
    connect(reverbDecaySlider, &QSlider::valueChanged, this, [this](int v) {
        m_reverbDecay = v / 100.0;
        reverbLabel->setText(QString("Reverb — Room: %1%  Decay: %2%  Mix: %3%")
            .arg(int(m_reverbRoomSize*100)).arg(v).arg(int(m_reverbMix*100)));
    });
    connect(reverbMixSlider, &QSlider::valueChanged, this, [this](int v) {
        m_reverbMix = v / 100.0;
        reverbLabel->setText(QString("Reverb — Room: %1%  Decay: %2%  Mix: %3%")
            .arg(int(m_reverbRoomSize*100)).arg(int(m_reverbDecay*100)).arg(v));
    });
    // stateChanged(int), not checkStateChanged: the latter only exists from
    // Qt 6.7 onward (missing on the Qt 6.4-era qt6-base-dev CI builds
    // against) — stateChanged() has been available since Qt5 and still
    // works everywhere in Qt6, just deprecated (not removed) from 6.7 on.

    #if QT_VERSION < QT_VERSION_CHECK(6, 7, 0)
    connect(playbackMute_option, &QCheckBox::stateChanged, this, [this]() {
        amplifier->setPlaybackVol(!playbackMute_option->isChecked());
    });    
    #else
    connect(playbackMute_option, &QCheckBox::checkStateChanged, this, [this]() {
        amplifier->setPlaybackVol(!playbackMute_option->isChecked());
    });
    #endif

    chronosTimer = new QTimer(this);
    connect(chronosTimer, &QTimer::timeout, this, &PreviewDialog::updateChronos);
    chronosTimer->start(250);

    volumeChangeTimer = new QTimer(this);
    connect(volumeChangeTimer, &QTimer::timeout, this, &PreviewDialog::updateVolume);
    volumeChangeTimer->setSingleShot(true);

    progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, this, [this]() {
        VocalEnhancer *enhancer = previewJob->enhancer();
        if (!enhancer)
            return;
        progressBar->setValue(enhancer->getProgress());
        bannerLabel->setText(enhancer->getBanner());
    });

    previewRebuildTimer = new QTimer(this);
    previewRebuildTimer->setSingleShot(true);
    connect(previewRebuildTimer, &QTimer::timeout,
            this, &PreviewDialog::startEnhancementJob);
}

void PreviewDialog::closeEvent(QCloseEvent *event)
{
    // Cancel any in-flight enhancement future so its finished() callback
    // doesn't fire against a hidden dialog and pop up stray QMessageBoxes,
    // then block until both extraction and enhancement are done — enhance()
    // has to notice the cancellation flag itself (in its hot loops) and
    // return early, or this would block the GUI thread for however long the
    // rest of that enhance() call was going to take anyway. extractAudio()
    // has no cancellation token (it's a single fast decode+resample, not a
    // long DSP pipeline), so there's nothing to flag for it — just wait.
    previewJob->cancelEnhance();
    previewJob->waitForIdle();
    if (mediaPlayer)
        mediaPlayer->stop();
    // Children QProcess objects (ffmpegProcess) have this as parent; Qt kills
    // them on destruction, but if one is running right now its finished()
    // lambda would call slots on a hidden 'this'. Kill all child processes now.
    const auto procs = findChildren<QProcess *>();
    for (QProcess *p : procs)
        p->kill();

    QDialog::closeEvent(event);
}

PreviewDialog::~PreviewDialog()
{
    if (amplifier)
        amplifier->stop();
}

void PreviewDialog::setAudioFile(const QString &filePath)
{
    audioFilePath = filePath;
    qDebug() << "Audio file set to:" << audioFilePath;

    setPreviewControlsEnabled(false);
    bannerLabel->setText("Extracting vocals preview...");
    progressBar->setValue(0);
    vocalVisualizer->clear();

    PreviewJob::ExtractParams params;
    params.sourceFile   = audioFilePath;
    params.destTempFile = tunedRecorded;
    params.trimOffsetMs = audioOffset;
    previewJob->extract(params);
}

void PreviewDialog::onVocalsExtracted(QByteArray pcmSamples, QAudioFormat pcmFormat)
{
    previewInputAudioData = pcmSamples;

    // Reinitialize audio pipeline if the extracted WAV's rate differs from the
    // current format (e.g. recording at 48000 Hz vs. previous default 44100 Hz).
    if (pcmFormat.sampleRate() != format.sampleRate() ||
        pcmFormat.channelCount() != format.channelCount()) {
        format.setSampleRate(pcmFormat.sampleRate());
        format.setChannelCount(pcmFormat.channelCount());
        amplifier.reset(new AudioAmplifier(format, this));
        connect(amplifier.data(), &AudioAmplifier::vocalPreviewChunk,
                vocalVisualizer, &AudioVisualizerWidget::updateVisualization);
        qDebug() << "PreviewDialog: audio pipeline reinitialized at"
                 << pcmFormat.sampleRate() << "Hz," << pcmFormat.channelCount() << "ch";
    }

    startEnhancementJob();
}

void PreviewDialog::setVideoFile(const QString &filePath, qint64 videoOffsetMs)
{
    m_mediaPlayerOffset = videoOffsetMs;

    const QFileInfo info(filePath);
    if (!info.exists() || info.size() <= 0) {
        qDebug() << "PreviewDialog: no webcam video available, hiding video preview.";
        m_hasVideo = false;
        videoRama->hide();
        return;
    }

    m_hasVideo = true;
    videoRama->show();
    mediaPlayer->setSource(QUrl::fromLocalFile(filePath));
    mediaPlayer->pause();
}

// Keeps mediaPlayer's frame locked to AudioAmplifier's playback clock (the
// backing track's timeline, where byte/position 0 == start of the song).
// webcamRecorded may have its own pre-roll/lag relative to that timeline,
// captured in m_mediaPlayerOffset (== MainWindow::videoOffset) the same way
// audioOffset captures it for the vocal recording.
void PreviewDialog::syncVideoToAudio()
{
    if (!m_hasVideo || !mediaPlayer || !amplifier)
        return;

    const qint64 posBytes = amplifier->getPosition();
    const qint64 songPosMs = format.durationForBytes(posBytes) / 1000;
    const qint64 targetMs  = songPosMs + m_mediaPlayerOffset;

    // Detect genuine playback by watching the position actually advance
    // between calls, instead of trusting AudioAmplifier::isPlaying()
    // (QAudioSink::state() == ActiveState): confirmed unreliable on at
    // least one real audio backend, where it never reports ActiveState
    // despite audio audibly playing and the position genuinely advancing —
    // which permanently starved mediaPlayer of any play() call.
    const bool audioAdvancing = (m_lastSyncPosBytes >= 0) && (posBytes != m_lastSyncPosBytes);
    m_lastSyncPosBytes = posBytes;

    if (targetMs < 0) {
        // Webcam hadn't started recording yet at this point in the song.
        if (mediaPlayer->playbackState() != QMediaPlayer::PausedState)
            mediaPlayer->pause();
        if (mediaPlayer->position() != 0)
            mediaPlayer->setPosition(0);
        return;
    }

    const bool alreadyPlaying = (mediaPlayer->playbackState() == QMediaPlayer::PlayingState);
    const qint64 drift = qAbs(targetMs - mediaPlayer->position());
    // While already playing, let the stream run freely instead of correcting
    // on every 250ms tick: re-seeking for small drift never lets a single
    // seek finish settling (decode + present a frame) before the next tick
    // interrupts it with another seek, which looks exactly like a frozen
    // frame. Snap immediately when (re)starting/paused so playback begins at
    // the right spot; once actually playing, only correct real drift.
    if (!alreadyPlaying || drift > 1000)
        mediaPlayer->setPosition(targetMs);

    if (audioAdvancing) {
        if (!alreadyPlaying)
            mediaPlayer->play();
    } else if (alreadyPlaying) {
        mediaPlayer->pause();
    }
}

// Builds one collapsible row per entry in complexes.h's videoEffectPresets —
// a checkbox (enables the effect) plus a clickable header (expands/collapses
// its parameter sliders, collapsed by default) — silently dropping any
// preset whose filter chain can't actually be built on this machine at its
// default parameter values (e.g. Vertigo needs frei0r-plugins installed —
// see main.cpp's FREI0R_PATH setup). Any number of rows can be checked at
// once; updateVideoEffectChain() joins all enabled ones into m_videoEffectChain.
// Rows are collapsed rather than shown in full so a dozen effects' worth of
// sliders don't all fight for space at once — see also the QScrollArea this
// gets added to in the constructor.
void PreviewDialog::buildEffectsUi(QVBoxLayout *effectsLayout)
{
    m_effectRows.clear();

#ifdef WAKKAQT_FFMPEG_NATIVE
    for (int presetIdx = 0; presetIdx < videoEffectPresets.size(); ++presetIdx) {
        const VideoEffectPreset &preset = videoEffectPresets[presetIdx];

        QVector<double> defaults;
        for (const VideoEffectParam &param : preset.params)
            defaults << param.defaultValue;

        if (!FFmpegNative::VideoEffectProcessor::isChainAvailable(preset.buildFilterChain(defaults))) {
            qDebug() << "PreviewDialog: video effect unavailable on this machine, hiding:" << preset.id;
            continue;
        }

        EffectRow row;
        row.presetIndex = presetIdx;

        QWidget *rowWidget = new QWidget(this);
        QVBoxLayout *rowLayout = new QVBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 4, 0, 4);

        QWidget *header = new QWidget(rowWidget);
        QHBoxLayout *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);

        QCheckBox *enableCheck = new QCheckBox(header);
        enableCheck->setToolTip("Enable this effect");

        QToolButton *expandButton = new QToolButton(header);
        expandButton->setText(preset.label);
        expandButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        expandButton->setStyleSheet("QToolButton { border: none; font-weight: bold; }");
        expandButton->setCheckable(true);
        expandButton->setArrowType(Qt::RightArrow);
        expandButton->setChecked(false);
        expandButton->setVisible(!preset.params.isEmpty()); // nothing to expand into

        headerLayout->addWidget(enableCheck);
        headerLayout->addWidget(expandButton);
        if (preset.params.isEmpty())
            headerLayout->addWidget(new QLabel(preset.label, header));
        headerLayout->addStretch();
        rowLayout->addWidget(header);

        QWidget *body = new QWidget(rowWidget);
        QVBoxLayout *bodyLayout = new QVBoxLayout(body);
        body->setVisible(false);

        for (const VideoEffectParam &param : preset.params) {
            QHBoxLayout *paramRow = new QHBoxLayout();
            QLabel *nameLabel = new QLabel(param.label + ":", body);
            nameLabel->setMinimumWidth(80);

            QSlider *slider = new QSlider(Qt::Horizontal, body);
            slider->setRange(0, 1000);
            slider->setValue(qRound((param.defaultValue - param.minValue)
                                     / (param.maxValue - param.minValue) * 1000.0));

            QLabel *valueLabel = new QLabel(QString::number(param.defaultValue, 'f', param.decimals), body);
            valueLabel->setMinimumWidth(50);

            paramRow->addWidget(nameLabel);
            paramRow->addWidget(slider);
            paramRow->addWidget(valueLabel);
            bodyLayout->addLayout(paramRow);

            row.sliders.append(slider);
            row.valueLabels.append(valueLabel);

            const double minV = param.minValue, maxV = param.maxValue;
            const int decimals = param.decimals;
            connect(slider, &QSlider::valueChanged, this, [this, valueLabel, minV, maxV, decimals](int sliderPos) {
                const double value = minV + (sliderPos / 1000.0) * (maxV - minV);
                valueLabel->setText(QString::number(value, 'f', decimals));
                updateVideoEffectChain();
            });
        }

        rowLayout->addWidget(body);

        connect(expandButton, &QToolButton::toggled, body, [expandButton, body](bool expanded) {
            expandButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            body->setVisible(expanded);
        });

        row.enabledCheck = enableCheck;
        connect(enableCheck, &QCheckBox::toggled, this, [this, expandButton](bool checked) {
            // Checking a box also reveals its sliders, so the effect the
            // user just turned on is immediately tunable without a second click.
            if (checked && expandButton->isVisible())
                expandButton->setChecked(true);
            updateVideoEffectChain();
        });

        effectsLayout->addWidget(rowWidget);

        QFrame *separator = new QFrame(this);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        effectsLayout->addWidget(separator);

        m_effectRows.append(row);
    }

    if (m_effectRows.isEmpty()) {
        QLabel *noneLabel = new QLabel("No video effects are available on this machine.", this);
        noneLabel->setWordWrap(true);
        effectsLayout->addWidget(noneLabel);
    }
#else
    QLabel *unavailable = new QLabel(
        "Video effects require the native FFmpeg build (WAKKAQT_FFMPEG_NATIVE).", this);
    unavailable->setWordWrap(true);
    effectsLayout->addWidget(unavailable);
#endif
}

// Rebuilds m_videoEffectChain from every currently-checked effect box, in
// row order, using each slider's live value — called whenever a checkbox is
// toggled or any slider moves.
void PreviewDialog::updateVideoEffectChain()
{
    QStringList enabledChains;
    for (const EffectRow &row : m_effectRows) {
        if (!row.enabledCheck || !row.enabledCheck->isChecked())
            continue;
        const VideoEffectPreset &preset = videoEffectPresets[row.presetIndex];
        QVector<double> values;
        for (int i = 0; i < row.sliders.size(); ++i) {
            const VideoEffectParam &param = preset.params[i];
            const double v = param.minValue
                + (row.sliders[i]->value() / 1000.0) * (param.maxValue - param.minValue);
            values.append(v);
        }
        enabledChains << preset.buildFilterChain(values);
    }
    m_videoEffectChain = enabledChains.join(",");
}

// Every decoded webcam frame passes through here (via QVideoSink, instead of
// handing mediaPlayer straight to a QVideoWidget) so the selected effect can
// be applied before the frame reaches the screen — same filter chain used
// for the final render, so preview and output match.
void PreviewDialog::onVideoFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    QImage image = frame.toImage();
    if (image.isNull())
        return;

    // Downscale before filtering (never before the final render, which
    // stays at full resolution via ffmpegnative.cpp's own separate pass).
    // Effect filter cost scales with pixel count, and the preview widget is
    // a fraction of a typical 1080p webcam frame's size, so there's no
    // visible quality loss here and a large real reduction in per-frame CPU
    // work — the main cause of the software-render stutter. FastTransformation
    // (nearest-neighbor) instead of smooth scaling since this frame is about
    // to be filtered and re-scaled again at paint time anyway.
    constexpr int kMaxPreviewDim = 720;
    if (image.width() > kMaxPreviewDim || image.height() > kMaxPreviewDim) {
        image = image.scaled(kMaxPreviewDim, kMaxPreviewDim,
                              Qt::KeepAspectRatio, Qt::FastTransformation);
    }

#ifdef WAKKAQT_FFMPEG_NATIVE
    if (!m_videoEffectChain.isEmpty() && videoEffectProcessor)
        image = videoEffectProcessor->process(image, m_videoEffectChain);
#endif

    videoRama->setImage(image);
}

void PreviewDialog::startEnhancementJob()
{
    if (previewInputAudioData.isEmpty())
        return;

    // Snapshot playback position so we can resume from here after re-processing
    if (amplifier)
        m_savedPlaybackPos = amplifier->getPosition();

    setPreviewControlsEnabled(false);
    progressBar->setValue(0);
    bannerLabel->setText("Enhancing Vocals");
    vocalVisualizer->clear();

    // Map combo index to scale preset name
    const QStringList scaleNames = {"chromatic","major","minor",
                                     "pentatonic_major","pentatonic_minor","blues"};
    const QString scaleName = (m_scaleIndex >= 0 && m_scaleIndex < scaleNames.size())
                            ? scaleNames[m_scaleIndex] : "chromatic";

    PreviewJob::EnhanceParams params;
    params.pitchCorrectionAmount = pitchCorrectionAmount;
    params.noiseReductionAmount  = noiseReductionAmount;
    params.retuneSpeedMs         = m_retuneSpeedMs;
    params.formantPreservation   = m_formantPreservation;
    params.reverbRoomSize        = m_reverbRoomSize;
    params.reverbDecay           = m_reverbDecay;
    params.reverbMix             = m_reverbMix;
    params.scalePreset           = scaleName;
    params.keyNote               = m_keyNote;

    progressTimer->start(55);
    if (!previewJob->enhance(previewInputAudioData, format, params)) {
        // Already busy re-processing a previous request — remember to run
        // this one (with whatever the sliders read at that time) once it's done.
        pendingPreviewRebuild = true;
    }
}

void PreviewDialog::onVocalsEnhanced(QByteArray tunedData)
{
    // An empty result means enhance() was cancelled (see
    // PreviewJob::cancelEnhance(), called from closeEvent()) rather than
    // genuinely finished — skip acting on it and skip auto-retriggering a
    // queued rebuild too, since the dialog is most likely closing.
    const bool wasCancelled = tunedData.isEmpty();

    if (!wasCancelled) {
        QFile audioFile(tunedRecorded);
        if (!audioFile.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to reopen PreviewDialog output file for writing header.";
        } else {
            const qint64 dataSize = tunedData.size();
            writeWavHeader(audioFile, format, dataSize, tunedData);
            audioFile.close();
        }

        amplifier->setAudioData(tunedData);
        amplifier->setAudioOffset(newOffset);
        amplifier->start();
        // Resume from where the user was listening instead of rewinding to the start
        amplifier->seekTo(m_savedPlaybackPos);
        amplifier->setPlaybackVol(!playbackMute_option->isChecked());
        syncVideoToAudio();

        progressTimer->stop();
        progressBar->setValue(100);
        bannerLabel->setText(QString("Vocal Enhancement complete!  Pitch %1% · Noise %2%")
                             .arg(int(pitchCorrectionAmount * 100.0))
                             .arg(int(noiseReductionAmount * 100.0)));
        setPreviewControlsEnabled(true);
    }

    if (!wasCancelled && pendingPreviewRebuild) {
        pendingPreviewRebuild = false;
        startEnhancementJob();
    }
}

void PreviewDialog::setPreviewControlsEnabled(bool enabled)
{
    startButton->setEnabled(enabled);
    stopButton->setEnabled(enabled);
    seekBackwardButton->setEnabled(enabled);
    seekForwardButton->setEnabled(enabled);
    volumeDial->setEnabled(enabled);
    offsetSlider->setEnabled(enabled);
    pitchCorrectionSlider->setEnabled(enabled);
    noiseReductionSlider->setEnabled(enabled);
    playbackMute_option->setEnabled(enabled);
    keyCombo->setEnabled(enabled);
    scaleCombo->setEnabled(enabled);
    retuneSpeedSlider->setEnabled(enabled);
    formantCheckBox->setEnabled(enabled);
    reverbRoomSlider->setEnabled(enabled);
    reverbDecaySlider->setEnabled(enabled);
    reverbMixSlider->setEnabled(enabled);
    applyButton->setEnabled(enabled);
}

void PreviewDialog::onOffsetSliderChanged(int value)
{
    newOffset = value;
    offsetLabel->setText(QString("Manual Sync Offset: %1 ms").arg(newOffset));
    if (amplifier)
        amplifier->setAudioOffset(newOffset);

}

void PreviewDialog::onPitchCorrectionChanged(int value)
{
    pitchCorrectionAmount = double(value) / 100.0;
    updateEnhancementLabels();
}

void PreviewDialog::onNoiseReductionChanged(int value)
{
    noiseReductionAmount = double(value) / 100.0;
    updateEnhancementLabels();
}

void PreviewDialog::onKeyChanged(int index)
{
    m_keyNote = std::clamp(index, 0, 11);
}

void PreviewDialog::onScaleChanged(int index)
{
    m_scaleIndex = index;
}

void PreviewDialog::onRetuneSpeedChanged(int value)
{
    m_retuneSpeedMs = double(value);
    if (retuneSpeedLabel)
        retuneSpeedLabel->setText(
            QString("Retune speed: %1 ms  (0=robotic, 500=natural)").arg(value));
}

void PreviewDialog::onFormantPreservationChanged(bool checked)
{
    m_formantPreservation = checked;
}

void PreviewDialog::updateEnhancementLabels()
{
    pitchCorrectionLabel->setText(
        QString("Pitch correction: %1% ++higher, more pitch correction")
            .arg(int(pitchCorrectionAmount * 100.0)));
    noiseReductionLabel->setText(
        QString("Noise reduction: %1% ++higher, more noise reduction")
            .arg(int(noiseReductionAmount * 100.0)));
}

double PreviewDialog::getVolume() const
{
    return volume;
}

qint64 PreviewDialog::getOffset() const
{
    return newOffset;
}

double PreviewDialog::getPitchCorrectionAmount() const
{
    return pitchCorrectionAmount;
}

double PreviewDialog::getNoiseReductionAmount() const
{
    return noiseReductionAmount;
}

void PreviewDialog::replayAudioPreview()
{
    amplifier->rewind();
    amplifier->setPlaybackVol(!playbackMute_option->isChecked());
    syncVideoToAudio();
}

void PreviewDialog::stopAudioPreview()
{
    qWarning() << "Set Volume factor to:" << volume;
    accept();
}

void PreviewDialog::seekForward()
{
    amplifier->seekForward();
    amplifier->setPlaybackVol(!playbackMute_option->isChecked());
    syncVideoToAudio();
}

void PreviewDialog::seekBackward()
{
    amplifier->seekBackward();
    amplifier->setPlaybackVol(!playbackMute_option->isChecked());
    syncVideoToAudio();
}

void PreviewDialog::onDialValueChanged(int value)
{
    volumeChangeTimer->stop();
    pendingVolumeValue = value;
    volumeChangeTimer->start(50);
}

void PreviewDialog::updateVolume()
{
    volume = static_cast<double>(pendingVolumeValue) / 100.0;
    amplifier->setVolumeFactor(volume);
    amplifier->setPlaybackVol(!playbackMute_option->isChecked());
    volumeLabel->setText(QString("Current Volume: %1% Elapsed Time: %2")
                         .arg(pendingVolumeValue)
                         .arg(chronos));
}

void PreviewDialog::updateChronos()
{
    chronos = amplifier->checkBufferState();
    volumeLabel->setText(QString("Current Volume: %1% Elapsed Time: %2")
                         .arg(pendingVolumeValue)
                         .arg(chronos));
    syncVideoToAudio();
}
