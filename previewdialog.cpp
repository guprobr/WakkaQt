#include "complexes.h"
#include "previewdialog.h"
#include "audioamplifier.h"
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

#include <QtConcurrent/QtConcurrentRun>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFont>
#include <QDebug>

PreviewDialog::PreviewDialog(qint64 offset, qint64 sysLatency, QWidget *parent)
    : QDialog(parent),
      audioOffset(offset),
      newOffset(sysLatency),
      amplifier(nullptr),
      volume(1.0),
      pendingVolumeValue(100)
{
    setWindowTitle("Audio Preview with Amplification");

    QVBoxLayout *layout = new QVBoxLayout(this);
    QHBoxLayout *controls = new QHBoxLayout();

    volumeDial = new QDial(this);
    volumeDial->setRange(0, 500);
    volumeDial->setValue(100);
    volumeDial->setNotchesVisible(true);
    volumeDial->setToolTip("Adjust the knob to amplify or lower volume level");
    volumeDial->setFixedSize(200, 100);

    QLabel *volumeBanner = new QLabel(
        "This is a low-quality preview.\n"
        "note vocals will be slightly louder in final mastering output.",
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
    vocalVisualizer->setToolTip("Vocal Visuailizer");
    vocalVisualizer->setMinimumHeight(110);

    controls->addWidget(seekBackwardButton);
    controls->addWidget(volumeDial);
    controls->addWidget(seekForwardButton);

    layout->addWidget(volumeBanner);
    layout->addWidget(bannerLabel);
    layout->addWidget(progressBar);
    layout->addWidget(vocalVisualizer);
    layout->addWidget(volumeLabel);
    layout->addWidget(startButton);
    layout->addWidget(playbackMute_option);
    layout->addLayout(controls);
    layout->addWidget(offsetLabel);
    layout->addWidget(offsetSlider);
    layout->addWidget(pitchCorrectionLabel);
    layout->addWidget(pitchCorrectionSlider);
    layout->addWidget(noiseReductionLabel);
    layout->addWidget(noiseReductionSlider);

    // Autotune controls row
    QHBoxLayout *tuneRow = new QHBoxLayout();
    tuneRow->addWidget(new QLabel("Key:", this));
    tuneRow->addWidget(keyCombo);
    tuneRow->addSpacing(12);
    tuneRow->addWidget(new QLabel("Scale:", this));
    tuneRow->addWidget(scaleCombo);
    layout->addLayout(tuneRow);
    layout->addWidget(retuneSpeedLabel);
    layout->addWidget(retuneSpeedSlider);
    layout->addWidget(formantCheckBox);
    layout->addWidget(reverbLabel);

    QHBoxLayout *reverbRow = new QHBoxLayout();
    reverbRow->addWidget(new QLabel("Room:", this));
    reverbRow->addWidget(reverbRoomSlider);
    reverbRow->addSpacing(8);
    reverbRow->addWidget(new QLabel("Decay:", this));
    reverbRow->addWidget(reverbDecaySlider);
    reverbRow->addSpacing(8);
    reverbRow->addWidget(new QLabel("Mix:", this));
    reverbRow->addWidget(reverbMixSlider);
    layout->addLayout(reverbRow);

    layout->addWidget(applyButton);

    layout->addWidget(stopButton);
    layout->setAlignment(Qt::AlignHCenter);
    setMinimumSize(680, 620);
    resize(800, 880);

    updateEnhancementLabels();

    format.setSampleRate(48000);  // default; overridden from WAV header in onExtracted
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::SampleFormat::Int16);

    amplifier.reset(new AudioAmplifier(format, this));
    vocalEnhancer.reset(new VocalEnhancer(format, this));

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
    connect(playbackMute_option, &QCheckBox::checkStateChanged, this, [this]() {
        amplifier->setPlaybackVol(!playbackMute_option->isChecked());
    });

    chronosTimer = new QTimer(this);
    connect(chronosTimer, &QTimer::timeout, this, &PreviewDialog::updateChronos);
    chronosTimer->start(250);

    volumeChangeTimer = new QTimer(this);
    connect(volumeChangeTimer, &QTimer::timeout, this, &PreviewDialog::updateVolume);
    volumeChangeTimer->setSingleShot(true);

    progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, this, [this]() {
        if (!vocalEnhancer)
            return;
        progressBar->setValue(vocalEnhancer->getProgress());
        bannerLabel->setText(vocalEnhancer->getBanner());
    });

    previewRebuildTimer = new QTimer(this);
    previewRebuildTimer->setSingleShot(true);
    connect(previewRebuildTimer, &QTimer::timeout,
            this, &PreviewDialog::startEnhancementJob);
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

    const QString tempAudioFile = tunedRecorded;
    const qint64  trimOffset    = audioOffset;

    auto onExtracted = [this, tempAudioFile]() {
        QFile audioFile(tempAudioFile);
        if (!audioFile.exists() || audioFile.size() <= 0) {
            qWarning() << "Audio extraction failed or file is empty.";
            QMessageBox::critical(this, "Extraction failed",
                                  "Audio extraction failed or file is empty.");
            setPreviewControlsEnabled(true);
            return;
        }
        if (!audioFile.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, "Open failed",
                                  "Failed to read extracted preview audio.");
            setPreviewControlsEnabled(true);
            return;
        }
        previewInputAudioData = audioFile.readAll();
        audioFile.close();
        QFile::remove(tempAudioFile);

        // Reinitialize audio pipeline if the extracted WAV's rate differs from the
        // current format (e.g. recording at 48000 Hz vs. previous default 44100 Hz).
        // Standard PCM WAV: channels at byte 22, sample rate at byte 24.
        if (previewInputAudioData.size() >= 44) {
            const int16_t wavCh   = *reinterpret_cast<const int16_t*>(
                                        previewInputAudioData.constData() + 22);
            const int32_t wavRate = *reinterpret_cast<const int32_t*>(
                                        previewInputAudioData.constData() + 24);
            if (wavRate > 0 && (wavRate != format.sampleRate() ||
                                wavCh   != format.channelCount())) {
                format.setSampleRate(wavRate);
                format.setChannelCount(wavCh);
                amplifier.reset(new AudioAmplifier(format, this));
                connect(amplifier.data(), &AudioAmplifier::vocalPreviewChunk,
                        vocalVisualizer, &AudioVisualizerWidget::updateVisualization);
                vocalEnhancer.reset(new VocalEnhancer(format, this));
                qDebug() << "PreviewDialog: audio pipeline reinitialized at"
                         << wavRate << "Hz," << wavCh << "ch";
            }
        }

        startEnhancementJob();
    };

