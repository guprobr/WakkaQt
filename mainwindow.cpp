#include "mainwindow.h"
#include "sndwidget.h"
#include "previewdialog.h"

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
#include <QCheckBox>

#include <QMediaDevices>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QMediaCaptureSession>

#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioInput>

#include <QVideoWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsVideoItem>
#include <QGraphicsSceneMouseEvent>

#include <QSysInfo> // For platform detection
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , isRecording(false)
{
    QString Wakka_welcome = "Welcome to WakkaQt " + Wakka_versione;

    // acquire app palette
    QPalette palette = this->palette();
    highlightColor = palette.color(QPalette::Highlight);

    // Create video widget
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(320, 240);
    videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget->hide();
    
    // Create a QLabel to display the placeholder image
    placeholderLabel = new QLabel(this);
    placeholderLabel->setMinimumSize(320, 240);
    QPixmap placeholderPixmap(":/images/logo.jpg");
    if (placeholderPixmap.isNull()) {
        qWarning() << "Failed to load placeholder image!";
    } else {
        placeholderLabel->setPixmap(placeholderPixmap.scaled(768, 768, Qt::IgnoreAspectRatio));
    }
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    placeholderLabel->setScaledContents(true);

    // Create the scene and view
    scene = new QGraphicsScene(this);
    previewView = new QGraphicsView(scene, this);
    previewView->setMinimumSize(640, 200);
    previewView->setMaximumSize(1920, 200);
    previewView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewView->setAlignment(Qt::AlignCenter);

    // Get the size of the view (the window size when it is first created)
    qreal viewWidth = previewView->width();
    qreal viewHeight = previewView->height();

    // Create the webcam video previewItem and set it in the center
    previewItem = new QGraphicsVideoItem;
    previewItem->setSize(QSizeF(160, 120)); // size for the webcam video preview
    
    wakkaLogoItem = new QGraphicsPixmapItem(placeholderPixmap.scaled(160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    qreal previewX = (viewWidth - previewItem->boundingRect().width()) / 2;
    qreal previewY = (viewHeight - previewItem->boundingRect().height()) / 2; 
    previewItem->setPos(previewX, previewY);
    wakkaLogoItem->setPos(previewX, previewY);
    
    scene->addItem(previewItem);
    scene->addItem(wakkaLogoItem);
    previewItem->hide();
    wakkaLogoItem->show();
    
    // Create durationTextItem
    durationTextItem = new QGraphicsTextItem;
    durationTextItem->setDefaultTextColor(palette.color(QPalette::Text));
    durationTextItem->setFont(QFont("Verdana", 10));
    qreal textWidth = viewWidth * 0.85;
    durationTextItem->setTextWidth(textWidth);
    qreal textX = (viewWidth - textWidth) / 2;
    qreal textY = viewHeight - durationTextItem->boundingRect().height();
    durationTextItem->setPos(textX, textY);
    durationTextItem->setPlainText(Wakka_welcome);
    scene->addItem(durationTextItem);
    scene->setSceneRect(0, 0, viewWidth, viewHeight);

    // Create buttons
    QPushButton *exitButton = new QPushButton("Exit", this);
    chooseVideoButton = new QPushButton("Load playback from disk", this);
    singButton = new QPushButton("♪ SING ♪", this);
    chooseInputButton = new QPushButton("Choose Input Device", this);
    renderAgainButton = new QPushButton("RENDER AGAIN", this);

    // Recording indicator
    recordingIndicator = new QLabel("⦿ rec", this);
    recordingIndicator->setStyleSheet("color: red;");
    recordingIndicator->setFixedSize(64, 16);
    QHBoxLayout *indicatorLayout = new QHBoxLayout();
    indicatorLayout->addStretch();
    indicatorLayout->addWidget(recordingIndicator, 0, Qt::AlignCenter);
    indicatorLayout->addStretch();

    // Instantiate SndWidget
    soundLevelWidget = new SndWidget(this);
    soundLevelWidget->setMinimumSize(200, 32);
    soundLevelWidget->setMaximumSize(1920, 32);

    // Selected device
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

    // Log text
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->append(Wakka_welcome);
    logTextEdit->setFixedHeight(100);
    logTextEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Move cursor to the end of the text when text is appended
    connect(logTextEdit, &QTextEdit::textChanged, this, [=]() {
        logTextEdit->moveCursor(QTextCursor::End);
        logTextEdit->ensureCursorVisible();
    });

    // custom cfgs
    previewCheckbox = new QCheckBox("Enable Webcam Preview");
    previewCheckbox->setFont(QFont("Arial", 8));
    previewCheckbox->setToolTip("Check before recording to enable camera preview");
    indicatorLayout->addWidget(previewCheckbox);

    // Layout
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

    // Widget visibility
    soundLevelWidget->setVisible(true);
    recordingIndicator->hide();
    singButton->setEnabled(false);
    renderAgainButton->setVisible(false);
    exitButton->setVisible(false);
    deviceLabel->setVisible(false);

    // Connections
    connect(exitButton, &QPushButton::clicked, this, &QMainWindow::close);
    connect(chooseVideoButton, &QPushButton::clicked, this, &MainWindow::chooseVideo);
    connect(singButton, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(fetchButton, &QPushButton::clicked, this, &MainWindow::fetchVideo);
    connect(chooseInputButton, &QPushButton::clicked, this, &MainWindow::chooseInputDevice);
    connect(renderAgainButton, &QPushButton::clicked, this, &MainWindow::renderAgain);
    connect(previewCheckbox, &QCheckBox::toggled, this, &MainWindow::onPreviewCheckboxToggled);

    playbackTimer = new QTimer(this);
    connect(playbackTimer, &QTimer::timeout, this, &MainWindow::updatePlaybackDuration);

    resetAudioComponents(true);
}


void MainWindow::resetAudioComponents(bool isStarting) {
    qDebug() << "Resetting audio components";

    if (!isStarting)
        disconnectAllSignals(); // Ensure all signals are disconnected

    // Reset multimedia components with null checks to avoid double free
    if (player) {
        player->stop();
        player->setVideoOutput(nullptr);  // Clear video output before resetting
        player->setAudioOutput(nullptr);  // Clear audio output before resetting
        player->setSource(QUrl());        // Explicitly clear the media source
        player.reset();                   // Reset the unique pointer
    }
    
    if (audioOutput) {
        audioOutput->setMuted(true);
        audioOutput.reset();
    }

    if (mediaRecorder) {
        if (mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {
            mediaRecorder->stop();
        }
        mediaRecorder.reset();
    }

    if (mediaCaptureSession) {
        mediaCaptureSession.reset();
    }

    if (audioInput) {
        audioInput->setMuted(true); 
        audioInput.reset();
    }

    if (format) {
        format.reset();
    }

    if (camera) {
        if (camera->isActive()) {
            camera->stop(); 
        }
        camera.reset();
    }

    // Reinitialize components
    configureMediaComponents();
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

    audioInput->setDevice(selectedDevice);
    audioInput->setVolume(1.0f);
    updateDeviceLabel(selectedDevice);
    soundLevelWidget->setInputDevice(selectedDevice);
    
    format->setFileFormat(QMediaFormat::MPEG4);
    format->setVideoCodec(QMediaFormat::VideoCodec::H264);
    format->setAudioCodec(QMediaFormat::AudioCodec::Wave);

    connect(mediaRecorder.data(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    connect(mediaRecorder.data(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);
    connect(player.data(), &QMediaPlayer::mediaStatusChanged, this, &MainWindow::onPlayerMediaStatusChanged);

    mediaRecorder->setMediaFormat(*format);
    mediaRecorder->setOutputLocation(QUrl::fromLocalFile(webcamRecorded));
    mediaRecorder->setQuality(QMediaRecorder::VeryHighQuality);

    previewCheckbox->setEnabled(true);

    qDebug() << "Reconfigured media components";

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
    currentVideoFile = QFileDialog::getOpenFileName(this, "Open Playback File", QString(), "Video or Audio (*.mp4 *.mkv *.webm *.avi *.mov *.mp3 *.wav *.flac)");
    if (!currentVideoFile.isEmpty()) {

        singButton->setEnabled(true);
        renderAgainButton->setVisible(false);
        placeholderLabel->hide();
        videoWidget->show();

        resetAudioComponents(false);
        if ( player )
            player->setSource(QUrl::fromLocalFile(currentVideoFile)); 
        currentVideoName = QFileInfo(currentVideoFile).baseName();        
        addProgressBarToScene(scene, getMediaDuration(currentVideoFile));
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
        
        isRecording = true;
        
        if (mediaRecorder && camera) {

            qWarning() << "Configuring mediaCaptureSession..";
            mediaCaptureSession->setCamera(camera.data());
            mediaCaptureSession->setAudioInput(audioInput.data());
            mediaCaptureSession->setRecorder(mediaRecorder.data());
            camera->start();
            recordingEventTime = QDateTime::currentMSecsSinceEpoch(); // MARK RECORDING TIMESTAMP
            mediaRecorder->record();
                
        }

    } catch (const std::exception &e) {
        logTextEdit->append("Error during startRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }
}

void MainWindow::onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::BufferedMedia ) 
        playbackTimer->start(1000); // the playback cronometer

    if (status == QMediaPlayer::LoadedMedia )
        player->play(); // play when media is loaded!

}

void MainWindow::onRecorderStateChanged(QMediaRecorder::RecorderState state) {

    // when "Recording Starts"
    if (mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {

        recordingIndicator->show();
        singButton->setText("Finish!");
        singButton->setEnabled(true); // Click to finish recording

        // turn on the webcam preview?
        if (previewCheckbox->isChecked()) {
            mediaCaptureSession->setVideoOutput(previewItem);
            wakkaLogoItem->hide();
            previewItem->show();
            qDebug() << "Camera preview enabled.";
        }

        previewCheckbox->setEnabled(false);

        if ( player )
            player->setSource(QUrl::fromLocalFile(currentVideoFile));
        addProgressBarToScene(scene, getMediaDuration(currentVideoFile));        
        progressSongFull->setToolTip("Cannot seek while recording!");

        connect(mediaRecorder.data(), &QMediaRecorder::durationChanged, this, [=](qint64 currentDuration) {
            
            if ( player && player->position() > 0 ) 
            { 
                playbackEventTime = QDateTime::currentMSecsSinceEpoch(); // MARK PLAYBACK TIMESTAMP 

                qDebug() << "partial mediaRecorder Duration:" << mediaRecorder->duration();
                qDebug() << "mediaPlayer position:" << player->position();

                offset = (playbackEventTime - recordingEventTime);
                
                qDebug() << "Offset between playback start and recording start: " << offset << " ms";
                logTextEdit->append(QString("Offset between playback start and recording start: %1 ms").arg(offset));

                disconnect(mediaRecorder.data(), &QMediaRecorder::durationChanged, this, nullptr); 
            }

        });

    }

    // when "Finished recording"
    if (state == QMediaRecorder::StoppedState) {
        qDebug() << "Recording stopped.";
        
        if ( player )
            player->stop();
        playbackTimer->stop();

        videoWidget->hide();
        placeholderLabel->show();

        QFile file(webcamRecorded);
        if (file.size() > 0) {
            qDebug() << "Recording saved successfully";
            renderAgain();
        } else {
        // damn it we tought we were recording but we did not record anything!
            qDebug() << "*FAILURE* File size is zero.";
            logTextEdit->append("Recording ERROR. File size is zero.");

            chooseVideoButton->setEnabled(true);
            fetchButton->setEnabled(true);
            chooseInputButton->setEnabled(true);
            singButton->setEnabled(false);
            
            resetAudioComponents(false);
            QMessageBox::warning(this, "SORRY: mediaRecorder ERROR", "File size is zero.");
        }
    }
}

// recording FINISH button
void MainWindow::stopRecording() {
    try {
        if (!isRecording) {
            qWarning() << "Not recording.";
            logTextEdit->append("Tried to stop Recording, but we are not recording. ERROR.");
            QMessageBox::warning(this, "ERROR.", "Tried to stop Recording, but we are not recording. ERROR.");
            return;
        }

        if ( camera )
            camera->stop();
        if ( mediaRecorder )
            mediaRecorder->stop();

        recordingIndicator->hide();
        previewItem->hide();
        wakkaLogoItem->show();
        
        isRecording = false;
        singButton->setText("♪ SING ♪");
        progressSongFull->setToolTip("Click to seek");

    } catch (const std::exception &e) {
        logTextEdit->append("Error during stopRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }
}

void MainWindow::handleRecorderError(QMediaRecorder::Error error) {
    qWarning() << "Recorder error:" << error << mediaRecorder->errorString();
    logTextEdit->append("Recording Error: " + mediaRecorder->errorString());

    try {
        if (mediaRecorder ) {
            qDebug() << "Stopping media due to error...";
            if ( player )
                player->stop();
            playbackTimer->stop();
            mediaRecorder->stop();
        }
    } catch (const std::exception &e) {
        logTextEdit->append("Error during stopRecording (while handling error): " + QString::fromStdString(e.what()));
    }

    handleRecordingError();
    QMessageBox::warning(this, "Recording Error", "An error occurred while recording: " + mediaRecorder->errorString());
}

void MainWindow::handleRecordingError() {
    logTextEdit->append("Attempting to recover from recording error...");

    isRecording = false;

    qDebug() << "Cleaning up..";
    if ( camera )
        camera->stop();

    recordingIndicator->hide();
    previewItem->hide();
    wakkaLogoItem->show();
    videoWidget->hide();
    placeholderLabel->show();

    singButton->setEnabled(false);
    singButton->setText("SING");
    chooseVideoButton->setEnabled(true);
    fetchButton->setEnabled(true);
    chooseInputButton->setEnabled(true);

    progressSongFull->setToolTip("Click to seek");

    resetAudioComponents(false);

}

// render //
void MainWindow::renderAgain()
{
    if ( player )
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
            chooseVideoButton->setEnabled(true);
            fetchButton->setEnabled(true);
            chooseInputButton->setEnabled(true);
            singButton->setEnabled(false);
            QMessageBox::warning(this, "Rendering Aborted!", "adjustment cancelled..");             
        }
    } else {
        chooseVideoButton->setEnabled(true);
        fetchButton->setEnabled(true);
        chooseInputButton->setEnabled(true);
        singButton->setEnabled(false);
        QMessageBox::warning(this, "Rendering aborted!", "Rendering was cancelled..");
    }
}

void MainWindow::mixAndRender(const QString &webcamFilePath, const QString &videoFilePath, const QString &outputFilePath, double vocalVolume, QString userRez) {
    
    videoWidget->hide();
    placeholderLabel->show();
    singButton->setEnabled(false); 
    chooseInputButton->setEnabled(true);

    int totalDuration = getMediaDuration(videoFilePath);  // Get the total duration
    progressBar = new QProgressBar(this);
    progressBar->setMinimumSize(640, 25);
    progressBar->setRange(0, 100);

    // Create QProcess instance
    QProcess *process = new QProcess(this);
    QStringList arguments;
    
    // Create QLabel for progress indication
    QLabel *progressLabel = new QLabel("Processing...", this);
    progressLabel->setAlignment(Qt::AlignCenter);
    progressLabel->setFont(QFont("Arial", 8));
    // Get the main layout and add the progress label
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    layout->insertWidget(0, progressLabel, 0, Qt::AlignCenter);
    layout->insertWidget(0, progressBar, 25, Qt::AlignCenter);

    QFileInfo outfileInfo(outputFilePath);
    QFileInfo videofileInfo(videoFilePath);
    QString videorama = "";

    if ( camera->isAvailable() && (outputFilePath.endsWith(".mp4") || outputFilePath.endsWith(".avi") || outputFilePath.endsWith(".mkv")) 
    && !( (videoFilePath.endsWith("mp3")) || (videoFilePath.endsWith("wav")) || (videoFilePath.endsWith("flac"))) ) {
        // Combine both recorded and playback videos
        videorama = QString("[0:v]trim=%1,scale=%2[webcam]; \
                            [1:v]scale=%2[video]; \
                            [video][webcam]vstack;")
                            .arg(millisecondsToSecondsString(offset))
                            .arg(userRez);
    }

    // we Configure autotalent plugin here:
    QString talent = "lv2=http\\\\://gareus.org/oss/lv2/fat1,";

    arguments << "-y" // Overwrite output file if it exists
          << "-fflags" << "+genpts"
          << "-i" << webcamFilePath // recorded vocals
          << "-i" << videoFilePath // playback file
          << "-filter_complex" // masterization and vocal enhancement of recorded audio
          << QString("[0:a]aformat=channel_layouts=stereo,atrim=%1,afftdn=nf=-20:nr=10:nt=w,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200,%2 \
                      aecho=0.7:0.7:84:0.21,treble=g=12,volume=%3[vocals]; \
                      [1:a][vocals]amix=inputs=2:normalize=0[wakkamix];%4" 
                      ).arg(millisecondsToSecondsString(offset))
                      .arg(talent)
                      .arg(vocalVolume)
                      .arg(videorama);

    // Map audio output
    arguments << "-ac" << "2" // Force stereo
            << "-dither_method" << "none" // dithering off
            << "-map" << "[wakkamix]"      // ensure audio goes on the pack
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

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, process, outputFilePath, progressLabel](int exitCode, QProcess::ExitStatus exitStatus) {
        qDebug() << "FFmpeg finished with exit code" << exitCode << "and status" << (exitStatus == QProcess::NormalExit ? "NormalExit" : "CrashExit");
        
        progressLabel->hide();
        delete this->progressBar;
        process->deleteLater(); // Clean up the process object

        if ( exitStatus == QProcess::CrashExit) {
            this->logTextEdit->append("FFmpeg crashed!!");
            chooseVideoButton->setEnabled(true);
            fetchButton->setEnabled(true);
            renderAgainButton->setVisible(true);
            QMessageBox::warning(this, "FFMpeg crashed :(", "Aborting.. Verify if FFMpeg is correctly installed and in the PATH to be executed. Verify logs for FFMpeg error. This program requires a LV2 plugin callex X42 by Gareus.");
            return;
        }
        else {
            QMessageBox::warning(this, "Rendering Done!", "Prepare to preview performance. You can press RenderAgain button to adjust volume again or select a different filename, file type or resolution.");
            this->logTextEdit->append("FFmpeg finished.");
        }

        QFile file(outputFilePath);
        if (!file.exists()) {
            qDebug() << "Output path does not exist: " << outputFilePath;
            QMessageBox::warning(this, "FFMpeg ?", "Aborted playback. Strange error: output file does not exist.");
            return;
        }

        // And now, for something completely different: play the rendered performance! :D

        chooseVideoButton->setEnabled(true);
        fetchButton->setEnabled(true);
        renderAgainButton->setVisible(true);
        placeholderLabel->hide();
        videoWidget->show();

        qDebug() << "Setting media source to" << outputFilePath;
        currentVideoFile = outputFilePath;
        if ( player )
            player->setSource(QUrl::fromLocalFile(currentVideoFile));
        addProgressBarToScene(scene, getMediaDuration(currentVideoFile));

        qDebug() << "trimmed rec offset: " << offset << " ms";
        logTextEdit->append(QString("trimmed recording Offset: %1 ms").arg(offset));
        this->logTextEdit->append("Playing mix!");

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

void MainWindow::updatePlaybackDuration() {
    if (player) {

        qint64 currentPosition = player->position();
        qint64 totalDuration = player->duration(); 

        QString currentTime = QDateTime::fromMSecsSinceEpoch(currentPosition).toUTC().toString("hh:mm:ss");
        QString totalTime = QDateTime::fromMSecsSinceEpoch(totalDuration).toUTC().toString("hh:mm:ss");

        QString durationText = QString("%1 / %2 - %3")
                                .arg(currentTime)
                                .arg(totalTime)
                                .arg(currentVideoName);
        
        durationTextItem->setPlainText(durationText);
    }
}

void MainWindow::addProgressBarToScene(QGraphicsScene *scene, qint64 duration) {
     
    disconnect(player.data(), &QMediaPlayer::positionChanged, this, nullptr);

    if (progressSongFull) {
        scene->removeItem(progressSongFull);
        delete progressSongFull;
        progressSongFull = nullptr;
    }
    // Song progress Bar
    progressSongFull = new QGraphicsRectItem(0, 6, 640, 20);
    progressSongFull->setToolTip("Click to seek");
    scene->addItem(progressSongFull);
    progressSongFull->setPos(0, 6);

    if (progressSong) {
        scene->removeItem(progressSong);
        delete progressSong;
        progressSong = nullptr;
    }
    // Song progress Bar
    progressSong = new QGraphicsRectItem(0, 6, 0, 20);
    progressSong->setBrush(QBrush(highlightColor));
    scene->addItem(progressSong);
    progressSong->setPos(0, 6);

    // Update progress bar as media plays
    connect(player.data(), &QMediaPlayer::positionChanged, this, [=](qint64 currentPosition) {
        qreal progress = qreal(currentPosition) / (duration * 1000);
        progressSong->setRect(0, 6, 640 * progress, 20);
    });

    scene->removeEventFilter(this); // remove prior eventfilter
    if ( !isRecording ) // do not seek when recording!
        scene->installEventFilter(this); // Handle mouse press events, making the progress bar seekable
}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    
    // Check if the event is a mouse press event in a QGraphicsScene
    if (event->type() == QEvent::GraphicsSceneMousePress) {
        // Cast the event to QGraphicsSceneMouseEvent
        QGraphicsSceneMouseEvent *mouseEvent = dynamic_cast<QGraphicsSceneMouseEvent *>(event);
        if (mouseEvent) {
            QPointF clickPos = mouseEvent->scenePos();  // Get the mouse click position in scene coordinates

            // Get progress bar position and dimensions
            qreal progressBarX = progressSong->pos().x();
            qreal progressBarY = progressSong->pos().y();
            qreal progressBarWidth = 640;
            qreal progressBarHeight = progressSong->rect().height();

        // SEEKABLE SONG PROGRESS BAR
            // Check if the click was within the progress bar's area (both x and y boundaries)
            if (clickPos.y() >= progressBarY && clickPos.y() <= progressBarY + progressBarHeight &&
                clickPos.x() >= progressBarX && clickPos.x() <= progressBarX + progressBarWidth) {
                
                // Calculate the relative progress based on the click position
                qreal clickPosition = clickPos.x() - progressBarX;  // Offset click position by the progress bar's X position
                qreal progress = clickPosition / progressBarWidth;

                // Ensure the progress is within valid range [0, 1]
                if (progress < 0) progress = 0;
                if (progress > 1) progress = 1;

                // Set the new position based on the click
                if ( player ) {
                    qint64 newPosition = static_cast<qint64>(progress * player->duration());

                    player->pause();                    // pause for smooth seeking
                    player->setAudioOutput(nullptr);    // avoid breaking sound when seeking (Qt6.4 bug?)
                    player->setPosition(newPosition);  // Seek the media player to the clicked position
                    player->setAudioOutput(audioOutput.data());
                    player->play();

                    return true;  // Event handled

                } else return false;
            }
        }
    }

    // Pass the event on to the parent class if it was not handled
    return QMainWindow::eventFilter(object, event);
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

            placeholderLabel->hide();
            videoWidget->show();
            
            currentVideoFile = videoFilePath;  // Store the video playback file path
            if ( player )
                player->setSource(QUrl::fromLocalFile(currentVideoFile)); 
            currentVideoName = QFileInfo(currentVideoFile).baseName();
            addProgressBarToScene(scene, getMediaDuration(currentVideoFile));

            downloadStatusLabel->setText("Download YouTube URL");
            singButton->setEnabled(true); 
            logTextEdit->append("Previewing playback. Press SING to start recording.");
            
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

void MainWindow::onPreviewCheckboxToggled(bool enable) {
    if (enable) {
        qDebug() << "Camera preview will be enabled next recording.";
        logTextEdit->append("Camera preview will be enabled next recording.");
    } else {
        // Hide the camera preview
        //mediaCaptureSession->setVideoOutput(nullptr);
        previewItem->hide(); 
        wakkaLogoItem->show(); 
        qDebug() << "Camera preview hidden.";
    }
}

MainWindow::~MainWindow() {
    if (isRecording) {
        stopRecording();
    }

    disconnectAllSignals();

    if ( player )
        player.reset();
    if ( audioOutput )
        audioOutput.reset();
    if ( mediaRecorder )
        mediaRecorder.reset();
    if ( mediaCaptureSession )
        mediaCaptureSession.reset();
    if ( audioInput )
        audioInput.reset();
    if ( format )
        format.reset();
    if ( videoWidget )
        delete videoWidget;
    if ( soundLevelWidget )
        delete soundLevelWidget;
    if ( progressSong )
        delete progressSong;
}

// Function to disconnect all signals
void MainWindow::disconnectAllSignals() {

    // Disconnect media signals
    if (mediaRecorder) {
        disconnect(mediaRecorder.data(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
        disconnect(mediaRecorder.data(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);
        disconnect(mediaRecorder.data(), &QMediaRecorder::durationChanged, this, nullptr); 
    }

    if (player) {
        disconnect(player.data(), &QMediaPlayer::mediaStatusChanged, this, &MainWindow::onPlayerMediaStatusChanged);
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