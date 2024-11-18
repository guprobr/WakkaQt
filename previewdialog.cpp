#include "complexes.h"
#include "previewdialog.h"
#include "audioamplifier.h"

#include <QApplication>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QProcess>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QFile>
#include <QTimer>
#include <QString>

PreviewDialog::PreviewDialog(qint64 offset, QWidget *parent)
    : QDialog(parent), audioOffset(offset), amplifier(nullptr), volume(1.0), pendingVolumeValue(100) {
    setWindowTitle("Audio Preview with Amplification");

    // Setup UI
    QVBoxLayout *layout = new QVBoxLayout(this);
    QHBoxLayout *controls = new QHBoxLayout();

    volumeDial = new QDial(this);  // Change from QSlider to QDial
    volumeDial->setRange(0, 300);   // 0% to 300% amplification
    volumeDial->setValue(100);       // Default 100% volume (no amplification)
    volumeDial->setNotchesVisible(true); // Show notches for better precision
    volumeDial->setToolTip("Adjust the knob to amplify or lower volume level");
    volumeDial->setFixedSize(200, 100);
    
    // Initialize UI elements
    QLabel *volumeBanner = new QLabel("Vocal Enhancement and Volume Amplification: This is a low-quality preview.\nSometimes it takes a while to encode the 3-pass vocal enhancement.\nPlease be patient. ", this);
    volumeBanner->setToolTip("You can adjust the final volume for the render output.");
    volumeBanner->setFont(QFont("Arial", 11));
    volumeBanner->setWordWrap(true);
    bannerLabel = new QLabel("Enhancing Vocals", this);
    bannerLabel->setToolTip("VocalEnhancement");
    bannerLabel->setFont(QFont("Arial", 16));
    volumeLabel = new QLabel("Current Volume: 100\%", this);
    volumeLabel->setToolTip("Values above 100\% amplifies, while values below reduce volume");
    volumeLabel->setFont(QFont("Courier", 14, QFont::Bold));
    startButton = new QPushButton("REWIND", this);
    startButton->setToolTip("Restart playback");
    stopButton = new QPushButton("Render Mix", this);
    stopButton->setToolTip("Apply volume and effect changes and begin rendering");
    seekForwardButton = new QPushButton(">>", this);
    seekForwardButton->setToolTip("Seek forward");
    seekBackwardButton = new QPushButton("<<", this);
    seekBackwardButton->setToolTip("Seek backwards");

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setFixedSize(QSize(600, 50));
    progressBar->setToolTip("VocalEnhancer progress bar");

    playbackMute_option = new QCheckBox("Preview vocals only", this);
    playbackMute_option->setToolTip("Check to mute backing track and hear vocals only");
    playbackMute_option->setChecked(false);
    playbackMute_option->setFont(QFont("Arial", 8));

    controls->addWidget(seekBackwardButton);
    controls->addWidget(volumeDial);
    controls->addWidget(seekForwardButton);

    layout->addWidget(volumeBanner);
    layout->addWidget(bannerLabel);
    layout->addWidget(progressBar);
    layout->addWidget(volumeLabel);
    layout->addWidget(startButton);
    layout->addWidget(playbackMute_option);
    layout->addLayout(controls);
    layout->addWidget(stopButton);
    layout->setAlignment(Qt::AlignHCenter);
    
    setFixedSize(800, 600);

    // Setup audio format
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::SampleFormat::Int16);  // Set sample format to 16 bit

    // Initialize Audio Amplifier
    amplifier.reset(new AudioAmplifier(format, this));

    // Connect UI elements
    connect(startButton, &QPushButton::clicked, this, &PreviewDialog::replayAudioPreview);
    connect(stopButton, &QPushButton::clicked, this, &PreviewDialog::stopAudioPreview);
    connect(seekBackwardButton, &QPushButton::clicked, this, &PreviewDialog::seekBackward);
    connect(seekForwardButton, &QPushButton::clicked, this, &PreviewDialog::seekForward);
    connect(volumeDial, &QDial::valueChanged, this, &PreviewDialog::onDialValueChanged);

    connect(playbackMute_option, &QCheckBox::stateChanged, this, [this]() {
        
        if ( playbackMute_option->isChecked() )
            amplifier->setPlaybackVol(false);
        else
            amplifier->setPlaybackVol(true);
        
     });

    // initialize the cronometer timer
    chronosTimer = new QTimer(this);
    connect(chronosTimer, &QTimer::timeout, this, &PreviewDialog::updateChronos);
    chronosTimer->start(250);

    // Initialize the volume change timer
    volumeChangeTimer = new QTimer(this);
    connect(volumeChangeTimer, &QTimer::timeout, this, &PreviewDialog::updateVolume);
    volumeChangeTimer->setSingleShot(true);  // Ensure the timer only runs once for each dial adjustment
    // Init the progress bar timer
    progressTimer = new QTimer(this);

    vocalEnhancer.reset(new VocalEnhancer(format, this));

}

PreviewDialog::~PreviewDialog() {
    amplifier->stop();
    amplifier.reset();
    vocalEnhancer.reset();
    delete volumeChangeTimer;
    delete progressTimer;
    delete chronosTimer;
}

