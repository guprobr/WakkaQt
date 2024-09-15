#include "mainwindow.h"
#include "sndwidget.h"
#include "previewdialog.h"

#include <QGuiApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QProcess>
#include <QTime>
#include <QTimer>

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

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsVideoItem>
#include <QGraphicsProxyWidget>

#include <QVideoWidget>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioInput>

#include <QSysInfo> // For platform detection
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , isRecording(false)
{

    // acquire app palette
    QPalette palette = this->palette();
    windowTextColor = palette.color(QPalette::WindowText);

    // Create video widget
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(1024, 480);
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

    //////////////// Create the scene and view
    scene = new QGraphicsScene(this);
    previewView = new QGraphicsView(scene, this);
    previewView->setMinimumSize(640, 200);
    previewView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Get the size of the view (the window size when it is first created)
    qreal viewWidth = previewView->width();
    qreal viewHeight = previewView->height();
    // Create the webcam video previewItem and set it in the center
    previewItem = new QGraphicsVideoItem; 
    previewItem->setSize(QSizeF(640, 140)); // size for the webcam video preview
    // Create a QGraphicsPixmapItem for placeholder while webcam preview is not onscreen
    wakkaLogoItem = new QGraphicsPixmapItem(placeholderPixmap.scaled(640, 140, Qt::AspectRatioMode::IgnoreAspectRatio, Qt::SmoothTransformation));
    // Calculate position to center the previewItem
    qreal previewX = (viewWidth - previewItem->boundingRect().width()) / 2;
    qreal previewY = (viewHeight - previewItem->boundingRect().height()) / 2; 
    previewItem->setPos(previewX, previewY);
    wakkaLogoItem->setPos(previewX, previewY);
    scene->addItem(previewItem);
    scene->addItem(wakkaLogoItem);
    previewItem->hide();
    wakkaLogoItem->show();
    // Create durationTextItem and position it at the bottom-center
    durationTextItem = new QGraphicsTextItem;
    durationTextItem->setDefaultTextColor(palette.color(QPalette::Text));
    durationTextItem->setFont(QFont("Helvetica", 12));
    // Set text width to 50% of the view width
    qreal textWidth = viewWidth * 0.50;
    durationTextItem->setTextWidth(textWidth);
    // Calculate position to center the durationTextItem at the bottom
    qreal textX = viewWidth / 2;
    qreal textY = viewHeight - durationTextItem->boundingRect().height();
    durationTextItem->setPos(textX, textY);
    durationTextItem->setPlainText("Welcome to WakkaQt v0.1 alpha");
    scene->addItem(durationTextItem);
    // Set the scene rect based on the initial view size
    scene->setSceneRect(0, 0, viewWidth, viewHeight);

    // Create buttons
    QPushButton *exitButton = new QPushButton("Exit", this);
    chooseVideoButton = new QPushButton("Load playback from disk", this);
    singButton = new QPushButton("♪ SING ♪", this);
    chooseInputButton = new QPushButton("Choose Input Device", this);
    renderAgainButton = new QPushButton("RENDER AGAIN", this);

    // Create the red recording indicator
    recordingIndicator = new QLabel("⦿ REC", this);
    recordingIndicator->setStyleSheet("color: red;");
    recordingIndicator->setFixedSize(64, 12); // Adjust the size
    QHBoxLayout *indicatorLayout = new QHBoxLayout();
    indicatorLayout->addStretch(); // Add stretchable space to the left
    indicatorLayout->addWidget(recordingIndicator, 0, Qt::AlignCenter); // Center the indicator
    indicatorLayout->addStretch(); // Add stretchable space to the right

    // Instantiate the SndWidget (green waveform volume meter)
    soundLevelWidget = new SndWidget(this);
    soundLevelWidget->setMinimumSize(200, 20); // Set a minimum size

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
    logTextEdit->append("Welcome to WakkaQt v0.1 alpha!");
    connect(logTextEdit, &QTextEdit::textChanged, this, [=]() {
        logTextEdit->moveCursor(QTextCursor::End);  // Move cursor to the end
        logTextEdit->ensureCursorVisible();
    });

    // Create a container widget for the layout
    QWidget *containerWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(containerWidget);
    layout->addLayout(indicatorLayout);
    layout->addWidget(previewView);
    layout->addWidget(soundLevelWidget);
    layout->addWidget(placeholderLabel);
    layout->addWidget(videoWidget);
    layout->addWidget(singButton);
    layout->addWidget(chooseVideoButton);
    layout->addWidget(chooseInputButton);
    layout->addWidget(renderAgainButton);
    layout->addWidget(exitButton);
    layout->addWidget(deviceLabel);
    layout->addLayout(fetchLayout);
    layout->addWidget(logTextEdit);
    setCentralWidget(containerWidget);

    soundLevelWidget->setVisible(true); // Show the SndWidget
    recordingIndicator->hide(); // Hide the red indicator since we are not recording yet
    singButton->setEnabled(false); // Disable the SING button, since we have no video selected for playback yet
    renderAgainButton->setVisible(false); // This only shows after successful render
    exitButton->setVisible(false); // Decided to remove this button since it's the same as closing window
    deviceLabel->setVisible(false); // Removed since Qt does not honor user input choice if change input src in OS

    // Connect button signals to slots
    connect(exitButton, &QPushButton::clicked, this, &QMainWindow::close);
    connect(chooseVideoButton, &QPushButton::clicked, this, &MainWindow::chooseVideo);
    connect(singButton, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(fetchButton, &QPushButton::clicked, this, &MainWindow::fetchVideo);
    connect(chooseInputButton, &QPushButton::clicked, this, &MainWindow::chooseInputDevice);
    connect(renderAgainButton, &QPushButton::clicked, this, &MainWindow::renderAgain);

    playbackTimer = new QTimer(this);
    connect(playbackTimer, &QTimer::timeout, this, &MainWindow::updatePlaybackDuration);

    resetAudioComponents(true);
}

void MainWindow::resetAudioComponents(bool isStarting) {
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

    configureMediaComponents(); // Reinitialize components
}

void MainWindow::configureMediaComponents()
{
    qDebug() << "Reconfiguring media components";
    
    webcamRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.mp4");

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

    connect(mediaRecorder.data(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    connect(mediaRecorder.data(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);

    mediaRecorder->setMediaFormat(*format);
    mediaRecorder->setOutputLocation(QUrl::fromLocalFile(webcamRecorded));
    mediaRecorder->setQuality(QMediaRecorder::VeryHighQuality);

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

        resetAudioComponents(false);

        singButton->setEnabled(true);
        renderAgainButton->setVisible(false);

        addProgressBarToScene(scene, getMediaDuration(currentVideoFile));
        player->setSource(QUrl::fromLocalFile(currentVideoFile)); 
        player->play(); // playback preview
        playbackTimer->start(1000); 

        placeholderLabel->hide();
        videoWidget->show();

        logTextEdit->append("Playback preview. Press SING to start recording.");
    }
}


// START RECORDING //
void MainWindow::startRecording() {
try {
        if (isRecording) {
            qWarning() << "Stop recording.";
            logTextEdit->append("Stop recording...");
            stopRecording();
            return;
        }

        singButton->setEnabled(false);
        chooseVideoButton->setEnabled(false);
        fetchButton->setEnabled(false);
        chooseInputButton->setEnabled(false);

        chooseInputDevice();
        resetAudioComponents(false);

        playbackEventTime = QDateTime::currentMSecsSinceEpoch();
        connect(player.data(), &QMediaPlayer::mediaStatusChanged, this, &MainWindow::onPlayerMediaStatusChanged);
        player->setSource(QUrl::fromLocalFile(currentVideoFile));
        playbackTimer->start(1000); // Update every second
        addProgressBarToScene(scene, getMediaDuration(currentVideoFile));
        player->play();
                
        mediaCaptureSession->setCamera(camera.data());
        mediaCaptureSession->setAudioInput(audioInput.data());
        mediaCaptureSession->setRecorder(mediaRecorder.data());

        if (!recordingCheckTimer) {
            recordingCheckTimer.reset(new QTimer(this));
            connect(recordingCheckTimer.data(), &QTimer::timeout, this, &MainWindow::checkRecordingStart);
            recordingCheckTimer->start(55);
        }
               
    } catch (const std::exception &e) {
        logTextEdit->append("Error during startRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }
}

void MainWindow::checkRecordingStart() {
    if (mediaRecorder->duration() > 0) {
        recordingEventTime = QDateTime::currentMSecsSinceEpoch() + mediaRecorder->duration();
        qDebug() << "Detected recording started. Duration gap:" << mediaRecorder->duration();

        recordingCheckTimer->stop();
        recordingCheckTimer.reset();

    // fixme: try to detect lockdown
        if ( mediaRecorder->duration() > 300 ) {
            qWarning() << "Something is wrong. Aborting recording session. SORRY";
            QMessageBox::warning(this, "QtMediaRecorder unstable", "SORRY, let's try again.");
            logTextEdit->append("detected unstable mediaRecorder. PLEASE TRY AGAIN SORRY");
            handleRecordingError();
        }
    }
}

void MainWindow::onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::BufferedMedia) {
        if (mediaRecorder) {
            qDebug() << "Player is buffered. Start recording...";
            camera->start();
            mediaRecorder->record();
        }
    }
}

void MainWindow::onRecorderStateChanged(QMediaRecorder::RecorderState state) {
    if (mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {
        isRecording = true;
        recordingIndicator->show();
        singButton->setText("Finish!");
        singButton->setEnabled(true);

        mediaCaptureSession->setVideoOutput(previewItem);
        wakkaLogoItem->hide();
        previewItem->show();
    }
    if (state == QMediaRecorder::StoppedState) {
        qDebug() << "Recording stopped.";
        
        player->stop();
        playbackTimer->stop();

        videoWidget->hide();
        placeholderLabel->show();

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
            resetAudioComponents(false);
        }
    }
}

void MainWindow::stopRecording() {
    try {
        if (!isRecording) {
            qWarning() << "Not recording.";
            logTextEdit->append("Tried to stop Recording, but we are not recording. ERROR.");
            return;
        }

        camera->stop();
        mediaRecorder->stop();
        previewItem->hide();
        wakkaLogoItem->show();
        recordingIndicator->hide();
        isRecording = false;
        singButton->setText("♪ SING ♪");

        offset = ( recordingEventTime - playbackEventTime );

        qDebug() << "Offset between playback start and recording start: " << offset << " ms";
        logTextEdit->append(QString("Offset between playback start and recording start: %1 ms").arg(offset));

    } catch (const std::exception &e) {
        logTextEdit->append("Error during stopRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }
}

void MainWindow::handleRecorderError(QMediaRecorder::Error error) {
    qWarning() << "Recorder error:" << error << mediaRecorder->errorString();
    
    logTextEdit->append("Recording Error: " + mediaRecorder->errorString());

    QMessageBox::warning(this, "Recording Error", "An error occurred while recording: " + mediaRecorder->errorString());

    try {
        if (mediaRecorder ) {
            qDebug() << "Stopping media recorder due to error...";
            mediaRecorder->stop();
        }
    } catch (const std::exception &e) {
        logTextEdit->append("Error during stopRecording (while handling error): " + QString::fromStdString(e.what()));
    }

    handleRecordingError(); 
}

void MainWindow::handleRecordingError() {
    logTextEdit->append("Attempting to recover from recording error...");

    if (mediaRecorder ) {
            qDebug() << "Stopping media recorder due to error...";
            mediaRecorder->stop();
            camera->stop();
            previewItem->hide();
            wakkaLogoItem->show();
            recordingIndicator->hide();
            isRecording = false;
        }

    resetAudioComponents(false);

    singButton->setEnabled(false);
    singButton->setText("SING");
    chooseVideoButton->setEnabled(true);
    fetchButton->setEnabled(true);
    chooseInputButton->setEnabled(true);

    placeholderLabel->show();
    videoWidget->hide();
}

// render //
void MainWindow::renderAgain()
{
    player->stop();
    playbackTimer->stop();
    resetAudioComponents(false);

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

        // High resolution or fast render?
        int response = QMessageBox::question(
            this, 
            "Resolution", 
            "Do you want high-resolution video? Low resolution renders much faster.", 
            QMessageBox::Yes | QMessageBox::No, 
            QMessageBox::No
        );
        setRez = (response == QMessageBox::Yes) ? "1920x540" : "480x270";
        qDebug() << "Will vstack each video with resolution:" << setRez;

        // Show the preview dialog
        PreviewDialog dialog(this);
        dialog.setAudioFile(webcamRecorded);
        if (dialog.exec() == QDialog::Accepted)
        {
            double vocalVolume = dialog.getVolume();
            mixAndRender(webcamRecorded, currentVideoFile, outputFilePath, vocalVolume, setRez);

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

void MainWindow::mixAndRender(const QString &webcamFilePath, const QString &videoFilePath, const QString &outputFilePath, double vocalVolume, QString userRez) {
    
    videoWidget->hide();
    placeholderLabel->show();
    singButton->setEnabled(false); 
    chooseInputButton->setEnabled(true);

    int totalDuration = getMediaDuration(videoFilePath);  // Get the total duration
    progressBar = new QProgressBar(this);
    progressBar->setMinimumSize(640, 12);
    progressBar->setRange(0, 100);

    // Create QProcess instance
    QProcess *process = new QProcess(this);
    QStringList arguments;
    
    // Create QLabel for progress indication
    QLabel *progressLabel = new QLabel("Processing...", this);
    progressLabel->setAlignment(Qt::AlignCenter);
    progressLabel->setFont(QFont("Helvetica", 7));
    // Get the main layout and add the progress label
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    layout->insertWidget(0, progressLabel, 0, Qt::AlignCenter);
    layout->insertWidget(0, progressBar, 25, Qt::AlignCenter);

    QFileInfo outfileInfo(outputFilePath);
    QFileInfo videofileInfo(videoFilePath);

    arguments << "-y" // Overwrite output file if it exists
            << "-fflags" << "+genpts"
            << "-i" << webcamFilePath // recorded vocals
            << "-i" << videoFilePath // playback file
            << "-filter_complex"
            << QString("[0:a]afftdn=nf=-20:nr=10:nt=w,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200, \
                        lv2=http\\\\://gareus.org/oss/lv2/fat1:c=mode=Auto|channelf=Any|bias=1.0|filter=0.1|offset=0.1|bendrange=2|corr=1.0, \
                        aecho=0.7:0.7:84:0.21,treble=g=12,volume=%1,atrim=%2[vocals]; \
                        [1:a][vocals]amix=inputs=2:normalize=0;")
                        .arg(vocalVolume)
                        .arg(millisecondsToSecondsString(offset));
            
    // In case of video output
    if ( outputFilePath.endsWith(".mp4") || outputFilePath.endsWith(".avi") || outputFilePath.endsWith(".mkv") ) {
        arguments << "-filter_complex" << QString("[0:v]scale=%1[webcam];[1:v]scale=%1[video];[video][webcam]vstack;")
                                                .arg(userRez);
    }

    // Selecina codec segundo conteiner    
    if (outputFilePath.endsWith(".mp4")) {
            // Para MP4, codecs comuns são libx264 para vídeo e aac para áudio
                arguments << "-c:v" << "libx264" << "-c:a" << "aac";
            } else if (outputFilePath.endsWith(".mkv")) {
                // Para MKV, libx265 para vídeo e vorbis para áudio são comuns
                arguments << "-c:v" << "libx265" << "-c:a" << "vorbis";
            } else if (outputFilePath.endsWith(".avi")) {
                // AVI geralmente usa mpeg4 para vídeo e mp3 para áudio
                arguments << "-c:v" << "mpeg4" << "-c:a" << "libmp3lame";
            } else if (outputFilePath.endsWith(".mp3")) {
                // Para MP3, só precisa de áudio
                arguments << "-vn" << "-c:a" << "libmp3lame";
            } else if (outputFilePath.endsWith(".flac")) {
                // Para FLAC, só precisa de áudio
                arguments << "-vn" << "-c:a" << "flac";
            } else if (outputFilePath.endsWith(".wav")) {
                // Para WAV, só precisa de áudio com PCM
                arguments << "-vn" << "-c:a" << "pcm_s16le";
        }
        arguments << "-ac" << "2" // force stereo
        << "-dither_method" << "none" // dithering
                << outputFilePath;
    
    // Connect signals to display output and errors
    connect(process, &QProcess::readyReadStandardOutput, [process, progressLabel, this]() {
        QByteArray outputData = process->readAllStandardOutput();
        QString output = QString::fromUtf8(outputData).trimmed();  // Convert to QString
        if (!output.isEmpty()) {
            qDebug() << "FFmpeg (stdout):" << output;
            progressLabel->setText("(FFmpeg is active)");
            this->logTextEdit->append("FFmpeg: " + output);
        }
    });
    connect(process, &QProcess::readyReadStandardError, [process, progressLabel, totalDuration, this]() {
        QByteArray errorData = process->readAllStandardError();
        QString output = QString::fromUtf8(errorData).trimmed();  // Convert to QString
        
        if (!output.isEmpty()) {
            qDebug() << "FFmpeg (stderr):" << output;
            progressLabel->setText("(FFmpeg running)");
            this->logTextEdit->append("FFmpeg: " + output);
            
            // Update progress based on FFmpeg output
            updateProgress(output, this->progressBar, totalDuration);
        }
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [process, progressLabel](int exitCode, QProcess::ExitStatus exitStatus) {
        progressLabel->hide();
        process->deleteLater(); // Clean up the process object
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, process, outputFilePath, progressLabel](int exitCode, QProcess::ExitStatus exitStatus) {
        qDebug() << "FFmpeg finished with exit code" << exitCode << "and status" << (exitStatus == QProcess::NormalExit ? "NormalExit" : "CrashExit");
        
        if ( exitStatus == QProcess::CrashExit) {
            this->logTextEdit->append("FFmpeg crashed!!");
            chooseVideoButton->setEnabled(true);
            fetchButton->setEnabled(true);
            renderAgainButton->setVisible(true);
            progressLabel->hide();
            process->deleteLater(); // Clean up the process object
            delete this->progressBar;
            return;
        }
        else
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
        playbackTimer->start(1000); // Update every second
        addProgressBarToScene(scene, getMediaDuration(outputFilePath));
        
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

QString MainWindow::millisecondsToSecondsString(qint64 milliseconds) {
    double seconds = milliseconds / 1000.0;
    return QString::number(seconds, 'f', 3); 
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
            QMessageBox::warning(this, "Sorry! Strange Error", "Total duration is not valid.");
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
        QMessageBox::warning(this, "Sorry! Strange Error", "Failed to get duration or duration is invalid. Aborting.");
        MainWindow::close();
        return 0;
    }

    int durationInSeconds = static_cast<int>(duration);
    qDebug() << "Media duration:" << durationInSeconds << "seconds";
    return durationInSeconds;
}

void MainWindow::updatePlaybackDuration() {
    if (player) {
        qint64 currentPosition = player->position();
        qint64 totalDuration = player->duration(); 

        QString currentTime = QDateTime::fromMSecsSinceEpoch(currentPosition).toUTC().toString("hh:mm:ss");
        QString totalTime = QDateTime::fromMSecsSinceEpoch(totalDuration).toUTC().toString("hh:mm:ss");

        QString durationText = QString("%1 / %2").arg(currentTime).arg(totalTime);
        durationTextItem->setPlainText(durationText); 
    }
}

void MainWindow::addProgressBarToScene(QGraphicsScene *scene, qint64 duration) {
     
     if (progressSong) {
        scene->removeItem(progressSong);
        delete progressSong;
        progressSong = nullptr;
    }
    // Barra de progresso
    progressSong = new QGraphicsRectItem(0, 0, 640, 12);
    progressSong->setBrush(QBrush(windowTextColor));
    scene->addItem(progressSong);
    progressSong->setPos(0, 0); // below cronômetro

    connect(player.data(), &QMediaPlayer::positionChanged, this, [=](qint64 currentPosition) {
        qreal progress = qreal(currentPosition) / ( duration * 1000 );
        progressSong->setRect(0, 0, 640 * progress, 12);
    });
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
            playbackTimer->start(1000); // Update every second
            addProgressBarToScene(scene, getMediaDuration(currentVideoFile));
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
    delete progressSong;
    recordingCheckTimer.reset();
}

// Function to disconnect all signals
void MainWindow::disconnectAllSignals() {

    // Disconnect signals from mediaRecorder
    if (mediaRecorder) {
        disconnect(mediaRecorder.get(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
        disconnect(mediaRecorder.get(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);
    }

    if (player) {
        disconnect(player.get(), &QMediaPlayer::mediaStatusChanged, this, &MainWindow::onPlayerMediaStatusChanged);
    }

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