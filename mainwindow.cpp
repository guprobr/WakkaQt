#include "mainwindow.h"
#include "sndwidget.h"
#include "previewdialog.h"

#include <QCoreApplication>
#include <QMenu>
#include <QMenuBar>
#include <QIcon>
#include <QFileInfo>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
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
#include <QVideoFrame>
#include <QVideoSink>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsSceneMouseEvent>

#include <QSysInfo> // For platform detection
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , isRecording(false)
    , webcamDialog(nullptr)
{
    QString Wakka_welcome = "Welcome to WakkaQt " + Wakka_versione;

    // acquire app palette
    QPalette palette = this->palette();
    highlightColor = palette.color(QPalette::Highlight);

    QMenuBar *menuBar = new QMenuBar(this);
    QMenu *helpMenu = new QMenu("About", this);
    QMenu *fileMenu = new QMenu("File", this);
    QAction *aboutQtAction = new QAction("About Qt", this);
    QAction *aboutWakkaQtAction = new QAction("About WakkaQt", this);
    loadPlaybackAction = new QAction("Load playback", this);
    chooseInputAction = new QAction("Configure Audio Input", this);
    QAction *exitAction = new QAction("Exit", this);
    menuBar->setFont(QFont("Arial", 8));
    
    helpMenu->addAction(aboutQtAction);
    helpMenu->addAction(aboutWakkaQtAction);
    fileMenu->addAction(loadPlaybackAction);
    fileMenu->addAction(chooseInputAction);
    fileMenu->addAction(exitAction);
    
    menuBar->addMenu(fileMenu);
    menuBar->addMenu(helpMenu);
    setMenuBar(menuBar);

    connect(aboutQtAction, &QAction::triggered, this, []() {
        QMessageBox::aboutQt(nullptr, "About Qt");
    });

    connect(aboutWakkaQtAction, &QAction::triggered, this, [Wakka_welcome]() {
        
        QMessageBox aboutBox;
        aboutBox.setWindowTitle("About WakkaQt");

        QPixmap logoPixmap(":/images/logo.jpg");
        aboutBox.setIconPixmap(logoPixmap.scaled(100, 100, Qt::KeepAspectRatio));

        QString aboutText = R"(
            <p>WakkaQt is a karaoke application built with C++ and Qt6, designed to record vocals over a video/audio track and mix them into a rendered file.</p>
            <p>This app features webcam recording, YouTube video downloading, real-time sound visualization, and post-recording video rendering with FFmpeg. It automatically does some mastering on the vocal tracks. It also uses an AutoTuner LV2 plugin for smoothing the results, X42 by Robin Gareus, with slight pitch shift/correction and formant preservation.</p>
            <p>©2024 The author is Gustavo L Conte. Visit the author's website: <a href="https://gu.pro.br">https://gu.pro.br</a></p>
            <p>WakkaQt is open source and the code is available <a href="https://github.com/guprobr/WakkaQt">on GitHub</a>.</p>
            <p>%1</p>
        )";

        aboutBox.setTextFormat(Qt::RichText);  
        aboutBox.setText(aboutText.arg(Wakka_welcome));  // Insert Wakka_welcome message
        aboutBox.setFont(QFont("Arial", 10));

        // Enable clickable links
        QLabel *label = aboutBox.findChild<QLabel *>("qt_msgbox_label");
        if (label) {
            label->setOpenExternalLinks(true);
        }
        // Display!
        aboutBox.exec();
    });

    // Create video widget
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(640, 240);
    videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget->hide();
    
    // Create a QLabel to display the placeholder image
    placeholderLabel = new QLabel(this);
    placeholderLabel->setMinimumSize(640, 240);
    QPixmap placeholderPixmap(":/images/logo.jpg");
    if (placeholderPixmap.isNull()) {
        qWarning() << "Failed to load placeholder image!";
    } else {
        placeholderLabel->setPixmap(placeholderPixmap.scaled(640, 640, Qt::KeepAspectRatio));
    }
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    placeholderLabel->setScaledContents(true);

    // Create the main VideoDisplayWidget
    webcamPreviewWidget = new VideoDisplayWidget(this);
    webcamPreviewWidget->setFixedSize(640, 160);
    webcamPreviewWidget->setToolTip("Click to open large preview");
    QHBoxLayout *webcamPreviewLayout = new QHBoxLayout();
    webcamPreviewLayout->addStretch();
    webcamPreviewLayout->addWidget(webcamPreviewWidget, 0, Qt::AlignCenter);
    webcamPreviewLayout->addStretch();

    // Create the scene and view
    scene = new QGraphicsScene(this);
    previewView = new QGraphicsView(scene, this);
    previewView->setMinimumSize(640, 50);
    previewView->setMaximumSize(1920, 50);
    previewView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewView->setAlignment(Qt::AlignCenter);
    qreal viewWidth = previewView->width();
    qreal viewHeight = previewView->height();
    // Create playback chronometer text item
    durationTextItem = new QGraphicsTextItem;
    durationTextItem->setDefaultTextColor(palette.color(QPalette::Text));
    durationTextItem->setFont(QFont("Courier", 16, QFont::Bold));
    durationTextItem->setY(previewView->height() - (durationTextItem->boundingRect().height()) +5 );
    durationTextItem->setPlainText("00:00:00 / 00:00:00");
        
    scene->addItem(durationTextItem);
    scene->setSceneRect(0, 0, viewWidth, viewHeight);

    banner = new QLabel(Wakka_welcome, this);
    banner->setTextFormat(Qt::TextFormat::RichText);
    banner->setFont(QFont("Arial", 10));
    banner->setAlignment(Qt::AlignCenter);
    banner->setToolTip("Here be the song title, dargh!");
    setBanner(Wakka_welcome);

    // Create buttons
    QPushButton *exitButton = new QPushButton("Exit", this);
    chooseVideoButton = new QPushButton("Load playback from disk", this);
    singButton = new QPushButton("♪ SING ♪", this);
    singButton->setFont(QFont("Arial", 21));
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
    soundLevelWidget->setToolTip("Sound input visualization widget");

    // Device label
    deviceLabel = new QLabel("Selected Device: None", this);
    deviceLabel->setFont(QFont("Verdana", 8));
    deviceLabel->setToolTip("Changing the default source input in the system cfg will not reflect the information here");
    
    // YT downloader
    urlInput = new QLineEdit(this);
    urlInput->setPlaceholderText("https://www.youtube.com/?v=ABCCDFGETC");
    urlInput->setFont(QFont("Arial", 10));
    urlInput->setToolTip("Paste a URL here to fetch your karaoke playback video");
    fetchButton = new QPushButton("FETCH", this);
    fetchButton->setFont(QFont("Arial", 10));
    fetchButton->setToolTip("Click here to begin yt-dlp: video streaming service downloader");
    downloadStatusLabel = new QLabel("Download YouTube URL", this);
    downloadStatusLabel->setFont(QFont("Arial", 8));
    downloadStatusLabel->setToolTip("Several streaming services URL besides YouTube may work here");
    QHBoxLayout *fetchLayout = new QHBoxLayout;
    fetchLayout->addWidget(urlInput);
    fetchLayout->addWidget(fetchButton);
    fetchLayout->addWidget(downloadStatusLabel);

    // Log text
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->append(Wakka_welcome);
    logTextEdit->setFixedHeight(50);
    logTextEdit->setFont(QFont("Arial", 9));
    logTextEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Move cursor to the end of the text when text is appended
    connect(logTextEdit, &QTextEdit::textChanged, this, [=]() {
        logTextEdit->moveCursor(QTextCursor::End);
        logTextEdit->ensureCursorVisible();
    });

    // Instantiate Audio Visualizer
    vizWidget = new AudioVisualizerWidget(this);
    vizWidget->setMinimumSize(200, 45);
    vizWidget->setMaximumSize(1920, 45);
    vizWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vizWidget->setToolTip("Yelloopy© Audio Visualizer");

    // custom options
    previewCheckbox = new QCheckBox("Cam Preview");
    previewCheckbox->setFont(QFont("Arial", 8));
    previewCheckbox->setToolTip("Toggle camera preview");
    indicatorLayout->addWidget(previewCheckbox);


    // Layout
    QWidget *containerWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(containerWidget);
    layout->addLayout(indicatorLayout);
    layout->addLayout(webcamPreviewLayout);
    layout->addWidget(banner);
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
    layout->addWidget(vizWidget);

    setCentralWidget(containerWidget);

    // Widget visibility
    chooseVideoButton->setVisible(false);
    chooseInputButton->setVisible(false);
    soundLevelWidget->setVisible(true);
    recordingIndicator->hide();
    webcamPreviewWidget->hide();
    singButton->setEnabled(false);
    renderAgainButton->setVisible(false);
    exitButton->setVisible(false);
    deviceLabel->setVisible(true);
    vizWidget->show();

    // Connections
    connect(exitButton, &QPushButton::clicked, this, &QMainWindow::close);
    connect(chooseVideoButton, &QPushButton::clicked, this, &MainWindow::chooseVideo);
    connect(chooseInputButton, &QPushButton::clicked, this, &MainWindow::chooseInputDevice);
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
    connect(loadPlaybackAction, &QAction::triggered, this, &MainWindow::chooseVideo);
    connect(chooseInputAction, &QAction::triggered, this, &MainWindow::chooseInputDevice);
    connect(singButton, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(fetchButton, &QPushButton::clicked, this, &MainWindow::fetchVideo);
    connect(renderAgainButton, &QPushButton::clicked, this, &MainWindow::renderAgain);
    connect(previewCheckbox, &QCheckBox::toggled, this, &MainWindow::onPreviewCheckboxToggled);
    connect(webcamPreviewWidget, &VideoDisplayWidget::clicked, this, &MainWindow::addVideoDisplayWidgetInDialog);


    playbackTimer = new QTimer(this);
    connect(playbackTimer, &QTimer::timeout, this, &MainWindow::updatePlaybackDuration);

    scene->installEventFilter(this);

    chooseInputDevice();
    resetMediaComponents(true);

}