void PreviewDialog::setAudioFile(const QString &filePath) {
    audioFilePath = filePath;
    qDebug() << "Audio file set to:" << audioFilePath;

    // Disable all controls while encoding
    startButton->setEnabled(false);
    stopButton->setEnabled(false);
    seekBackwardButton->setEnabled(false);
    seekForwardButton->setEnabled(false);
    volumeDial->setEnabled(false);
    playbackMute_option->setEnabled(false);

    // Initialize the QProcess
    QProcess *ffmpegProcess = new QProcess(this);

    // Temporary output file for the extracted audio
    QString tempAudioFile = tunedRecorded;

    // Prepare FFmpeg command
    QStringList arguments;
    arguments   << "-y" << "-i" << audioFilePath 
                << "-vn" << "-filter_complex" 
                << QString("%1 atrim=%2ms,asetpts=PTS-STARTPTS;")
                                                .arg(_audioMasterization)
                                                .arg(audioOffset)
                << "-ac" << "2" 
                << "-acodec" << "pcm_s16le" << "-ar" << "44100" << tempAudioFile;

    // Connect the finished signal
    connect(ffmpegProcess, &QProcess::finished, this, [this, tempAudioFile, ffmpegProcess](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            qWarning() << "FFmpeg process crashed or exited with error code:" << exitCode;
            QMessageBox::critical(this, "FFmpeg crashed", "FFmpeg process crashed!");
            startButton->setEnabled(true);
            stopButton->setEnabled(true);
            return;
        }

        // Check if the extraction was successful
        QFile audioFile(tempAudioFile);
        if (audioFile.exists() && audioFile.size() > 0) {
            audioFile.open(QIODevice::ReadOnly);
            QByteArray audioData = audioFile.readAll();
            audioFile.close();
            QFile::remove(tempAudioFile);

            // Prepare the VocalEnhancer to run in a separate thread
            QFutureWatcher<QByteArray> *watcher = new QFutureWatcher<QByteArray>(this);
            connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher]() {
                QByteArray tunedData = watcher->result();

                QFile audioFile(tunedRecorded);
                if (!audioFile.open(QIODevice::WriteOnly)) {
                    qWarning() << "Failed to reopen PreviewDialog output file for writing header.";
                    return;
                }
                qint64 dataSize = tunedData.size();
                writeWavHeader(audioFile, format, dataSize, tunedData);
                audioFile.close();

                amplifier->setAudioData(tunedData);
                amplifier->start();

                // Re-enable controls
                startButton->setEnabled(true);
                stopButton->setEnabled(true);
                seekBackwardButton->setEnabled(true);
                seekForwardButton->setEnabled(true);
                volumeDial->setEnabled(true);
                playbackMute_option->setEnabled(true);

                progressTimer->stop();
                this->progressBar->setValue(100);
                this->bannerLabel->setText("Vocal Enhancement complete!");

                vocalEnhancer.reset();
                watcher->deleteLater();  // Clean up watcher
            });
            
            // update progress bar each 55 ms
            connect(progressTimer, &QTimer::timeout, this, [this]() {
                this->progressBar->setValue(vocalEnhancer->getProgress());
                this->bannerLabel->setText(vocalEnhancer->getBanner());
            });
            progressTimer->start(55);

            // Run VocalEnhancer::enhance in a background thread using a lambda
            QFuture<QByteArray> future = QtConcurrent::run([this, audioData]() {
                return vocalEnhancer->enhance(audioData);
            });

            watcher->setFuture(future);

        } else {
            qWarning() << "Audio extraction failed or file is empty.";
            QMessageBox::critical(this, "Extraction failed", "Audio extraction failed or file is empty.");
        }

        ffmpegProcess->deleteLater(); // Clean up FFmpeg process object
    });

    // Start the FFmpeg process
    ffmpegProcess->start("ffmpeg", arguments);
    if (!ffmpegProcess->waitForStarted()) {
        qWarning() << "Failed to start FFmpeg.";
        delete ffmpegProcess; // Clean up if starting failed
        return;
    }
}


double PreviewDialog::getVolume() const {
    return volume;  // Returns the current volume level (we send this to render function)
}

void PreviewDialog::replayAudioPreview() {
    
    // Check if amplifier is playing
    if (amplifier->isPlaying()) {
        // Stop playback and reset the audio components before replaying
        chronosTimer->stop();
        amplifier->stop();
        amplifier->resetAudioComponents();  // Reset the amplifier components
    }
        
        amplifier->rewind();
        chronosTimer->start();
        amplifier->start();
        amplifier->setPlaybackVol(!playbackMute_option->isChecked());

}

void PreviewDialog::stopAudioPreview() {
    qWarning() << "Set Volume factor to:" << volume;
    accept();
}

void PreviewDialog::seekForward() {
    amplifier->seekForward();
    amplifier->setPlaybackVol(!playbackMute_option->isChecked());
}

void PreviewDialog::seekBackward() {
    amplifier->seekBackward();
    amplifier->setPlaybackVol(!playbackMute_option->isChecked());
}

void PreviewDialog::onDialValueChanged(int value) {
    // Stop the timer if it's already running
    volumeChangeTimer->stop();

    // Store the pending volume value
    pendingVolumeValue = value;

    // Start the timer to update volume after a delay (500 ms)
    volumeChangeTimer->start(250);
}

void PreviewDialog::updateVolume() {
    // Scale the dial value to a volume factor, where 100% = 1.0 (no amplification) and zero = mute
    volume = static_cast<double>(pendingVolumeValue) / 100.0;
    amplifier->setVolumeFactor(volume);
    amplifier->setPlaybackVol(!playbackMute_option->isChecked());

    // Update the volume label to inform the user
    volumeLabel->setText(QString("Current Volume: %1\% Elapsed Time: %2").arg(pendingVolumeValue).arg(chronos));
}

void PreviewDialog::updateChronos() {
    chronos = amplifier->checkBufferState();
     // Update the volume label to inform the user
    volumeLabel->setText(QString("Current Volume: %1\% Elapsed Time: %2").arg(pendingVolumeValue).arg(chronos));
}