#ifdef WAKKAQT_FFMPEG_NATIVE
    // Run extraction in a thread so the UI stays responsive
    [[maybe_unused]] auto extractFuture = QtConcurrent::run([=]() {
        // Extract stereo (no mono hint — VocalEnhancer handles channel mixing internally)
        const bool ok = FFmpegNative::extractAudio(audioFilePath, tempAudioFile,
                                                    trimOffset);
        QMetaObject::invokeMethod(this, [this, ok, onExtracted]() {
            if (!ok) {
                QMessageBox::critical(this, "Extraction failed",
                                      "Native audio extraction failed.");
                setPreviewControlsEnabled(true);
                return;
            }
            onExtracted();
        }, Qt::QueuedConnection);
    });
#else
    QProcess *ffmpegProcess = new QProcess(this);
    QStringList arguments;
    arguments << "-y"
              << "-i" << audioFilePath
              << "-vn"
              << "-filter_complex"
              << QString("%1 atrim=%2ms,asetpts=PTS-STARTPTS;")
                     .arg(_audioEnhance).arg(trimOffset)
              << "-ac" << "2"
              << "-acodec" << "pcm_s16le"
              << "-async" << "1"
              << tempAudioFile;

    connect(ffmpegProcess, &QProcess::finished, this,
            [this, onExtracted, ffmpegProcess](int exitCode, QProcess::ExitStatus exitStatus) {
        ffmpegProcess->deleteLater();
        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            qWarning() << "FFmpeg exited with code" << exitCode;
            QMessageBox::critical(this, "FFmpeg error", "FFmpeg process failed.");
            setPreviewControlsEnabled(true);
            return;
        }
        onExtracted();
    });

    ffmpegProcess->start("ffmpeg", arguments);
    if (!ffmpegProcess->waitForStarted()) {
        qWarning() << "Failed to start FFmpeg.";
        ffmpegProcess->deleteLater();
        setPreviewControlsEnabled(true);
    }
#endif
}

void PreviewDialog::startEnhancementJob()
{
    if (previewInputAudioData.isEmpty())
        return;

    if (enhanceWatcher && !enhanceWatcher->isFinished()) {
        pendingPreviewRebuild = true;
        return;
    }

    pendingPreviewRebuild = false;

    if (enhanceWatcher) {
        enhanceWatcher->deleteLater();
        enhanceWatcher = nullptr;
    }

    // Snapshot playback position so we can resume from here after re-processing
    if (amplifier)
        m_savedPlaybackPos = amplifier->getPosition();

    setPreviewControlsEnabled(false);
    progressBar->setValue(0);
    bannerLabel->setText("Enhancing Vocals");
    vocalVisualizer->clear();

    vocalEnhancer->setPitchCorrectionAmount(pitchCorrectionAmount);
    vocalEnhancer->setNoiseReductionAmount(noiseReductionAmount);
    vocalEnhancer->setRetuneSpeed(m_retuneSpeedMs);
    vocalEnhancer->setFormantPreservation(m_formantPreservation);
    vocalEnhancer->setReverbRoomSize(m_reverbRoomSize);
    vocalEnhancer->setReverbDecay(m_reverbDecay);
    vocalEnhancer->setReverbMix(m_reverbMix);
    // Map combo index to scale preset name
    const QStringList scaleNames = {"chromatic","major","minor",
                                     "pentatonic_major","pentatonic_minor","blues"};
    const QString scaleName = (m_scaleIndex >= 0 && m_scaleIndex < scaleNames.size())
                            ? scaleNames[m_scaleIndex] : "chromatic";
    vocalEnhancer->setScalePreset(scaleName, m_keyNote);

    enhanceWatcher = new QFutureWatcher<QByteArray>(this);
    connect(enhanceWatcher, &QFutureWatcher<QByteArray>::finished, this, [this]() {
        const QByteArray tunedData = enhanceWatcher->result();

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

        progressTimer->stop();
        progressBar->setValue(100);
        bannerLabel->setText(QString("Vocal Enhancement complete!  Pitch %1% · Noise %2%")
                             .arg(int(pitchCorrectionAmount * 100.0))
                             .arg(int(noiseReductionAmount * 100.0)));
        setPreviewControlsEnabled(true);

        QFutureWatcher<QByteArray> *finishedWatcher = enhanceWatcher;
        enhanceWatcher = nullptr;
        finishedWatcher->deleteLater();

        if (pendingPreviewRebuild) {
            pendingPreviewRebuild = false;
            startEnhancementJob();
        }
    });

    progressTimer->start(55);
    auto future = QtConcurrent::run([this, audioData = previewInputAudioData]() {
        return vocalEnhancer->enhance(audioData);
    });
    enhanceWatcher->setFuture(future);
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
}

void PreviewDialog::seekBackward()
{
    amplifier->seekBackward();
    amplifier->setPlaybackVol(!playbackMute_option->isChecked());
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
}