void MainWindow::resetMediaComponents(bool isStarting) {

    QCoreApplication::processEvents();

    qDebug() << "Resetting media components";

    if (!isStarting)
        disconnectAllSignals(); // Ensure all signals are disconnected

    playbackTimer->stop();
    // Reset multimedia components with null checks to avoid double free

    if ( vizPlayer ) {
        vizPlayer->stop();
        vizPlayer.reset();
    }

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

    if (audioRecorder) {
        audioRecorder->stopRecording();
        audioRecorder.reset();
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

    // PATHS to the tmp files for recording audio and video
    webcamRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.mp4");
    audioRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.wav");

    // Reconfigure ALL components //
    audioRecorder.reset(new AudioRecorder(selectedDevice, this));
    connect(audioRecorder.data(), &AudioRecorder::deviceLabelChanged, this, &MainWindow::updateDeviceLabel);
    audioRecorder->initialize();

    player.reset(new QMediaPlayer(this));
    audioOutput.reset(new QAudioOutput(this));
    videoSink.reset(new QVideoSink(this));
    format.reset(new QMediaFormat);
    camera.reset(new QCamera(this));
    mediaRecorder.reset(new QMediaRecorder(this));
    mediaCaptureSession.reset(new QMediaCaptureSession(this));

    // Setup Media player
    player->setVideoOutput(videoWidget);
    player->setAudioOutput(audioOutput.data());
    vizPlayer.reset(new AudioVizMediaPlayer(player.data(), vizWidget, this));
    
    // Setup SndWidget
    soundLevelWidget->setInputDevice(selectedDevice);
    
    format->setFileFormat(QMediaFormat::FileFormat::MPEG4);
    format->setVideoCodec(QMediaFormat::VideoCodec::H264);
    format->setAudioCodec(QMediaFormat::AudioCodec::AAC);

    // Setup Media recorder
    mediaRecorder->setMediaFormat(*format);
    mediaRecorder->setOutputLocation(QUrl::fromLocalFile(webcamRecorded));
    mediaRecorder->setQuality(QMediaRecorder::VeryHighQuality);
    
    qDebug() << "Configuring mediaCaptureSession..";
    mediaCaptureSession->setVideoOutput(videoSink.data());
    mediaCaptureSession->setCamera(camera.data());
    //mediaCaptureSession->setAudioInput(nullptr);
    mediaCaptureSession->setRecorder(mediaRecorder.data());

    connect(mediaRecorder.data(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    connect(mediaRecorder.data(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);
    connect(player.data(), &QMediaPlayer::mediaStatusChanged, this, &MainWindow::onPlayerMediaStatusChanged);
    connect(videoSink.data(), &QVideoSink::videoFrameChanged, this, &MainWindow::onVideoFrameReceived);

    qDebug() << "Reconfigured media components";

}

void MainWindow::chooseInputDevice() {

    selectedDevice = QMediaDevices::defaultAudioInput(); // default to the default!
    // Find all available audio input devices
    QList<QAudioDevice> audioInputs = QMediaDevices::audioInputs();
    
    // If no devices found, warn the user and return
    if (audioInputs.isEmpty()) {
        QMessageBox::warning(this, "No Input Device", "No audio input devices found.");
        return;
    }

    // Create a dialog to show the list of devices
    QDialog *deviceDialog = new QDialog();
    deviceDialog->setWindowTitle("Select Input Device");
    deviceDialog->setFixedSize(300, 200);

    // Create a layout for the dialog
    QVBoxLayout *layout = new QVBoxLayout(deviceDialog);

    // Create a list widget to display audio devices
    QListWidget *deviceList = new QListWidget(deviceDialog);
    for (const QAudioDevice &device : audioInputs) {
        // Create a new item for the list
        QListWidgetItem *item = new QListWidgetItem(device.description());
        // Store the unique ID as user data
        item->setData(Qt::UserRole, device.id());
        deviceList->addItem(item);
    }
    layout->addWidget(deviceList);

    // Create a button to confirm the selection
    QPushButton *selectButton = new QPushButton("Select", deviceDialog);
    layout->addWidget(selectButton);

    // Connect the select button to handle selection
    connect(selectButton, &QPushButton::clicked, this, [this, deviceList, deviceDialog]() {
        // Get the selected item
        QListWidgetItem *selectedItem = deviceList->currentItem();
        if (selectedItem) {
            // Find the corresponding device using its unique ID
            QString selectedDeviceId = selectedItem->data(Qt::UserRole).toString();
            QList<QAudioDevice> audioInputs = QMediaDevices::audioInputs();
            selectedDevice = QAudioDevice(); // Reset selected device

            for (const QAudioDevice &device : audioInputs) {
                if (device.id() == selectedDeviceId) {
                    selectedDevice = device;
                    break;
                }
            }

            // Close the dialog
            deviceDialog->accept();

            // Check if the selected device is valid
            if (selectedDevice.isNull()) {
                QMessageBox::warning(this, "Device Error", "The selected audio input device is invalid.");
            } else {
                
                // Set a supported audio format (adjust as needed)
                QAudioFormat format;
                format.setSampleRate(44100);
                format.setChannelCount(1);
                format.setSampleFormat(QAudioFormat::Int16);

                // Create an audio source to check if the device is working
                QScopedPointer<QAudioSource> audioSource(new QAudioSource(selectedDevice, format));

                if (!audioSource->start()) {
                    QMessageBox::warning(this, "Device Error", "Failed to start audio source: " + audioSource->error());
                    // Check specific error codes for more information
                    if (audioSource->error() == QAudio::IOError) {
                        QMessageBox::warning(this, "IO Error", "I/O error occurred while starting audio source.");
                    } else if (audioSource->error() != QAudio::NoError) {
                        QMessageBox::warning(this, "Error", "Error on input device.");
                    }
                }
                audioSource->stop();
            }
        } else {
            QMessageBox::warning(this, "No Selection", "Please select a device before proceeding.");
        }
    });

    // Execute the dialog and check if it was cancelled
    if (deviceDialog->exec() == QDialog::Rejected) {
        QMessageBox::information(this, "Cancelled", "Device selection was cancelled.");
    }

    audioRecorder.reset(new AudioRecorder(selectedDevice, this));
    connect(audioRecorder.data(), &AudioRecorder::deviceLabelChanged, this, &MainWindow::updateDeviceLabel);
    audioRecorder->initialize();
    soundLevelWidget->setInputDevice(selectedDevice);

    delete deviceDialog; // Clean up the dialog after it is closed
}

void MainWindow::updateDeviceLabel(const QString &deviceLabelText) {
    if (deviceLabel) {
        deviceLabel->setText(QString("Audio Input Device: %1").arg(deviceLabelText));
    }
}

void MainWindow::chooseVideo()
{
    //QString loadVideoFile = QFileDialog::getOpenFileName(this, "Open Playback File", QString(), "Video or Audio (*.mp4 *.mkv *.webm *.avi *.mov *.mp3 *.wav *.flac)");
    QFileDialog* fileDialog = new QFileDialog(this);
    //fileDialog->setAttribute(Qt::WA_DeleteOnClose);
    fileDialog->setWindowTitle("Open Playback File");
    fileDialog->setNameFilter("Video or Audio (*.mp4 *.mkv *.webm *.avi *.mov *.mp3 *.wav *.flac)");
    if (fileDialog->exec() == QDialog::Accepted) {
        QString loadVideoFile = fileDialog->selectedFiles().first();
        if (!loadVideoFile.isEmpty()) {

            resetMediaComponents(false);

            singButton->setEnabled(true);
            renderAgainButton->setVisible(false);
            placeholderLabel->hide();
            videoWidget->show();

            currentVideoFile = loadVideoFile;
            if ( player && vizPlayer ) {
                vizPlayer->stop();
                playbackTimer->stop();
                setBanner("DECODING... Please wait."); 
                
                vizPlayer->setMedia(currentVideoFile); 
                //player->setSource(QUrl::fromLocalFile(currentVideoFile));
                currentVideoName = QFileInfo(currentVideoFile).completeBaseName();        
                addProgressSong(scene, getMediaDuration(currentVideoFile));
                logTextEdit->append("Playback preview. Press SING to start recording.");
            }
        }
    }
    delete fileDialog;
}

void MainWindow::onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::BufferedMedia ) {
        startEventTime = QDateTime::currentMSecsSinceEpoch(); // MARK TIMESTAMP
    }

    if (status == QMediaPlayer::LoadedMedia ) {

        if ( mediaRecorder && audioRecorder && isRecording )
        {   
            camera->start();
            mediaRecorder->record();
            audioRecorder->startRecording(audioRecorded);

        }

        if ( player && vizPlayer ) {
            
            durationTextItem->setToolTip(currentVideoName);
            banner->setToolTip(currentVideoName);
            setBanner(currentVideoName);

            playbackTimer->start(1000); // the playback cronometer
            vizPlayer->play();
        }        
        
    }

}

// START RECORDING //
void MainWindow::startRecording() {
try {
        if (currentVideoFile.isEmpty()) {
            QMessageBox::warning(this, "No playback set", "No playback loaded! Maybe you tried to load a playback and cancelled the dialog? Please load a playback to sing.");
            singButton->setEnabled(false);
            chooseVideoButton->setEnabled(true);
            loadPlaybackAction->setEnabled(true);
            fetchButton->setEnabled(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            return;

        }

        if (isRecording) {
            qWarning() << "Stop recording.";
            logTextEdit->append("Stop recording...");
            stopRecording();
            return;
        }

        singButton->setEnabled(false);
        chooseVideoButton->setEnabled(false);
        loadPlaybackAction->setEnabled(false);
        fetchButton->setEnabled(false);
        chooseInputButton->setEnabled(false);
        chooseInputAction->setEnabled(false);

        chooseInputDevice();
        resetMediaComponents(false);
        
        isRecording = true;
        
        if (mediaRecorder && camera) {

            if ( player && vizPlayer ) {
                vizPlayer->stop();
                playbackTimer->stop();

                setBanner("DECODING... Please wait.");
                
                vizPlayer->setMedia(currentVideoFile);
                addProgressSong(scene, getMediaDuration(currentVideoFile));        
                progressSongFull->setToolTip("Will not seek while recording!");
            }            

        }

    } catch (const std::exception &e) {
        logTextEdit->append("Error during startRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }
}

void MainWindow::onRecorderStateChanged(QMediaRecorder::RecorderState state) {

    // when "Recording Starts"
    if (mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {

        recordingIndicator->show();
        singButton->setText("Finish!");
        singButton->setEnabled(true); // Click to finish recording

        connect(mediaRecorder.data(), &QMediaRecorder::durationChanged, this, [=](qint64 currentDuration) {
            
            if ( player && player->position() > 0 ) 
            {
                disconnect(mediaRecorder.data(), &QMediaRecorder::durationChanged, this, nullptr); 
                recordingEventTime = QDateTime::currentMSecsSinceEpoch(); // MARK TIMESTAMP 

                offset = (recordingEventTime - startEventTime);
                qWarning() << "Calculated Offset: " << offset << " ms";
                logTextEdit->append(QString("Calculated Offset: %1 ms").arg(offset));
            }

        });

    }

    // when "Finished recording"
    if (state == QMediaRecorder::StoppedState) {
        qWarning() << "Recording stopped.";
        
        if ( player && vizPlayer )
            vizPlayer->stop();
        playbackTimer->stop();

        videoWidget->hide();
        placeholderLabel->show();

        QFile file(webcamRecorded);
        if (file.size() > 0) {
            qWarning() << "Recording saved successfully";
            renderAgain();
        } else {
        // damn it we tought we were recording but we did not record anything!
            qWarning() << "*FAILURE* File size is zero.";
            logTextEdit->append("Recording ERROR. File size is zero.");

            chooseVideoButton->setEnabled(true);
            loadPlaybackAction->setEnabled(true);
            fetchButton->setEnabled(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            singButton->setEnabled(false);
            
            resetMediaComponents(false);
            QMessageBox::critical(this, "SORRY: mediaRecorder ERROR", "File size is zero.");
        }
    }
}

// recording FINISH button
void MainWindow::stopRecording() {
    try {
        if (!isRecording) {
            qWarning() << "Not recording.";
            logTextEdit->append("Tried to stop Recording, but we are not recording. ERROR.");
            QMessageBox::critical(this, "ERROR.", "Tried to stop Recording, but we are not recording. ERROR.");
            return;
        }

        if ( camera->isAvailable() && camera->isActive() )
            camera->stop();
        else
            mediaCaptureSession->setCamera(nullptr);
        
        if ( mediaRecorder->isAvailable() )
            mediaRecorder->stop();
        if ( audioRecorder->isRecording() )
            audioRecorder->stopRecording();

        recordingIndicator->hide();
        webcamPreviewWidget->hide();
        previewCheckbox->setChecked(false);

        isRecording = false;
        singButton->setText("♪ SING ♪");
        progressSongFull->setToolTip("Nothing to seek");

    } catch (const std::exception &e) {
        logTextEdit->append("Error during stopRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }
}

void MainWindow::handleRecorderError(QMediaRecorder::Error error) {

    qWarning() << "Detected a mediaRecorder error:" << error << mediaRecorder->errorString();
    logTextEdit->append("Recorder Error: " + mediaRecorder->errorString());

    if ( !isRecording ) {
        qWarning() << "Strange, because we are not recording!!";
        return;
    }

    try {
        if (mediaRecorder ) {
            qWarning() << "Stopping media due to error...";
            if ( player && vizPlayer )
                vizPlayer->stop();
            playbackTimer->stop();
            mediaRecorder->stop();
        }
        
    } catch (const std::exception &e) {
        logTextEdit->append("Error during stopRecording (while handling error): " + QString::fromStdString(e.what()));
    }

    handleRecordingError();
    QMessageBox::critical(this, "Recording Error", "An error occurred while recording: " + mediaRecorder->errorString());
}

void MainWindow::handleRecordingError() {
    logTextEdit->append("Attempting to recover from recording error...");

    isRecording = false;

    qDebug() << "Cleaning up..";
    if ( camera )
        camera->stop();
    if ( audioRecorder->isRecording() )
        audioRecorder->stopRecording();

    recordingIndicator->hide();
    webcamPreviewWidget->hide();
    previewCheckbox->setChecked(false);
    videoWidget->hide();
    placeholderLabel->show();

    singButton->setEnabled(false);
    singButton->setText("SING");
    chooseVideoButton->setEnabled(true);
    loadPlaybackAction->setEnabled(true);
    fetchButton->setEnabled(true);
    chooseInputButton->setEnabled(true);
    chooseInputAction->setEnabled(true);

    progressSongFull->setToolTip("Nothing to seek");

    resetMediaComponents(false);

}

// render //
void MainWindow::renderAgain()
{
    if ( player && vizPlayer )
        vizPlayer->stop();
    playbackTimer->stop();
    resetMediaComponents(false);

    renderAgainButton->setVisible(false);
    chooseVideoButton->setEnabled(false);
    loadPlaybackAction->setEnabled(false);
    fetchButton->setEnabled(false);
    progressSongFull->setToolTip("Nothing to seek");

    // choose where to save rendered file
    outputFilePath = QFileDialog::getSaveFileName(this, "Mix destination (default .MP4)", "", "Video or Audio Files (*.mp4 *.mkv *.webm *.avi *.mp3 *.flac *.wav)");
    if (!outputFilePath.isEmpty()) {
        // Check if the file path has a valid extension
        QStringList allowedExtensions = QStringList() << "mp4" << "mkv" << "webm" << "avi" << "mp3" << "flac" << "wav";
        QString selectedExtension = QFileInfo(outputFilePath).suffix().toLower(); 

        if (!allowedExtensions.contains(selectedExtension)) {
            QMessageBox::warning(this, "Invalid File Extension", "Please choose a file with one of the following extensions:\n.mp4, .mkv, .webm, .avi, .mp3, .flac, .wav");
            // Go back to the save destination dialog
            return renderAgain();
        }

        // High resolution or fast render?
        int response = QMessageBox::question(
            this, 
            "Resolution", 
            "Do you want high-resolution video? Low resolution renders much faster.", 
            QMessageBox::Yes | QMessageBox::No, 
            QMessageBox::No
        );
        setRez = (response == QMessageBox::Yes) ? "1920x540" : "640x240";
        qDebug() << "Will vstack each video with resolution:" << setRez;

        // Show the preview dialog
        PreviewDialog dialog(this);
        dialog.setAudioFile(audioRecorded);
        if (dialog.exec() == QDialog::Accepted)
        {
            double vocalVolume = dialog.getVolume();
            mixAndRender(vocalVolume);

        } else {
            QMessageBox::warning(this, "Cancelled?", "Repeating process so you don't lose your performance. Please adjust volume and click render...");             
            return renderAgain();
        }
    } else {
        QMessageBox::warning(this, "Cancelled?", "Please choose a destination for your performance...");
        return renderAgain();
    }
}

void MainWindow::mixAndRender(double vocalVolume) {
    
    videoWidget->hide();
    placeholderLabel->show();
    singButton->setEnabled(false); 
    chooseInputButton->setEnabled(true);
    chooseInputAction->setEnabled(true);

    int totalDuration = getMediaDuration(currentVideoFile);  // Get the total duration
    int recordingDuration = getMediaDuration(webcamRecorded);  // Get the recording duration
    int stopDuration = ( totalDuration - recordingDuration );
    if ( stopDuration < 0 )
        stopDuration = 0;

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
    layout->insertWidget(0, progressBar, 0, Qt::AlignCenter);

    QFileInfo outfileInfo(outputFilePath);
    QFileInfo videofileInfo(currentVideoFile);
    QString videorama = "";

        if ((outputFilePath.endsWith(".mp4", Qt::CaseInsensitive)                       \
        || outputFilePath.endsWith(".avi", Qt::CaseInsensitive)                         \
        || outputFilePath.endsWith(".mkv", Qt::CaseInsensitive)                         \
        || outputFilePath.endsWith(".webm", Qt::CaseInsensitive) )) 
            if ( !( (currentVideoFile.endsWith("mp3", Qt::CaseInsensitive))             \
                ||  (currentVideoFile.endsWith("wav", Qt::CaseInsensitive))             \
                ||  (currentVideoFile.endsWith("flac", Qt::CaseInsensitive)) )) {
                // Combine both recorded and playback videos
                videorama = QString("[1:v]trim=%1ms,setpts=PTS-STARTPTS,scale=%2[webcam]; \
                                    [2:v]scale=%2[video]; \
                                    [video][webcam]vstack[videorama];")
                                    .arg(offset)
                                    .arg(setRez);
                                    
            } else {
                QStringList partsRez = setRez.split("x");
                QString fullRez = QString("%1x%2").arg(partsRez[0].toInt()).arg(partsRez[1].toInt() * 2);
                // No video playback, work only with webcam video
                videorama = QString("[1:v]trim=%1ms,setpts=PTS-STARTPTS, \
                                    scale=%2,tpad=stop_mode=clone:stop_duration=%3[videorama];")
                                    .arg(offset)
                                    .arg(fullRez)
                                    .arg(stopDuration);
            }

    // we Configure autotalent plugin here:
    QString talent = "lv2=http\\\\://gareus.org/oss/lv2/fat1,";

    arguments << "-y"               // Overwrite output file if it exists
          << "-i" << audioRecorded  // audio vocals INPUT file
          << "-i" << webcamRecorded // recorded camera INPUT file
          << "-i" << currentVideoFile // playback file INPUT file
          << "-filter_complex"      // masterization and vocal enhancement of recorded audio
          << QString("[0:a]aformat=channel_layouts=stereo,atrim=%1ms,asetpts=PTS-STARTPTS, \
                        afftdn=nf=-20:nr=10:nt=w,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200,%2 \
                        aecho=0.6:0.4:69|51:0.21|0.13,treble=g=12,volume=%3[vocals]; \
                        [2:a][vocals]amix=inputs=2:normalize=0,aresample=async=1[wakkamix];%4" 
                        ).arg(offset)
                        .arg(talent)
                        .arg(vocalVolume)
                        .arg(videorama);

    // Map audio output
    arguments  << "-ac" << "2"                 // Force stereo
                << "-dither_method" << "none"   // dithering off
                << "-map" << "[wakkamix]";      // ensure audio mix goes on the pack
    if ( !videorama.isEmpty() ) {
        arguments << "-map" << "[videorama]"    // ensure video mix goes on the pack
                <<  "-vsync" << "1";
    }

    arguments << outputFilePath;              // OUTPUT mix

    
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

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, process, progressLabel](int exitCode, QProcess::ExitStatus exitStatus) {
        qWarning() << "FFmpeg finished with exit code" << exitCode << "and status" << (exitStatus == QProcess::NormalExit ? "NormalExit" : "CrashExit");
        
        delete progressLabel;
        delete this->progressBar;
        process->deleteLater(); // Clean up the process object

        if ( exitStatus == QProcess::CrashExit) {
            this->logTextEdit->append("FFmpeg crashed!!");
            chooseVideoButton->setEnabled(true);
            loadPlaybackAction->setEnabled(true);
            fetchButton->setEnabled(true);
            renderAgainButton->setVisible(true);
            QMessageBox::critical(this, "FFmpeg crashed :(", "Aborting.. Verify if FFmpeg is correctly installed and in the PATH to be executed. Verify logs for FFmpeg error. This program requires a LV2 plugin callex X42 by Gareus.");
            return;
        }
        else {
            QMessageBox::information(this, "Rendering Done!", "Prepare to preview performance. You can press RenderAgain button to adjust volume again or select a different filename, file type or resolution.");
            this->logTextEdit->append("FFmpeg finished.");
        }

        QFile file(outputFilePath);
        if (!file.exists()) {
            qWarning() << "Output path does not exist: " << outputFilePath;
            QMessageBox::critical(this, "Check FFmpeg ?", "Aborted playback. Strange error: output file does not exist.");
            return;
        }

        // And now, for something completely different: play the rendered performance! :D

        chooseVideoButton->setEnabled(true);
        loadPlaybackAction->setEnabled(true);
        fetchButton->setEnabled(true);
        renderAgainButton->setVisible(true);
        placeholderLabel->hide();
        videoWidget->show();

        qDebug() << "Setting media source to" << outputFilePath;

        if ( player && vizPlayer ) {

            vizPlayer->stop();
            playbackTimer->stop();

            setBanner("DECODING... PLEASE WAIT.");

            vizPlayer->setMedia(outputFilePath);
            addProgressSong(scene, getMediaDuration(outputFilePath));

            qWarning() << "trimmed rec offset: " << offset << " ms";
            this->logTextEdit->append(QString("trimmed recording Offset: %1 ms").arg(offset));

            this->logTextEdit->append("Playing mix!");
        }

    });

    // Start the process for FFmpeg command and arguments
    process->start("ffmpeg", arguments);

    if (!process->waitForStarted()) {
        qWarning() << "Failed to start FFmpeg process.";
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
        qDebug() << "No match for time regex:" << output;
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

        QString durationText = QString("%1 / %2")
                                .arg(currentTime)
                                .arg(totalTime);
        
        durationTextItem->setPlainText(durationText);
    }       
}

void MainWindow::addProgressSong(QGraphicsScene *scene, qint64 duration) {
     
    disconnect(player.data(), &QMediaPlayer::positionChanged, this, nullptr);

    if (progressSongFull) {
        scene->removeItem(progressSongFull);
        delete progressSongFull;
        progressSongFull = nullptr;
    }
    // Song progress Bar
    progressSongFull = new QGraphicsRectItem(0, 0, 640, 20);
    progressSongFull->setToolTip("Click to seek");
    scene->addItem(progressSongFull);
    progressSongFull->setPos((this->previewView->width() - progressSongFull->boundingRect().width()) / 2, 0);
    
    if (progressSong) {
        scene->removeItem(progressSong);
        delete progressSong;
        progressSong = nullptr;
    }
    // Song progress Bar
    progressSong = new QGraphicsRectItem(0, 0, 0, 20);
    progressSong->setBrush(QBrush(highlightColor));
    scene->addItem(progressSong);
    progressSong->setPos((this->previewView->width() - progressSongFull->boundingRect().width()) / 2, 0);

    // Update progress bar as media plays
    connect(player.data(), &QMediaPlayer::positionChanged, this, [=](qint64 currentPosition) {
        qreal progress = qreal(currentPosition) / (duration * 1000);
        progressSong->setRect(0, 0, 640 * progress, 20);
    });

}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    
    // Check if the event is a mouse press event in a QGraphicsScene
    if (event->type() == QEvent::GraphicsSceneMousePress) {
        // Cast the event to QGraphicsSceneMouseEvent
        QGraphicsSceneMouseEvent *mouseEvent = dynamic_cast<QGraphicsSceneMouseEvent *>(event);
        if (mouseEvent) {
            
            QPointF clickPos = mouseEvent->scenePos();  // Get the mouse click position in scene coordinates

            if ( progressSong ) {
                if ( !isRecording ) {
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
                        qint64 newPosition = static_cast<qint64>(progress * player->duration());

                        vizPlayer->seek(newPosition); // Seek the media player to the clicked position

#ifdef __linux__
// Avoid breaking sound when seeking (Qt6.4 or gStreamer bug? on Linux only..)
                        player->pause(); // Pause for a smooth workaround
                        player->setAudioOutput(nullptr); // first, detach the audio output 
                        player->setAudioOutput(audioOutput.data()); // now gimme back my sound mon
                        player->play(); // the show must go on!
#endif
                        return true;  // Event handled
                    }
                }
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
        QString fetchVideoPath = QFileDialog::getOpenFileName(this, "Choose the downloaded playback or any another", directory, "Videos (*.mp4 *.mkv *.avi)");
        if (!fetchVideoPath.isEmpty()) {

            resetMediaComponents(false);

            placeholderLabel->hide();
            videoWidget->show();
            
            currentVideoFile = fetchVideoPath;  // Store the video playback file path
            if ( player && vizPlayer ) {
                vizPlayer->stop();
                playbackTimer->stop();
                
                setBanner("DECODING... Please wait.");

                vizPlayer->setMedia(currentVideoFile);
                currentVideoName = QFileInfo(currentVideoFile).completeBaseName();
                addProgressSong(scene, getMediaDuration(currentVideoFile));

                downloadStatusLabel->setText("Download YouTube URL");
                singButton->setEnabled(true); 
                logTextEdit->append("Previewing playback. Press SING to start recording.");
            }
            
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
        qDebug() << "Camera preview will be enabled.";
        webcamPreviewWidget->show();
        if ( camera->isAvailable() && !camera->isActive() )
            camera->start();
    } else {
        webcamPreviewWidget->hide();
        qDebug() << "Camera preview hidden.";
    }
}

void MainWindow::onVideoFrameReceived(const QVideoFrame &frame) {
    // Forward the video frame to all widgets
    if (frame.isValid()) {
        // Create a mutable copy
        QVideoFrame mutableFrame = frame;
        proxyVideoFrame(mutableFrame); // Pass the mutable copy (maybe expensive, but safer)
    }
}

void MainWindow::proxyVideoFrame(QVideoFrame &frame) {
    if (!frame.isMapped()) {
        // Map the frame for reading
        if (!frame.map(QVideoFrame::ReadOnly)) {
            return;
        }
    }

    // Convert the frame to a QImage
    QImage img = frame.toImage();
    if (!img.isNull()) {
        // Update the main VideoDisplayWidget
        if (webcamPreviewWidget) {
            webcamPreviewWidget->setImage(img);
        }

        // Update each VideoDisplayWidget in the list
        for (VideoDisplayWidget* widget : previewWidgets) {
            widget->setImage(img);
        }
    }

    frame.unmap();
}

void MainWindow::addVideoDisplayWidgetInDialog() {
    // is already open?
    if (webcamDialog && webcamDialog->isVisible() ) {
        // bring it to the front, captain
        webcamDialog->raise();
        webcamDialog->activateWindow();
        return;
    }

    webcamDialog = new QDialog(this);
    webcamDialog->setWindowTitle("Webcam Preview");
    webcamDialog->setFixedSize(1024, 768); 
    VideoDisplayWidget *newWidget = new VideoDisplayWidget(webcamDialog);
    newWidget->setMinimumSize(1024, 768);
    
    // Add to the list
    previewWidgets.append(newWidget);

    QVBoxLayout *layout = new QVBoxLayout(webcamDialog);
    layout->addWidget(newWidget);
    webcamDialog->setLayout(layout);

    // signal to cleanup
    connect(webcamDialog, &QDialog::finished, this, [this]() {
        // prevent memory leaks
        webcamDialog = nullptr;
    });

    // Show me show me show me
    webcamDialog->show();
}

MainWindow::~MainWindow() {
    
    qDeleteAll(previewWidgets);
    previewWidgets.clear();

    if (isRecording) {
        stopRecording();
    }

    disconnectAllSignals();

    if ( player )
        player.reset();
    if ( audioOutput )
        audioOutput.reset();
    if ( videoSink )
        videoSink.reset();
    if ( mediaRecorder )
        mediaRecorder.reset();
    if ( mediaCaptureSession )
        mediaCaptureSession.reset();
    if ( audioRecorder )
        audioRecorder.reset();
    if ( format )
        format.reset();
    if ( videoWidget )
        delete videoWidget;
    if ( soundLevelWidget )
        delete soundLevelWidget;
    if ( progressSong )
        delete progressSong;
    if ( vizPlayer )
        vizPlayer.reset();
    if ( vizWidget )
        delete vizWidget;
}

// Function to disconnect all signals
void MainWindow::disconnectAllSignals() {

    if ( audioRecorder )
        disconnect(audioRecorder.data(), &AudioRecorder::deviceLabelChanged, this, &MainWindow::updateDeviceLabel);

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

void MainWindow::setBanner(const QString &msg) {
    banner->setText(msg);
}

void MainWindow::resizeEvent(QResizeEvent* event) {

    QMainWindow::resizeEvent(event);

    // Update the scene size to match the view size
    QRectF sceneRect = previewView->rect(); 
    previewView->scene()->setSceneRect(sceneRect); 
    qreal sceneWidth = previewView->scene()->width();
    
    if ( this->progressSongFull ) {
        progressSongFull->setX((sceneWidth - progressSongFull->boundingRect().width()) / 2);
    }
    if ( this->progressSong ) {
        progressSong->setX((sceneWidth - progressSongFull->boundingRect().width()) / 2);
    }    
    
    durationTextItem->setTextWidth(durationTextItem->boundingRect().width());
    durationTextItem->setX((this->previewView->width() - durationTextItem->boundingRect().width()) / 2);
    
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