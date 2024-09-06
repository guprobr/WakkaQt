#include "mainwindow.h"
#include "sndwidget.h"
#include "previewdialog.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QProcess>
#include <QTime>
#include <QTimer>
#include <QElapsedTimer>

#include <QVBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>

#include <QVideoWidget>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QMediaCaptureSession>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioInput>

#include <QSysInfo> // For platform detection
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , isRecording(false)
{

    // Create video widget
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(640, 480);
    videoWidget->hide();

    // Create a QLabel to display the placeholder image
    placeholderLabel = new QLabel(this);
    QPixmap placeholderPixmap(":/images/logo.jpg");
    if (placeholderPixmap.isNull()) {
        qWarning() << "Failed to load placeholder image!";
    } else {
        placeholderLabel->setPixmap(placeholderPixmap.scaled(videoWidget->size(), Qt::KeepAspectRatio));
    }
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setGeometry(videoWidget->geometry());

    // Create buttons
    QPushButton *exitButton = new QPushButton("Exit", this);
    chooseVideoButton = new QPushButton("Load playback from disk", this);
    singButton = new QPushButton("♪ SING ♪", this);
    chooseInputButton = new QPushButton("Choose Input Device", this);
    renderAgainButton = new QPushButton("RENDER AGAIN", this);

    // Create the red recording indicator
    recordingIndicator = new QLabel(" ⬤ REC ", this);
    recordingIndicator->setStyleSheet("color: red;");
    recordingIndicator->setFixedSize(45, 45); // Adjust the size
    QHBoxLayout *indicatorLayout = new QHBoxLayout();
    indicatorLayout->addStretch(); // Add stretchable space to the left
    indicatorLayout->addWidget(recordingIndicator, 0, Qt::AlignCenter); // Center the indicator
    indicatorLayout->addStretch(); // Add stretchable space to the right

    // Instantiate the SndWidget (green waveform volume meter)
    soundLevelWidget = new SndWidget(this);
    soundLevelWidget->setMinimumSize(200, 100); // Set a minimum size

    deviceLabel = new QLabel("Selected Device: None", this);
    selectedDevice = QMediaDevices::defaultAudioInput();
    updateDeviceLabel(selectedDevice);
    soundLevelWidget->setInputDevice(selectedDevice);

    // YT downloader
    urlInput = new QLineEdit(this);
    fetchButton = new QPushButton("FETCH", this);
    downloadStatusLabel = new QLabel("Download YouTube URL", this);
    QHBoxLayout *fetchLayout = new QHBoxLayout;
    fetchLayout->addWidget(urlInput);
    fetchLayout->addWidget(fetchButton);
    fetchLayout->addWidget(downloadStatusLabel);

    // Create a widget to display output logs from yt-dlp and FFmpeg
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true); // prevent user to edit contents of the widget
    logTextEdit->append("Welcome to WakkaQt!");

    // Create a container widget for the layout
    QWidget *containerWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(containerWidget);
    layout->addWidget(placeholderLabel);
    layout->addWidget(videoWidget);
    layout->addWidget(singButton);
    layout->addWidget(chooseVideoButton);
    layout->addWidget(chooseInputButton);
    layout->addWidget(renderAgainButton);
    layout->addWidget(exitButton);
    layout->addLayout(indicatorLayout); 
    layout->addWidget(deviceLabel);
    layout->addWidget(soundLevelWidget);
    layout->addLayout(fetchLayout);
    layout->addWidget(logTextEdit);
    setCentralWidget(containerWidget);

    soundLevelWidget->setVisible(true); // show the SndWidget
    recordingIndicator->hide(); // hide the red indicator since we are not recording yet
    singButton->setEnabled(false); // Disable the SING button, since we have no video selected for playback yet
    renderAgainButton->setVisible(false); // this only shows after succesfull render
    exitButton->setVisible(false); // decided to remove this button since its the same as closing window
    deviceLabel->setVisible(false); // removed since Qt does not honour user input choice if change input src in OS

    // Connect button signals to slots
    connect(exitButton, &QPushButton::clicked, this, &QMainWindow::close);
    connect(chooseVideoButton, &QPushButton::clicked, this, &MainWindow::chooseVideo);
    connect(singButton, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(fetchButton, &QPushButton::clicked, this, &MainWindow::fetchVideo);
    connect(chooseInputButton, &QPushButton::clicked, this, &MainWindow::chooseInputDevice);
    connect(renderAgainButton, &QPushButton::clicked, this, &MainWindow::renderAgain);

    resetAudioComponents(false, true); 

}

