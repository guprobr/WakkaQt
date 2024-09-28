#include "previewdialog.h"
#include "audioamplifier.h"

#include <QVBoxLayout>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QString>

// PreviewDialog class implementation
PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent), amplifier(nullptr), volume(1.0), pendingVolumeValue(100) { // Initialize pendingVolumeValue
    setWindowTitle("Audio Preview with Amplification");

    // Setup UI
    QVBoxLayout *layout = new QVBoxLayout(this);

    volumeDial = new QDial(this);  // Change from QSlider to QDial
    volumeDial->setRange(0, 300);   // 0% to 300% amplification
    volumeDial->setValue(100);       // Default 100% volume (no amplification)
    volumeDial->setNotchesVisible(true); // Show notches for better precision

    // Initialize UI elements
    QLabel *volumeBanner = new QLabel("Volume Amplification", this);
    volumeLabel = new QLabel("Current Volume: 100%", this); // Initialize the volume label

    startButton = new QPushButton("PLAY", this);
    stopButton = new QPushButton("Render Mix", this);

    layout->addWidget(volumeBanner);
    layout->addWidget(volumeDial);
    layout->addWidget(volumeLabel); // Add the volume label to the layout
    layout->addWidget(startButton);
    layout->addWidget(stopButton);

    setLayout(layout);
    setFixedSize(800, 240);

    // Setup audio format
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::SampleFormat::Int16);  // Set sample format to Int16

    // Initialize Audio Amplifier
    amplifier = new AudioAmplifier(format, this);

    // Connect UI elements to functions
    connect(startButton, &QPushButton::clicked, this, &PreviewDialog::replayAudioPreview);
    connect(stopButton, &QPushButton::clicked, this, &PreviewDialog::stopAudioPreview);
    connect(volumeDial, &QDial::valueChanged, this, &PreviewDialog::onDialValueChanged);  // Connect to dial value change

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
    audioFilePath = filePath;  // Store the provided file path
    qDebug() << "Audio file set to:" << audioFilePath;

    // Initialize the QProcess
    QProcess *ffmpegProcess = new QProcess(this);

    // Temporary output file for the extracted audio
    QString tempAudioFile = QDir::temp().filePath("WakkaQt_extracted_audio.wav");

    // Prepare FFmpeg command
    QStringList arguments;
    arguments   << "-y" << "-i" << audioFilePath 
                << "-vn" << "-ac" << "2" 
                << "-acodec" << "pcm_s16le" << "-ar" << "44100" << tempAudioFile;

    // Connect the finished signal
    connect(ffmpegProcess, &QProcess::finished, this, [this, tempAudioFile, ffmpegProcess](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            qWarning() << "FFmpeg process crashed or exited with error code:" << exitCode;
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

            // Start playback if necessary
            amplifier->start();
        } else {
            qWarning() << "Audio extraction failed or file is empty.";
        }

        ffmpegProcess->deleteLater(); // Clean up process object
    });

    // Start the process
    ffmpegProcess->start("ffmpeg", arguments);
    if (!ffmpegProcess->waitForStarted()) {
        qWarning() << "Failed to start FFmpeg.";
        delete ffmpegProcess; // Clean up if starting failed
        return;
    }
}

double PreviewDialog::getVolume() const {
    return volume;  // Return the current volume level
}

void PreviewDialog::replayAudioPreview() {
    // Check if amplifier is playing
    if (amplifier->isPlaying()) {
        // Stop playback and reset the audio components before replaying
        amplifier->stop();
        amplifier->resetAudioComponents();  // Reset the amplifier components
    }

    //amplifier->rewind();
    amplifier->start();
    
}

void PreviewDialog::stopAudioPreview() {
    amplifier->stop();
    qWarning() << "Volume adj factor:" << volume;
    accept();
}

void PreviewDialog::onDialValueChanged(int value) {
    // Stop the timer if it's already running
    volumeChangeTimer->stop();

    // Store the pending volume value
    pendingVolumeValue = value;

    // Start the timer to update volume after a delay (500 ms)
    volumeChangeTimer->start(500);
}

void PreviewDialog::updateVolume() {
    // Scale the dial value to a volume factor, where 100% = 1.0 (no amplification) and zero = mute
    volume = static_cast<double>(pendingVolumeValue) / 100.0;
    amplifier->setVolumeFactor(volume);

    // Update the volume label to inform the user
    volumeLabel->setText(QString("Current Volume: %1%").arg(pendingVolumeValue));
}
