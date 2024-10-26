#include "complexes.h"
#include "previewdialog.h"
#include "audioamplifier.h"

#include <QMessageBox>
#include <QVBoxLayout>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QString>

PreviewDialog::PreviewDialog(qint64 offset, QWidget *parent)
    : QDialog(parent), audioOffset(offset), amplifier(nullptr), volume(1.0), pendingVolumeValue(100) {
    setWindowTitle("Audio Preview with Amplification");

    // Setup UI
    QVBoxLayout *layout = new QVBoxLayout(this);
    QHBoxLayout *controls = new QHBoxLayout();

    volumeDial = new QDial(this);  // Change from QSlider to QDial
    volumeDial->setRange(0, 1000);   // 0% to 1000% amplification
    volumeDial->setValue(100);       // Default 100% volume (no amplification)
    volumeDial->setNotchesVisible(true); // Show notches for better precision
    volumeDial->setToolTip("Adjust the knob to amplify or lower volume level");
    volumeDial->setFixedSize(200, 100);
    
    // Initialize UI elements
    QLabel *volumeBanner = new QLabel("Volume Amplification: This is a low-quality preview.\nChoose effects if you have the plugins and click to apply for previewing.\nSometimes it takes a while to encode the new preview. ", this);
    volumeBanner->setToolTip("While you review your performance you can adjust the volume for the render output.");
    volumeBanner->setFont(QFont("Arial", 10));
    volumeBanner->setWordWrap(true);
    volumeLabel = new QLabel("Current Volume: 100\%", this);
    volumeLabel->setToolTip("Values above 100\% amplifies, while values below reduce volume");
    volumeLabel->setFont(QFont("Courier", 12, QFont::Bold));
    startButton = new QPushButton("REWIND", this);
    startButton->setToolTip("Restart playback");
    stopButton = new QPushButton("Render Mix", this);
    stopButton->setToolTip("Apply volume and effect changes and begin rendering");
    seekForwardButton = new QPushButton(">>", this);
    seekForwardButton->setToolTip("Seek forward");
    seekBackwardButton = new QPushButton("<<", this);
    seekBackwardButton->setToolTip("Seek backwards");

    playbackMute_option = new QCheckBox("Preview vocals only", this);
    playbackMute_option->setToolTip("Check to mute backing track and hear vocals only");
    playbackMute_option->setChecked(false);
    playbackMute_option->setFont(QFont("Arial", 8));

    controls->addWidget(seekBackwardButton);
    controls->addWidget(volumeDial);
    controls->addWidget(seekForwardButton);

    layout->addWidget(volumeBanner);
    layout->addWidget(volumeLabel);
    layout->addWidget(startButton);
    layout->addWidget(playbackMute_option);
    layout->addLayout(controls);
    layout->addWidget(stopButton);
    layout->setAlignment(Qt::AlignHCenter);
    
    setFixedSize(800, 480);

    // Setup audio format
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::SampleFormat::Int16);  // Set sample format to 16 bit

    // Initialize Audio Amplifier
    amplifier = new AudioAmplifier(format, this);

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

    chronosTimer = new QTimer(this);
    connect(chronosTimer, &QTimer::timeout, this, &PreviewDialog::updateChronos);
    chronosTimer->start(250);

    // Initialize the volume change timer
    volumeChangeTimer = new QTimer(this);
    connect(volumeChangeTimer, &QTimer::timeout, this, &PreviewDialog::updateVolume);
    volumeChangeTimer->setSingleShot(true);  // Ensure the timer only runs once for each dial adjustment
}

PreviewDialog::~PreviewDialog() {
    amplifier->stop();
    delete amplifier;
}

void PreviewDialog::setAudioFile(const QString &filePath) {
    audioFilePath = filePath;
    qDebug() << "Audio file set to:" << audioFilePath;
    
    // disable all controls while encoding
    startButton->setEnabled(false);
    stopButton->setEnabled(false);
    seekBackwardButton->setEnabled(false);
    seekForwardButton->setEnabled(false);
    volumeDial->setEnabled(false);

    // Initialize the QProcess
    QProcess *ffmpegProcess = new QProcess(this);

    // Temporary output file for the extracted audio
    QString tempAudioFile = QDir::temp().filePath("WakkaQt_extracted_audio.wav");

    // Prepare FFmpeg command
    QStringList arguments;
    arguments   << "-y" << "-i" << audioFilePath 
                << "-vn" << "-filter_complex" 
                << QString("%1 atrim=%2ms;")
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

            // Set the audio data in the AudioAmplifier
            amplifier->setAudioData(audioData);

            // Start playback
            amplifier->start();

            startButton->setEnabled(true);
            stopButton->setEnabled(true);
            seekBackwardButton->setEnabled(true);
            seekForwardButton->setEnabled(true);
            volumeDial->setEnabled(true);

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
    chronosTimer->stop();
    amplifier->stop();
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