void MainWindow::resetAudioComponents(bool willRecord, bool isStarting) {
    qDebug() << "Resetting audio components";

    if (!isStarting)
        disconnectAllSignals(); // Ensure all signals are disconnected

    // Reset the audio components
    audioInput.reset();         
    format.reset();        
    mediaRecorder.reset();      
    mediaCaptureSession.reset();
    player.reset();
    audioOutput.reset();
    camera.reset();

    if (willRecord) {
        qDebug() << "Choosing input device";
        chooseInputDevice(); // force choosing input device, if about to record
    }

    configureMediaComponents(); // Reinitialize components
}

void MainWindow::configureMediaComponents()
{
    qDebug() << "Reconfiguring media components";
    
    webcamRecorded = QDir::temp().filePath("WakkaQt_recorded.mp4");

    // Reinitialize components
    audioInput.reset(new QAudioInput(this));
    format.reset(new QMediaFormat);
    mediaRecorder.reset(new QMediaRecorder(this));
    mediaCaptureSession.reset(new QMediaCaptureSession(this));
    player.reset(new QMediaPlayer(this));
    audioOutput.reset(new QAudioOutput(this));
    camera.reset(new QCamera(this));

    // Setup video player
    player->setVideoOutput(videoWidget);
    player->setAudioOutput(audioOutput.data());

    // Reconfigure components
    audioInput->setDevice(selectedDevice);
    audioInput->setVolume(1.0f);
    updateDeviceLabel(selectedDevice);
    soundLevelWidget->setInputDevice(selectedDevice);

    format->setFileFormat(QMediaFormat::MPEG4);
    format->setVideoCodec(QMediaFormat::VideoCodec::H264);
    format->setAudioCodec(QMediaFormat::AudioCodec::AAC);
    mediaRecorder->setMediaFormat(*format);
    mediaRecorder->setOutputLocation(QUrl::fromLocalFile(webcamRecorded));
    mediaRecorder->setQuality(QMediaRecorder::HighQuality);

    // Connect signals
    connect(mediaRecorder.data(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    connect(mediaRecorder.data(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);

}

void MainWindow::chooseInputDevice()
{
    QList<QAudioDevice> devices = QMediaDevices::audioInputs();

    QStringList deviceNames;
    for (const QAudioDevice &device : devices) {
        deviceNames << device.description();
    }

    bool ok;
    QString selectedDeviceName = QInputDialog::getItem(this, "Select Input Device", "Input Device:", deviceNames, 0, false, &ok);

    if (ok && !selectedDeviceName.isEmpty()) {
        for (const QAudioDevice &device : devices) {
            if (device.description() == selectedDeviceName) {
                selectedDevice = device; // Store the selected device
                updateDeviceLabel(selectedDevice);
                soundLevelWidget->setInputDevice(selectedDevice);
                break;
            }
        }
    }
}

void MainWindow::updateDeviceLabel(const QAudioDevice &device) {
    if (deviceLabel) {
        deviceLabel->setText(QString("Input Device: %1").arg(device.description()));
    }
}

void MainWindow::chooseVideo()
{
    currentVideoFile = QFileDialog::getOpenFileName(this, "Open Playback File", QString(), "Video or Audio (*.mp4 *.avi *.mov *.mp3 *.wav *.flac)");
    if (!currentVideoFile.isEmpty()) {

        singButton->setEnabled(true);
        renderAgainButton->setVisible(false);

        player->setSource(QUrl::fromLocalFile(currentVideoFile)); 
        player->play(); // playback preview
        placeholderLabel->hide();
        videoWidget->show();

        logTextEdit->append("Playback preview. Press SING to start recording.");
    }
}

void MainWindow::startSingSession() {
  // Reinit timers
  playbackTimer.invalidate();
  recordingTimer.invalidate();

  playbackTimer.start(); // playback timer

  player->setSource(QUrl::fromLocalFile(currentVideoFile));

  // Disconnect any existing connections to avoid multiple connections
  disconnect(player.data(), &QMediaPlayer::mediaStatusChanged, this, nullptr);
  // Connect to mediaStatusChanged before playing
  connect(player.data(), &QMediaPlayer::mediaStatusChanged, player.data(), [this](QMediaPlayer::MediaStatus status) {
    qDebug() << status;
    if (status == QMediaPlayer::BufferedMedia) {
      recordingTimer.start(); // Start recording timer when playback starts
      qint64 playbackStartTime = playbackTimer.elapsed();
      logTextEdit->append(QString("Playback started at: %1 ms").arg(playbackStartTime));
    }
  });

  player->play(); // Start playback! 

}

// START RECORDING
void MainWindow::startRecording() {
    if (isRecording) {
        qWarning() << "Stop recording.";
        logTextEdit->append("Stop recording...");
        stopRecording();
        return;
    }

    resetAudioComponents(true, false);

    singButton->setEnabled(false);
    chooseVideoButton->setEnabled(false);
    fetchButton->setEnabled(false);
    chooseInputButton->setEnabled(false);

    mediaCaptureSession->setRecorder(mediaRecorder.data());
    mediaCaptureSession->setAudioInput(audioInput.data());
    mediaCaptureSession->setCamera(camera.data());

    // Show webcamera preview 
    previewWebcam.reset(new PreviewWebcam(this));
    mediaCaptureSession->setVideoOutput(previewWebcam->videoWidget);
    previewWebcam->show();

    camera->start();

    //player->setSource(QUrl::fromLocalFile(currentVideoFile)); 
    //player->play();
    startSingSession();

    mediaRecorder->record(); 
    isRecording = true;
    logTextEdit->append("Recording NOW!");
    recordingIndicator->show();

    QTimer::singleShot(1000, [this]() {
        QFile file(webcamRecorded);
        if (mediaRecorder->recorderState() != QMediaRecorder::RecordingState ) {
            QMessageBox::warning(this, "SORRY: Recording Error", "Recording has failed to start.");
            logTextEdit->append("Recording ERROR.");
            stopRecording();
        }
    });

    singButton->setText("Finish!");
    singButton->setEnabled(true);

}

// STOP RECORDING
void MainWindow::stopRecording() {
    if (!isRecording) {
        qWarning() << "Not recording.";
        logTextEdit->append("Tried to stop Recording, but we are not recording. ERROR.");
        return;
    }

    camera->stop();
    mediaRecorder->stop();
    previewWebcam->close();
    recordingIndicator->hide();
    isRecording = false;
   
    singButton->setText("♪ SING ♪");    

}

void MainWindow::handleRecorderError(QMediaRecorder::Error error) {
    qWarning() << "Recorder error:" << error << mediaRecorder->errorString();
    QMessageBox::warning(this, "Recording Error", "Recording has failed: " + mediaRecorder->errorString());
    stopRecording();
}

void MainWindow::onRecorderStateChanged(QMediaRecorder::RecorderState state) {
    if (state == QMediaRecorder::StoppedState) {
        qDebug() << "Recording stopped.";
        
        player->stop();
        videoWidget->hide();
        placeholderLabel->show();


        qint64 playbackStartTime = playbackTimer.elapsed();
        qint64 recordingStartTime = recordingTimer.elapsed();

        // Calc offset
        offset = playbackStartTime - recordingStartTime; //  milisseconds

        qDebug() << "Offset between playback and recording: " << offset << "ms";
        logTextEdit->append(QString("Offset between playback and recording: %1 secs").arg(QString::number(offset / 1000.0, 'f', 2)));

        // for safety, invalidate timers since recording finished
        playbackTimer.invalidate();
        recordingTimer.invalidate();

        // Check file length
        QFile file(webcamRecorded);
        if (file.size() > 0) {
            qDebug() << "Recording saved successfully";
            renderAgain();
        } else {
            qDebug() << "*FAILURE* File size is zero.";
            QMessageBox::warning(this, "SORRY: mediaRecorder ERROR", "File size is zero.");
            logTextEdit->append("Recording ERROR. File size is zero.");

            chooseVideoButton->setEnabled(true);
            fetchButton->setEnabled(true);
            chooseInputButton->setEnabled(true);
            singButton->setEnabled(false);
            resetAudioComponents(false, false);
        }
    }
}

void MainWindow::renderAgain()
{
    player->stop();
    resetAudioComponents(false, false);

    renderAgainButton->setVisible(false);
    chooseVideoButton->setEnabled(false);
    fetchButton->setEnabled(false);

    // choose where to save rendered file
    outputFilePath = QFileDialog::getSaveFileName(this, "Mix destination (default .MP4)", "", "Video or Audio Files (*.mp4 *.mkv *.avi *.mp3 *.flac *.wav)");
    if (!outputFilePath.isEmpty()) {
        // Check if the file path has an extension
        if (QFileInfo(outputFilePath).suffix().isEmpty()) {
            outputFilePath.append(".mp4");  // Append .mp4 if no extension is provided
        }
        // Show the preview dialog
        PreviewDialog dialog(this);
        dialog.setAudioFile(webcamRecorded);
        if (dialog.exec() == QDialog::Accepted)
        {
            double vocalVolume = dialog.getVolume();
            mixAndRender(webcamRecorded, currentVideoFile, outputFilePath, vocalVolume);
        } else {
            QMessageBox::warning(this, "Rendering Aborted!", "adjustment cancelled..");
            chooseVideoButton->setEnabled(true);
            fetchButton->setEnabled(true);
            chooseInputButton->setEnabled(true);
            singButton->setEnabled(false);             
        }
    } else {
        QMessageBox::warning(this, "Rendering aborted!", "Rendering was cancelled..");
        chooseVideoButton->setEnabled(true);
        fetchButton->setEnabled(true);
        chooseInputButton->setEnabled(true);
        singButton->setEnabled(false);
    }
}

void MainWindow::mixAndRender(const QString &webcamFilePath, const QString &videoFilePath, const QString &outputFilePath, double vocalVolume) {
    
    videoWidget->hide();
    placeholderLabel->show();
    singButton->setEnabled(false); 
    chooseInputButton->setEnabled(true);

    QString offsetArg = QString::number(offset / 1000.0, 'f', 2);  // Convert from ms to seconds

    int totalDuration = getMediaDuration(videoFilePath);  // Get the total duration
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);

    // Create QProcess instance
    QProcess *process = new QProcess(this);
    QStringList arguments;
    
    // Create QLabel for progress indication
    QLabel *progressLabel = new QLabel("Processing...", this);
    progressLabel->setAlignment(Qt::AlignCenter);
    // Get the main layout and add the progress label
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    layout->insertWidget(0, progressLabel, 0, Qt::AlignCenter);  // Add the label at the top center of the layout
    layout->insertWidget(0, progressBar, 10, Qt::AlignCenter);  // Add the label at the top center of the layout

    QFileInfo outfileInfo(outputFilePath);
    QFileInfo videofileInfo(videoFilePath);

    if (outfileInfo.suffix().toLower() == "mp3" || outfileInfo.suffix().toLower() == "flac" || outfileInfo.suffix().toLower() == "wav" \
    || videofileInfo.suffix().toLower() == "mp3" || videofileInfo.suffix().toLower() == "flac" || videofileInfo.suffix().toLower() == "wav" ) {
        arguments << "-y" // Overwrite output file if it exists
              << "-ss" << offsetArg
              << "-i" << webcamFilePath // recorded vocals
              << "-i" << videoFilePath // playback file
              << "-filter_complex"
              << QString("[0:a]afftdn=nf=-20:nr=10:nt=w,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200, \
              lv2=http\\\\://gareus.org/oss/lv2/fat1:c=mode=Auto|channelf=Any|bias=1.0|filter=0.1|offset=0.1|bendrange=2|corr=1.0, \
              aecho=1.0:1.0:84:0.21,treble=g=12,volume=%1[vocals]; \
              [1:a][vocals]amix=inputs=2:normalize=0;").arg(vocalVolume)
              << "-dither_method" << "shibata" // dithering
              << "-ac" << "2" // force stereo
              << outputFilePath;
    }
    else {
        arguments << "-y" // Overwrite output file if it exists
              << "-ss" << offsetArg 
              << "-i" << webcamFilePath // recorded vocals
              << "-i" << videoFilePath // playback file
              << "-filter_complex"
              << QString("[0:a]afftdn=nf=-20:nr=10:nt=w,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200, \
              lv2=http\\\\://gareus.org/oss/lv2/fat1:c=mode=Auto|channelf=Any|bias=1.0|filter=0.1|offset=0.1|bendrange=2|corr=1.0, \
              aecho=1.0:1.0:84:0.21,treble=g=12,volume=%1[vocals]; \
              [1:a][vocals]amix=inputs=2:normalize=0;   \
              [0:v]scale=s=1280x720[webcam];    \
              [1:v]scale=s=1280x720[video];     \
              [video][webcam]vstack;").arg(vocalVolume)
              << "-dither_method" << "shibata" // dithering
              << "-ac" << "2" // force stereo
              << "-s" << "1280x720"
              << outputFilePath;
    }
    
    // Connect signals to display output and errors
    connect(process, &QProcess::readyReadStandardOutput, [process, progressLabel, this]() {
        QByteArray outputData = process->readAllStandardOutput();
        QString output = QString::fromUtf8(outputData).trimmed();  // Convert to QString
        if (!output.isEmpty()) {
            qDebug() << "FFmpeg (stdout):" << output;
            progressLabel->setText("Processing... (FFmpeg is active)");
            this->logTextEdit->append("FFmpeg: " + output);
        }
    });
    connect(process, &QProcess::readyReadStandardError, [process, progressLabel, totalDuration, this]() {
        QByteArray errorData = process->readAllStandardError();
        QString output = QString::fromUtf8(errorData).trimmed();  // Convert to QString
        
        if (!output.isEmpty()) {
            qDebug() << "FFmpeg (stderr):" << output;
            progressLabel->setText("Processing... (FFmpeg is active)");
            this->logTextEdit->append("FFmpeg: " + output);
            
            // Update progress based on FFmpeg output
            updateProgress(output, this->progressBar, totalDuration);
        }
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [process, progressLabel](int exitCode, QProcess::ExitStatus exitStatus) {
        qDebug() << "FFmpeg finished with exit code" << exitCode << "and status" << (exitStatus == QProcess::NormalExit ? "NormalExit" : "Crash");
        progressLabel->hide();
        process->deleteLater(); // Clean up the process object
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, process, outputFilePath](int exitCode, QProcess::ExitStatus exitStatus) {
        qDebug() << "FFmpeg finished with exit code" << exitCode << "and status" << (exitStatus == QProcess::NormalExit ? "NormalExit" : "Crash");
        this->logTextEdit->append("FFmpeg finished.");
        delete this->progressBar;
        QFile file(outputFilePath);
        if (!file.exists()) {
            qDebug() << "Output path does not exist: " << outputFilePath;
            return;
        }

        qDebug() << "Setting media source to" << outputFilePath;
        player->setSource(QUrl::fromLocalFile(outputFilePath));
        
        player->play();
        placeholderLabel->hide();
        videoWidget->show();

        this->logTextEdit->append("Playing mix!");

        chooseVideoButton->setEnabled(true);
        fetchButton->setEnabled(true);
        renderAgainButton->setVisible(true);
    });

    // Start the process for FFmpeg command and arguments
    process->start("ffmpeg", arguments);

    if (!process->waitForStarted()) {
        qDebug() << "Failed to start FFmpeg process.";
        progressLabel->setText("Failed to start FFmpeg");
        logTextEdit->append("Failed to start FFmpeg process.");
        process->deleteLater();
        return;
    }

}

void MainWindow::updateProgress(const QString& output, QProgressBar* progressBar, int totalDuration) {
    QRegularExpression timeRegex("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
    QRegularExpressionMatch match = timeRegex.match(output);

    if (match.hasMatch()) {
        QString hoursStr = match.captured(1);
        QString minutesStr = match.captured(2);
        QString secondsStr = match.captured(3);
        QString millisecondsStr = match.captured(4);

        bool ok;
        int hours = hoursStr.toInt(&ok);
        int minutes = minutesStr.toInt(&ok);
        int seconds = secondsStr.toInt(&ok);
        int milliseconds = millisecondsStr.toInt(&ok);

        if (!ok) {
            qWarning() << "Error parsing time components";
            return;
        }

        // Convert time to milliseconds
        int elapsedMilliseconds = (hours * 3600 + minutes * 60 + seconds) * 1000 + milliseconds * 10;
        qDebug() << "Elapsed milliseconds:" << elapsedMilliseconds;

        // Convert total duration from seconds to milliseconds
        int totalDurationMilliseconds = totalDuration * 1000;
        if (totalDurationMilliseconds <= 0) {
            qWarning() << "Total duration is not valid";
            return;
        }

        // Calculate progress as a percentage
        int progressValue = static_cast<int>(100.0 * elapsedMilliseconds / totalDurationMilliseconds);
        qDebug() << "Progress value:" << progressValue;

        // Update the progress bar
        progressBar->setValue(progressValue);
    } else {
        qWarning() << "No match for time regex:" << output;
    }
}


int MainWindow::getMediaDuration(const QString &filePath) {
    QProcess ffprobeProcess;
    ffprobeProcess.start("ffprobe", QStringList() << "-v" << "error" << "-show_entries" << "format=duration" << "-of" << "default=noprint_wrappers=1:nokey=1" << filePath);
    ffprobeProcess.waitForFinished();
    QString durationStr = QString::fromUtf8(ffprobeProcess.readAllStandardOutput()).trimmed();
    
    bool ok;
    double duration = durationStr.toDouble(&ok);
    if (!ok || duration <= 0) {
        qWarning() << "Failed to get duration or duration is invalid:" << durationStr;
        return 0;
    }

    int durationInSeconds = static_cast<int>(duration);
    qDebug() << "Media duration:" << durationInSeconds << "seconds";
    return durationInSeconds;
}

void MainWindow::fetchVideo() {
    QString url = urlInput->text();
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please enter a YouTube URL.");
        return;
    }

    QString directory = QFileDialog::getExistingDirectory(this, "Choose Directory to Save Video");
    if (directory.isEmpty()) {
        return;
    }

    downloadStatusLabel->setText("Downloading...");
    QProcess *process = new QProcess(this);
    
    // Prepare the command for yt-dlp
    QStringList arguments;
    arguments << "--output" << (directory + "/%(title)s.%(ext)s")
              << url;

    connect(process, &QProcess::finished, [this, directory, process]() {
        downloadStatusLabel->setText("Download complete.");
        process->deleteLater();

        // Open the choose video dialog in the directory chosen to save video
        QString videoFilePath = QFileDialog::getOpenFileName(this, "Choose the downloaded playback or any another", directory, "Videos (*.mp4 *.mkv *.avi)");
        if (!videoFilePath.isEmpty()) {
            currentVideoFile = videoFilePath;  // Store the video playback file path
            player->setSource(QUrl::fromLocalFile(videoFilePath)); 
            player->play(); // play playback for preview
            placeholderLabel->hide();
            videoWidget->show();

            singButton->setEnabled(true); 
            logTextEdit->append("Playback preview. Press SING to start recording.");
            downloadStatusLabel->setText("Download YouTube URL");
        }
    });

    // Connect signals to handle standard output
    connect(process, &QProcess::readyReadStandardOutput, [this, process]() {
        QByteArray outputData = process->readAllStandardOutput();
        QString output = QString::fromUtf8(outputData).trimmed();  // Convert to QString
        if (!output.isEmpty()) {
            qDebug() << "yt-dlp:" << output;
            this->logTextEdit->append("yt-dlp: " + output);
        }
    });

    // Connect signals to handle standard error
    connect(process, &QProcess::readyReadStandardError, [this, process]() {
        QByteArray errorData = process->readAllStandardError();
        QString error = QString::fromUtf8(errorData).trimmed();  // Convert to QString
        if (!error.isEmpty()) {
            qDebug() << "yt-dlp Error:" << error;
            this->logTextEdit->append("yt-dlp: " + error);
        }
    });

    process->start("yt-dlp", arguments);
}

MainWindow::~MainWindow() {
    if (isRecording) {
        stopRecording();
    }

    disconnectAllSignals();

    player.reset();
    audioOutput.reset();
    mediaRecorder.reset();
    mediaCaptureSession.reset();
    audioInput.reset();
    format.reset();
    delete videoWidget;
    delete soundLevelWidget;
}

// Function to disconnect all signals
void MainWindow::disconnectAllSignals() {

    // Disconnect signals from mediaRecorder
    disconnect(mediaRecorder.get(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    disconnect(mediaRecorder.get(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);

}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (isRecording) {
        // Stop the recording safely
        stopRecording();
    }
    // Now allow the window to close
    event->accept(); // Call the base class implementation
}