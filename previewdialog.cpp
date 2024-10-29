#include "complexes.h"
#include "previewdialog.h"
#include "audioamplifier.h"
#include "vocalenhancer.h"

#include <QApplication>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QProcess>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
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
    QLabel *volumeBanner = new QLabel("Volume Amplification: This is a low-quality preview.\nSometimes it takes a while to encode the preview. Please be patient. ", this);
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

                watcher->deleteLater();  // Clean up watcher
            });

            // Run VocalEnhancer::enhance in a background thread using a lambda
            
            QFuture<QByteArray> future = QtConcurrent::run([this, audioData]() {
                VocalEnhancer vocalEnhancer(format);
                return vocalEnhancer.enhance(audioData);
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

void PreviewDialog::writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize, const QByteArray &pcmData)
{
    // Prepare header values
    qint32 sampleRate = format.sampleRate(); 
    qint16 numChannels = format.channelCount(); 
    qint16 bitsPerSample = format.bytesPerSample() * 8; // Convert bytes to bits
    qint32 byteRate = sampleRate * numChannels * (bitsPerSample / 8); // Calculate byte rate
    qint16 blockAlign = numChannels * (bitsPerSample / 8); // Calculate block align
    qint16 audioFormatValue = 1; // 1 for PCM format

    // Create header
    QByteArray header;
    header.append("RIFF");                                         // Chunk ID
    qint32 chunkSize = dataSize + 36;                            // Data size + 36 bytes for the header
    header.append(reinterpret_cast<const char*>(&chunkSize), sizeof(chunkSize)); // Chunk Size
    header.append("WAVE");                                         // Format
    header.append("fmt ");                                         // Subchunk 1 ID
    qint32 subchunk1Size = 16;                                   // Subchunk 1 Size (16 for PCM)
    header.append(reinterpret_cast<const char*>(&subchunk1Size), sizeof(subchunk1Size)); // Subchunk 1 Size
    header.append(reinterpret_cast<const char*>(&audioFormatValue), sizeof(audioFormatValue)); // Audio Format
    header.append(reinterpret_cast<const char*>(&numChannels), sizeof(numChannels));        // Channels
    header.append(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));          // Sample Rate
    header.append(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));                // Byte Rate
    header.append(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));            // Block Align
    header.append(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));      // Bits per Sample
    header.append("data");                                         // Subchunk 2 ID
    qint32 subchunk2Size = pcmData.size();                       // Size of the audio data
    header.append(reinterpret_cast<const char*>(&subchunk2Size), sizeof(subchunk2Size)); // Subchunk2 Size

    // Write the header and audio data to the file in one go
    file.write(header);
    file.write(pcmData); // Write audio data after the header

    qDebug() << "WAV header and audio data written.";
}