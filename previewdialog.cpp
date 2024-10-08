#include "previewdialog.h"
#include "audioamplifier.h"

#include <QVBoxLayout>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QString>

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent), amplifier(nullptr), volume(1.0), pendingVolumeValue(100) {
    setWindowTitle("Audio Preview with Amplification");

    // Setup UI
    QVBoxLayout *layout = new QVBoxLayout(this);
    QHBoxLayout *controls = new QHBoxLayout();

    volumeDial = new QDial(this);  // Change from QSlider to QDial
    volumeDial->setRange(0, 1000);   // 0% to 1000% amplification
    volumeDial->setValue(100);       // Default 100% volume (no amplification)
    volumeDial->setNotchesVisible(true); // Show notches for better precision
    volumeDial->setToolTip("Adjust the knob to amplify or lower volume level");

    // Initialize UI elements
    QLabel *volumeBanner = new QLabel("Volume Amplification: This is a low-quality preview. Final render will sound much better.", this);
    volumeBanner->setToolTip("While you review your performance you can adjust the volume of the final output.");
    volumeLabel = new QLabel("Current Volume: 100\%", this);
    volumeLabel->setToolTip("This is an approximation of what the final render will sound.");
    startButton = new QPushButton("REWIND", this);
    startButton->setToolTip("Restart recording preview");
    stopButton = new QPushButton("Render Mix", this);
    stopButton->setToolTip("Apply changes and begin rendering");
    seekForwardButton = new QPushButton(">>", this);
    seekForwardButton->setToolTip("Seek forward");
    seekBackwardButton = new QPushButton("<<", this);
    seekBackwardButton->setToolTip("Seek backwards");
    controls->addWidget(seekBackwardButton);
    controls->addWidget(seekForwardButton);

    layout->addWidget(volumeBanner);
    layout->addWidget(volumeDial);
    layout->addWidget(volumeLabel);
    layout->addWidget(startButton);
    layout->addLayout(controls);
    layout->addWidget(stopButton);

    //setLayout(layout);
    setFixedSize(800, 240);

    // Setup audio format
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::SampleFormat::Int16);  // Set sample format to Int16

    // Initialize Audio Amplifier
    amplifier = new AudioAmplifier(format, this);

    // Connect UI elements
    connect(startButton, &QPushButton::clicked, this, &PreviewDialog::replayAudioPreview);
    connect(stopButton, &QPushButton::clicked, this, &PreviewDialog::stopAudioPreview);
    connect(seekBackwardButton, &QPushButton::clicked, amplifier, &AudioAmplifier::seekBackward);
    connect(seekForwardButton, &QPushButton::clicked, amplifier, &AudioAmplifier::seekForward);
    connect(volumeDial, &QDial::valueChanged, this, &PreviewDialog::onDialValueChanged);

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

            // Start playback
            amplifier->start();
        } else {
            qWarning() << "Audio extraction failed or file is empty.";
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
        amplifier->stop();
        amplifier->resetAudioComponents();  // Reset the amplifier components
    }
        amplifier->rewind();
        amplifier->start();
}

void PreviewDialog::stopAudioPreview() {
    amplifier->stop();
    qWarning() << "Set Volume factor to:" << volume;
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
    volumeLabel->setText(QString("Current Volume: %1\%").arg(pendingVolumeValue));
}
