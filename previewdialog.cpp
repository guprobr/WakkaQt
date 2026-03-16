#include "complexes.h"
#include "previewdialog.h"
#include "audioamplifier.h"

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
    layout->addWidget(stopButton);
    layout->setAlignment(Qt::AlignHCenter);
    setFixedSize(800, 760);

    updateEnhancementLabels();

    format.setSampleRate(44100);
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

    QProcess *ffmpegProcess = new QProcess(this);
    const QString tempAudioFile = tunedRecorded;

    QStringList arguments;
    arguments << "-y"
              << "-i" << audioFilePath
              << "-vn"
              << "-filter_complex"
              << QString("%1 atrim=%2ms,asetpts=PTS-STARTPTS,aresample=44100;")
                    .arg(_audioEnhance)
                    .arg(audioOffset)
              << "-ac" << "2"
              << "-acodec" << "pcm_s16le"
              << "-ar" << "44100"
              << "-async" << "1"
              << tempAudioFile;

    connect(ffmpegProcess, &QProcess::finished, this,
            [this, tempAudioFile, ffmpegProcess](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            qWarning() << "FFmpeg process crashed or exited with error code:" << exitCode;
            QMessageBox::critical(this, "FFmpeg crashed", "FFmpeg process crashed!");
            setPreviewControlsEnabled(true);
            ffmpegProcess->deleteLater();
            return;
        }

        QFile audioFile(tempAudioFile);
        if (!audioFile.exists() || audioFile.size() <= 0) {
            qWarning() << "Audio extraction failed or file is empty.";
            QMessageBox::critical(this, "Extraction failed", "Audio extraction failed or file is empty.");
            setPreviewControlsEnabled(true);
            ffmpegProcess->deleteLater();
            return;
        }

        if (!audioFile.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, "Open failed", "Failed to read extracted preview audio.");
            setPreviewControlsEnabled(true);
            ffmpegProcess->deleteLater();
            return;
        }

        previewInputAudioData = audioFile.readAll();
        audioFile.close();
        QFile::remove(tempAudioFile);

        startEnhancementJob();
        ffmpegProcess->deleteLater();
    });

    ffmpegProcess->start("ffmpeg", arguments);
    if (!ffmpegProcess->waitForStarted()) {
        qWarning() << "Failed to start FFmpeg.";
        ffmpegProcess->deleteLater();
        setPreviewControlsEnabled(true);
    }
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

    setPreviewControlsEnabled(false);
    progressBar->setValue(0);
    bannerLabel->setText("Enhancing Vocals");
    vocalVisualizer->clear();

    vocalEnhancer->setPitchCorrectionAmount(pitchCorrectionAmount);
    vocalEnhancer->setNoiseReductionAmount(noiseReductionAmount);

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
        amplifier->rewind();
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
    if (!previewInputAudioData.isEmpty())
        previewRebuildTimer->start(50);
}

void PreviewDialog::onNoiseReductionChanged(int value)
{
    noiseReductionAmount = double(value) / 100.0;
    updateEnhancementLabels();
    if (!previewInputAudioData.isEmpty())
        previewRebuildTimer->start(50);
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
