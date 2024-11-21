#include "mainwindow.h"
#include "complexes.h"
#include "sndwidget.h"

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
    chooseInputAction = new QAction("Choose Input Devices", this);
    singAction = new QAction("SING", this);
    QAction *exitAction = new QAction("Exit", this);
    menuBar->setFont(QFont("Arial", 8));
    
    helpMenu->addAction(aboutQtAction);
    helpMenu->addAction(aboutWakkaQtAction);
    fileMenu->addAction(loadPlaybackAction);
    fileMenu->addAction(chooseInputAction);
    fileMenu->addAction(singAction);
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
            <p>This app features webcam recording, YouTube video downloading, real-time sound visualization, and post-recording video rendering with FFmpeg. It automatically does some mastering on the vocal tracks. It also has a custom AutoTuner class called VocalEnhancer that provides slight pitch shift/correction and formant preservation.</p>
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
    videoWidget->setMinimumSize(320, 248);
    videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget->hide();
    
    // Create a QLabel to display the placeholder image
    placeholderLabel = new QLabel(this);
    placeholderLabel->setMinimumSize(320, 248);
    QPixmap placeholderPixmap(":/images/logo.jpg");
    if (placeholderPixmap.isNull()) {
        qWarning() << "Failed to load placeholder image!";
    } else {
        placeholderLabel->setPixmap(placeholderPixmap.scaled(640, 640, Qt::KeepAspectRatio));
    }
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    placeholderLabel->setScaledContents(true);

    // Create the webcam preview
    webcamScene = new QGraphicsScene(this);
    webcamScene->setSceneRect(0, 0, 196, 88);
    webcamView = new QGraphicsView(webcamScene, this);
    webcamView->setFixedSize(196, 88);
    webcamView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    webcamView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    webcamPreviewItem = new QGraphicsVideoItem();
    webcamPreviewItem->setSize(QSizeF(196, 88));
    webcamPreviewItem->setToolTip("Click to open large preview");
    webcamScene->addItem(webcamPreviewItem);
    webcamPreviewLayout = new QHBoxLayout();
    webcamPreviewLayout->addWidget(webcamView, 0, Qt::AlignCenter);
   
    // Create the scene and view
    progressScene = new QGraphicsScene(this);
    progressView = new QGraphicsView(progressScene, this);
    progressView->setMinimumSize(640, 45);
    progressView->setMaximumHeight(50);
    progressView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    progressView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    progressView->setAlignment(Qt::AlignCenter);
    qreal viewWidth = progressView->width();
    qreal viewHeight = progressView->height();
    // Create playback chronometer text item
    durationTextItem = new QGraphicsTextItem;
    durationTextItem->setDefaultTextColor(palette.color(QPalette::Text));
    durationTextItem->setFont(QFont("Courier", 12, QFont::Bold));
    durationTextItem->setPlainText("00:00:00 / 00:00:00");
    durationTextItem->setY(progressView->height()/2);
    progressScene->addItem(durationTextItem);
    progressScene->setSceneRect(0, 0, viewWidth, viewHeight);

    banner = new QLabel(Wakka_welcome, this);
    banner->setTextFormat(Qt::TextFormat::RichText);
    banner->setFont(QFont("Arial", 11, QFont::Bold));
    banner->setAlignment(Qt::AlignCenter);
    banner->setToolTip("Here be the song title!");
    setBanner(Wakka_welcome);

    // Create buttons
    QPushButton *exitButton = new QPushButton("Exit", this);
    chooseVideoButton = new QPushButton("Load playback from disk", this);
    chooseVideoButton->setToolTip("Load media files from disk");
    singButton = new QPushButton("♪ SING ♪", this);
    singButton->setFont(QFont("Arial", 21));
    singButton->setToolTip("Start/Stop recording after loaded playback");
    chooseInputButton = new QPushButton("Choose Input Devices", this);
    renderAgainButton = new QPushButton("RENDER AGAIN", this);
    renderAgainButton->setToolTip("Repeat render and adjustments without singing again");

    // Recording indicator
    recordingIndicator = new QLabel("⦿ rec", this);
    recordingIndicator->setStyleSheet("color: red;");
 
    // custom options
    previewCheckbox = new QCheckBox("Cam Preview");
    previewCheckbox->setFont(QFont("Arial", 8));
    previewCheckbox->setToolTip("Toggle camera preview");
    vizCheckbox = new QCheckBox("Audio Visualizer");
    vizCheckbox->setFont(QFont("Arial", 8));
    vizCheckbox->setToolTip("Freeze Audio Visualizer");
    vizCheckbox->setChecked(true);
    
    QHBoxLayout *indicatorLayout = new QHBoxLayout();
    indicatorLayout->addStretch();
    indicatorLayout->addWidget(recordingIndicator, 0, Qt::AlignCenter);
    indicatorLayout->addWidget(vizCheckbox, 0, Qt::AlignRight);
    indicatorLayout->addWidget(previewCheckbox, 0, Qt::AlignRight);
    //indicatorLayout->addStretch();

    // Instantiate SndWidget
    soundLevelWidget = new SndWidget(this);
    soundLevelWidget->setMinimumSize(640, 25);
    soundLevelWidget->setMaximumHeight(32);
    soundLevelWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    soundLevelWidget->setToolTip("Sound input visualization widget");

    // Device label
    deviceLabel = new QLabel("Selected Device: None", this);
    deviceLabel->setFont(QFont("Arial", 9));
    deviceLabel->setToolTip("Changing the default source input in the system cfg will not reflect the information here");
    
    // YT downloader
    urlInput = new QLineEdit(this);
    urlInput->setPlaceholderText("https://www.youtube.com/?v=aBcDFgETc");
    urlInput->setFont(QFont("Arial", 10));
    urlInput->setToolTip("Paste a URL here and fetch your karaoke media");
    fetchButton = new QPushButton("FETCH", this);
    fetchButton->setFont(QFont("Arial", 12));
    fetchButton->setToolTip("Click here and download URL to disk");
    downloadStatusLabel = new QLabel("Download media", this);
    downloadStatusLabel->setFont(QFont("Arial", 8));
    downloadStatusLabel->setToolTip("Several URL besides YouTube will work");
    QHBoxLayout *fetchLayout = new QHBoxLayout;
    fetchLayout->addWidget(urlInput);
    fetchLayout->addWidget(fetchButton);
    fetchLayout->addWidget(downloadStatusLabel);

    // Log text
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logUI(Wakka_welcome);
    logTextEdit->setMinimumHeight(50);
    logTextEdit->setMaximumHeight(90);
    logTextEdit->setFont(QFont("Arial", 10));
    logTextEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Move cursor to the end of the text when text is appended
    connect(logTextEdit, &QTextEdit::textChanged, this, [=]() {
        logTextEdit->moveCursor(QTextCursor::End);
        logTextEdit->ensureCursorVisible();
    });

    // Instantiate Audio Visualizers
    QHBoxLayout *vizLayout = new QHBoxLayout();
    vizUpperLeft = new AudioVisualizerWidget(this);
    vizUpperRight = new AudioVisualizerWidget(this);
    vizUpperLeft->setMinimumHeight(32);
    vizUpperRight->setMinimumHeight(32);
    vizUpperLeft->setMaximumHeight(64);
    vizUpperRight->setMaximumHeight(64);
    vizUpperLeft->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vizUpperRight->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vizLayout->addWidget(vizUpperLeft);
    vizLayout->addLayout(webcamPreviewLayout);
    vizLayout->addWidget(vizUpperRight);

    // Layout
    QWidget *containerWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(containerWidget);
    layout->addLayout(indicatorLayout);
    layout->addWidget(banner);
    layout->addLayout(vizLayout);
    layout->addWidget(progressView);
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
    chooseVideoButton->setVisible(true);
    chooseInputButton->setVisible(false);
    soundLevelWidget->setVisible(true);
    webcamPreviewItem->setVisible(true);
    recordingIndicator->hide();
    webcamView->hide();
    singButton->setEnabled(false);
    singAction->setEnabled(false);
    renderAgainButton->setVisible(false);
    exitButton->setVisible(false);
    deviceLabel->setVisible(true);

    // Connections
    connect(exitButton, &QPushButton::clicked, this, &QMainWindow::close);
    connect(chooseVideoButton, &QPushButton::clicked, this, &MainWindow::chooseVideo);
    connect(chooseInputButton, &QPushButton::clicked, this, &MainWindow::chooseInputDevice);
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
    connect(loadPlaybackAction, &QAction::triggered, this, &MainWindow::chooseVideo);
    connect(chooseInputAction, &QAction::triggered, this, &MainWindow::chooseInputDevice);
    connect(singButton, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(singAction, &QAction::triggered, this, &MainWindow::startRecording);
    connect(fetchButton, &QPushButton::clicked, this, &MainWindow::fetchVideo);
    connect(renderAgainButton, &QPushButton::clicked, this, &MainWindow::renderAgain);
    connect(previewCheckbox, &QCheckBox::toggled, this, &MainWindow::onPreviewCheckboxToggled);
    connect(vizCheckbox, &QCheckBox::toggled, this, &MainWindow::onVizCheckboxToggled);

    playbackTimer = new QTimer(this);
    connect(playbackTimer, &QTimer::timeout, this, &MainWindow::updatePlaybackDuration);

    progressScene->installEventFilter(this);
    webcamScene->installEventFilter(this);

    chooseInputDevice();
    resetMediaComponents(true);

}


void MainWindow::resetMediaComponents(bool isStarting) {

    QCoreApplication::processEvents();

    qDebug() << "Resetting media components";

    isRecording = false;

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

    // Reconfigure ALL components //
    audioRecorder.reset(new AudioRecorder(selectedDevice, this));
    connect(audioRecorder.data(), &AudioRecorder::deviceLabelChanged, this, &MainWindow::updateDeviceLabel);
    audioRecorder->initialize();

    player.reset(new QMediaPlayer(this));
    audioOutput.reset(new QAudioOutput(this));
    format.reset(new QMediaFormat);
    camera.reset(new QCamera(selectedCameraDevice, this));
    mediaRecorder.reset(new QMediaRecorder(this));
    mediaCaptureSession.reset(new QMediaCaptureSession(this));

    // Setup Media player
    player->setVideoOutput(videoWidget);
    player->setAudioOutput(audioOutput.data());
    vizPlayer.reset(new AudioVizMediaPlayer(player.data(), vizUpperLeft, vizUpperRight, this));
    onVizCheckboxToggled(vizCheckbox->isChecked());
    
    // Setup SndWidget
    soundLevelWidget->setInputDevice(selectedDevice);
    
    format->setFileFormat(QMediaFormat::FileFormat::MPEG4);
    format->setVideoCodec(QMediaFormat::VideoCodec::H264);
    format->setAudioCodec(QMediaFormat::AudioCodec::AAC);

    // Setup Media recorder
    mediaRecorder->setMediaFormat(*format);
    mediaRecorder->setOutputLocation(QUrl::fromLocalFile(webcamRecorded));
    mediaRecorder->setQuality(QMediaRecorder::VeryHighQuality);
    //mediaRecorder->setVideoFrameRate(25); 
    
    qDebug() << "Configuring mediaCaptureSession..";
    mediaCaptureSession->setVideoOutput(webcamPreviewItem);
    mediaCaptureSession->setCamera(camera.data());
    mediaCaptureSession->setAudioInput(nullptr);
    mediaCaptureSession->setRecorder(mediaRecorder.data());

    camera->start();

    connect(mediaRecorder.data(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
    connect(mediaRecorder.data(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);
    connect(player.data(), &QMediaPlayer::mediaStatusChanged, this, &MainWindow::onPlayerMediaStatusChanged);
    connect(player.data(), &QMediaPlayer::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);

    qDebug() << "Reconfigured media components";

}

void MainWindow::chooseInputDevice() {
    selectedDevice = QMediaDevices::defaultAudioInput();  // Default to the default audio input device

    // Get available audio and video input devices
    QList<QAudioDevice> audioInputs = QMediaDevices::audioInputs();
    QList<QCameraDevice> videoInputs = QMediaDevices::videoInputs();

    // Check if there are any devices available
    if (audioInputs.isEmpty() || videoInputs.isEmpty()) {
        QString missingDevices;
        if (audioInputs.isEmpty()) {
            missingDevices.append("No audio devices available.\n");
        }
        if (videoInputs.isEmpty()) {
            missingDevices.append("No video devices available.");
        }

        QMessageBox::critical(this, "Device Error", missingDevices);
        exit(-1);  // Abort the program if devices are missing
        return;
    }

    // Create a dialog to show the lists of devices
    QDialog *deviceDialog = new QDialog();
    deviceDialog->setWindowTitle("WakkaQt - Select Input Devices");
    deviceDialog->setFixedSize(400, 250);

    // Create a layout for the dialog
    QVBoxLayout *layout = new QVBoxLayout(deviceDialog);

    // Create a tab widget to separate audio and video device selection
    QTabWidget *tabWidget = new QTabWidget(deviceDialog);

    // Create a widget for audio input devices
    QWidget *audioTab = new QWidget();
    QVBoxLayout *audioLayout = new QVBoxLayout(audioTab);
    QListWidget *audioList = new QListWidget(audioTab);

    for (const QAudioDevice &device : audioInputs) {
        QListWidgetItem *item = new QListWidgetItem(device.description());
        item->setData(Qt::UserRole, device.id());
        audioList->addItem(item);
    }
    audioLayout->addWidget(audioList);
    tabWidget->addTab(audioTab, "Audio Devices");

    // Create a widget for video input devices
    QWidget *videoTab = new QWidget();
    QVBoxLayout *videoLayout = new QVBoxLayout(videoTab);
    QListWidget *videoList = new QListWidget(videoTab);

    for (const QCameraDevice &device : videoInputs) {
        QListWidgetItem *item = new QListWidgetItem(device.description());
        item->setData(Qt::UserRole, device.id());
        videoList->addItem(item);
    }
    videoLayout->addWidget(videoList);
    tabWidget->addTab(videoTab, "Video Devices");

    layout->addWidget(tabWidget);

    // Create a button to confirm the selection
    QPushButton *selectButton = new QPushButton("Select", deviceDialog);
    layout->addWidget(selectButton);

    // Create a button to exit the program
    QPushButton *exitButton = new QPushButton("Exit Program", deviceDialog);
    layout->addWidget(exitButton);

    // Connect the select button to handle selection
    connect(selectButton, &QPushButton::clicked, this, [this, audioList, videoList, deviceDialog]() {
        // Handle audio device selection
        QListWidgetItem *selectedAudioItem = audioList->currentItem();
        if (selectedAudioItem) {
            QString selectedAudioDeviceId = selectedAudioItem->data(Qt::UserRole).toString();
            QList<QAudioDevice> audioInputs = QMediaDevices::audioInputs();
            selectedDevice = QAudioDevice();  // Reset the selected device

            for (const QAudioDevice &device : audioInputs) {
                if (device.id() == selectedAudioDeviceId) {
                    selectedDevice = device;
                    break;
                }
            }
        }

        // Handle video device selection
        QListWidgetItem *selectedVideoItem = videoList->currentItem();
        if (selectedVideoItem) {
            QString selectedVideoDeviceId = selectedVideoItem->data(Qt::UserRole).toString();
            QList<QCameraDevice> videoInputs = QMediaDevices::videoInputs();
            selectedCameraDevice = QCameraDevice();  // Reset the selected camera device

            for (const QCameraDevice &device : videoInputs) {
                if (device.id() == selectedVideoDeviceId) {
                    selectedCameraDevice = device;
                    break;
                }
            }
        }

        // Close the dialog
        deviceDialog->accept();

        // Check if the selected devices are valid
        if (selectedDevice.isNull()) {
            QMessageBox::warning(this, "Audio Device Error", "The selected audio input device is invalid.");
        } else {
            // Set up audio recording
            audioRecorder.reset(new AudioRecorder(selectedDevice, this));
            connect(audioRecorder.data(), &AudioRecorder::deviceLabelChanged, this, &MainWindow::updateDeviceLabel);
            audioRecorder->initialize();
            soundLevelWidget->setInputDevice(selectedDevice);
        }

        if (!selectedCameraDevice.isNull()) {
            // Set up Video recording
            mediaCaptureSession.reset(new QMediaCaptureSession(this));  // Set up media session
            camera.reset(new QCamera(selectedCameraDevice, this));  // Set up camera
            mediaCaptureSession->setVideoOutput(this->webcamPreviewItem);
            mediaCaptureSession->setCamera(camera.data());
            mediaCaptureSession->setAudioInput(nullptr);
            mediaCaptureSession->setRecorder(mediaRecorder.data());
            camera->start();
        }
    });

    // Connect the exit button to close the program
    connect(exitButton, &QPushButton::clicked, this, []() {
        exit(0);
    });

    // Execute the dialog
    if (deviceDialog->exec() == QDialog::Rejected) {
        QMessageBox::information(this, "Cancelled", "Device selection was cancelled.");
    }

    delete deviceDialog;  // Clean up the dialog after it is closed
}


void MainWindow::updateDeviceLabel(const QString &deviceLabelText) {
    if (deviceLabel) { // this only works for selected devices within the app,
                        // I could not figure a way to detect changes in the system default input src
        deviceLabel->setText(QString("Audio Input Device: %1").arg(deviceLabelText));
    }
}

 void MainWindow::playVideo(const QString& playbackVideoPath) {

    if ( player && vizPlayer ) {

        isPlayback = false; // disable seeking while decoding/loading new media

        vizPlayer->stop();
        playbackTimer->stop();
        
        if (progressSongFull) {
            progressScene->removeItem(progressSongFull);
            delete progressSongFull;
            progressSongFull = nullptr;
        }
        if (progressSong) {
            progressScene->removeItem(progressSong);
            delete progressSong;
            progressSong = nullptr;
        }

        setBanner("DECODING... Please wait."); 
                
        currentVideoName = QFileInfo(playbackVideoPath).completeBaseName();
        currentPlayback = playbackVideoPath;
        vizPlayer->setMedia(playbackVideoPath);
    }

 }

void MainWindow::chooseVideo()
{

    qint64 lastPos = player->position();
    vizPlayer->stop(); // stop to prevent "Unexpected FFmpeg behaviour"

    QFileDialog* fileDialog = new QFileDialog(this);
    fileDialog->setWindowTitle("Open Playback File");
    fileDialog->setNameFilter("Video or Audio (*.mp4 *.mkv *.webm *.avi *.mov *.mp3 *.wav *.flac)");
    if (fileDialog->exec() == QDialog::Accepted) {
        QString loadVideoFile = fileDialog->selectedFiles().first();
        if (!loadVideoFile.isEmpty()) {

            resetMediaComponents(false);

            singButton->setEnabled(true);
            singAction->setEnabled(true);
            renderAgainButton->setVisible(false);
            placeholderLabel->hide();
            videoWidget->show();

            currentVideoFile = loadVideoFile;
            playVideo(currentVideoFile);
            logUI("Playback preview. Press SING to start recording.");

        }
    } else
        if ( isPlayback )
            QTimer::singleShot(500, this, [this, lastPos]() {
                vizPlayer->seek(lastPos, true);
                #if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
                #ifdef __linux__
                    player->setAudioOutput(nullptr); // first, detach the audio output 
                    player->setAudioOutput(audioOutput.data()); // now gimme back my sound mon
                #endif
                #endif
                vizPlayer->play();
            }); // resume play

    delete fileDialog;
}

void MainWindow::onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status) {

    if ( QMediaPlayer::MediaStatus::LoadedMedia == status && !isPlayback ) {

        setBanner(currentVideoName); // video loaded, set title
        banner->setToolTip(currentVideoName);

        if ( !playbackTimer->isActive())
            playbackTimer->start(1000);

        vizPlayer->play();
                
    }

    if ( QMediaPlayer::MediaStatus::EndOfMedia == status ) {

        if ( isRecording && mediaRecorder->duration() )
            stopRecording();

    }

}

void MainWindow::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {

    if ( QMediaPlayer::PlayingState == state ) {
        
        if ( !isPlayback ) { 
            addProgressSong(progressScene, static_cast<int>(getMediaDuration(currentPlayback)));
        }            

        isPlayback = true; // enable seeking now

    }

}

void MainWindow::onPlayerPositionChanged(qint64 position) {
    if ( isRecording ) {
        pos = position - offset;
        sysLatency.restart();
    }
}

void MainWindow::startRecording() {
    try {
        if (currentVideoFile.isEmpty()) {
            QMessageBox::warning(this, "No playback set", "No playback loaded! Please load a playback to sing.");
            singButton->setEnabled(false);
            singAction->setEnabled(false);
            return;
        }

        if (isRecording) {
            qWarning() << "Stop recording.";
            stopRecording();
            return;
        }

        // Disable buttons while recording starts
        singButton->setEnabled(false);
        singAction->setEnabled(false);
        chooseInputButton->setEnabled(false);
        chooseInputAction->setEnabled(false);
        enable_playback(false);

        // Set up the house for recording
        offset = 0;
        isRecording = true;

        if (camera && mediaRecorder && player && vizPlayer) {

            connect(player.data(), &QMediaPlayer::positionChanged, this, &MainWindow::onPlayerPositionChanged);
            connect(mediaRecorder.data(), &QMediaRecorder::durationChanged, this, &MainWindow::onRecorderDurationChanged);

            camera->start(); // prep camera first

            // rewind current playback to start performance
            vizPlayer->seek(0, true);
            player->pause();

#if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
    #ifdef __linux__
            player->setAudioOutput(nullptr);
            player->setAudioOutput(audioOutput.data());
    #endif
#endif
            audioRecorder->startRecording(audioRecorded); // start audio recorder
            mediaRecorder->record(); // start recording video
            
            player->play(); // start the show
                       
        } else {
            qWarning() << "Failed to initialize camera, media recorder or player.";
        }

    } catch (const std::exception &e) {
        logUI("Error during startRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }
}

void MainWindow::onRecorderStateChanged(QMediaRecorder::RecorderState state) {

    if ( QMediaRecorder::RecordingState == state ) {
        
        // Update UI to show recording status
        recordingIndicator->show();
        singButton->setText("Finish!");
        singAction->setText("Finish recording");
        singButton->setEnabled(true);
        singAction->setEnabled(true);

    }
    
}

void MainWindow::onRecorderDurationChanged(qint64 currentDuration) {
    
    if ( sysLatency.isValid() ) {
        if ( sysLatency.elapsed() > offset )
            offset = sysLatency.elapsed();
        sysLatency.invalidate();
    }
    
}

// recording FINISH button
void MainWindow::stopRecording() {

    try {
        if (!isRecording) {
            qWarning() << "Not recording.";
            logUI("Tried to stop Recording, but we are not recording. ERROR.");
            QMessageBox::critical(this, "ERROR.", "Tried to stop Recording, but we are not recording. ERROR.");
            return;
        }
        setBanner(".. .ENHANCING VOCALS.. .");
        isRecording = false;
        disconnect(player.data(), &QMediaPlayer::positionChanged, this, &MainWindow::onPlayerPositionChanged);

        recordingIndicator->hide();
        webcamView->hide();
        previewCheckbox->setChecked(false);

        if ( player ) {
            vizPlayer->stop();
            playbackTimer->stop();
        }

        if ( mediaRecorder->isAvailable() ) {
            mediaRecorder->stop();
        }

        if ( audioRecorder->isRecording() )
            audioRecorder->stopRecording();

        if ( camera->isAvailable() && camera->isActive() )
            camera->stop();

        qWarning() << "Recording stopped.";
        logUI("Recording Stopped");

        singButton->setText("♪ SING ♪");
        singAction->setText("SING");
        vizCheckbox->setEnabled(true);
        progressSongFull->setToolTip("Nothing to seek");
        
        videoWidget->hide();
        placeholderLabel->show();

        QFile fileAudio(audioRecorded);
        QFile fileCam(webcamRecorded);
        if (fileAudio.size() > 0 && fileCam.size() > 0 ) {

            // DETERMINE videoOffset
            qint64 recDuration = 1000 * getMediaDuration(webcamRecorded);
            videoOffset = offset + recDuration - pos;
            // DETERMINE audioOffset
            recDuration = 1000 * getMediaDuration(audioRecorded);
            audioOffset = offset + recDuration - pos;

            qWarning() << "System Latency: " << offset << " ms";
            qWarning() << "Audio Gap: " << audioOffset << " ms";
            qWarning() << "Video Gap: " << videoOffset << " ms";
            logUI(QString("System Latency: %1 ms").arg(offset));
            logUI(QString("Calculated Camera Offset: %1 ms").arg(videoOffset));
            logUI(QString("Calculated Audio Offset: %1 ms").arg(audioOffset));
            
            
            QString sourceFilePath = extractedPlayback;
            QString destinationFilePath = extractedTmpPlayback;

            QFile sourceFile(sourceFilePath);

            if (sourceFile.exists()) {
                // Check if the destination file exists
                if (QFile::exists(destinationFilePath)) {
                    // delete the existing file
                    if (!QFile::remove(destinationFilePath)) {
                        qDebug() << "Failed to remove existing file:" << destinationFilePath;
                        return;
                    }
                }

                // Attempt to copy the file
                if (QFile::copy(sourceFilePath, destinationFilePath)) {
                    qDebug() << "File copied successfully to" << destinationFilePath;
                } else {
                    qDebug() << "Failed to copy file to" << destinationFilePath;
                }
            } else {
                qDebug() << "Source file does not exist:" << sourceFilePath;
            }

            qWarning() << "Recording saved successfully";
            setBanner("Recording saved successfully!");
            renderAgain();

        } else {
            qWarning() << "*FAILURE* File size is zero.";
            logUI("Recording ERROR. File size is zero.");
            setBanner("Recording ERROR. File size is zero.");

            enable_playback(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            singButton->setEnabled(false);
            singAction->setEnabled(false);
            
            resetMediaComponents(false);
            QMessageBox::critical(this, "SORRY: mediaRecorder ERROR", "File size is zero.");
        }

    } catch (const std::exception &e) {
        logUI("Error during stopRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }

}

void MainWindow::handleRecorderError(QMediaRecorder::Error error) {

    qWarning() << "Detected camera error:" << error << mediaRecorder->errorString();
    logUI("Camera Error: " + mediaRecorder->errorString());

    if ( !isRecording ) {
        return;
    }

    handleRecordingError();
    QMessageBox::critical(this, "Recording Error", "An error occurred while recording: " + mediaRecorder->errorString());

}

void MainWindow::handleRecordingError() {

    logUI("Attempting to recover from recording error...");
    setBanner("Attempting to recover from recording error...");
    
    isRecording = false;

    qWarning() << "Cleaning up..";

    if ( vizPlayer && player )
        vizPlayer->stop();

    if ( audioRecorder->isRecording() )
        audioRecorder->stopRecording();

    recordingIndicator->hide();
    webcamView->hide();
    previewCheckbox->setChecked(false);
    videoWidget->hide();
    placeholderLabel->show();

    singButton->setEnabled(false);
    singAction->setEnabled(false);
    singButton->setText("SING");
    singAction->setText("SING");
    enable_playback(true);
    chooseInputButton->setEnabled(true);
    chooseInputAction->setEnabled(true);
    vizCheckbox->setEnabled(true);

}

// render //
void MainWindow::renderAgain()
{
    vizPlayer->stop();
    playbackTimer->stop();
    
    resetMediaComponents(false);

    isPlayback = false; // to avoid seeking while rendering

    renderAgainButton->setVisible(false);
    enable_playback(false);
    progressSongFull->setToolTip("Nothing to seek");
    setBanner("Let's preview performance and render!");
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
            "Do you want 1920x1080 high-resolution video? Low resolution 640x480 renders much more faster.", 
            QMessageBox::Yes | QMessageBox::No, 
            QMessageBox::No
        );
        setRez = (response == QMessageBox::Yes) ? "1920x540" : "640x240";
        qDebug() << "Will vstack each video with resolution:" << setRez;

        // Show the preview dialog
        previewDialog.reset(new PreviewDialog(audioOffset, this));
        previewDialog->setAudioFile(audioRecorded);
        if (previewDialog->exec() == QDialog::Accepted)
        {

            double vocalVolume = previewDialog->getVolume();
            previewDialog.reset();
            mixAndRender(vocalVolume);

        } else {
            enable_playback(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            singButton->setEnabled(false);
            singAction->setEnabled(false);
            previewDialog.reset();
            QMessageBox::warning(this, "Performance cancelled", "Performance cancelled during volume adjustment.");
        }
    } else {
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        singButton->setEnabled(false);
        singAction->setEnabled(false);
        QMessageBox::warning(this, "Performance cancelled", "Performance cancelled!");
    }
}

void MainWindow::mixAndRender(double vocalVolume) {
    
    videoWidget->hide();
    placeholderLabel->show();
    singButton->setEnabled(false);
    singAction->setEnabled(false);
    chooseInputButton->setEnabled(true);
    chooseInputAction->setEnabled(true);

    int totalDuration = static_cast<int>(getMediaDuration(currentVideoFile));  // Get the total duration
    int recordingDuration = static_cast<int>(getMediaDuration(webcamRecorded));  // Get the recording duration
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

            // if OUTPUT is video and playback song is not AUDIO only...
        if ((outputFilePath.endsWith(".mp4", Qt::CaseInsensitive)                       \
        || outputFilePath.endsWith(".avi", Qt::CaseInsensitive)                         \
        || outputFilePath.endsWith(".mkv", Qt::CaseInsensitive)                         \
        || outputFilePath.endsWith(".webm", Qt::CaseInsensitive) )) 
            if ( !( (currentVideoFile.endsWith("mp3", Qt::CaseInsensitive))             \
                ||  (currentVideoFile.endsWith("wav", Qt::CaseInsensitive))             \
                ||  (currentVideoFile.endsWith("flac", Qt::CaseInsensitive)) )) {
                // ...Combine both recorded and playback videos
                videorama = QString("[1:v]trim=%1ms,setpts=PTS-STARTPTS,scale=%2[webcam]; \
                                    [2:v]scale=%2[video]; \
                                    [video][webcam]vstack[videorama];")
                                    .arg(videoOffset)
                                    .arg(setRez);
                                    
            } else { // output is VIDEO but input playback is AUDIO only
                QStringList partsRez = setRez.split("x");
                QString fullRez = QString("%1x%2").arg(partsRez[0].toInt()).arg(partsRez[1].toInt() * 2);
                // No video on playback, work only with webcam recorded video
                videorama = QString("[1:v]trim=%1ms,setpts=PTS-STARTPTS, \
                                    scale=%2,tpad=stop_mode=clone:stop_duration=%3[videorama];")
                                    .arg(videoOffset)
                                    .arg(fullRez)
                                    .arg(stopDuration);
            }

    arguments << "-y"               // Overwrite output file if it exists
          << "-i" << tunedRecorded  // tuned audio vocals INPUT file
          << "-i" << webcamRecorded // recorded camera INPUT file
          << "-i" << currentVideoFile // playback song INPUT file
          << "-filter_complex"      // NOW, masterization and vocal enhancement of recorded audio
          << QString("[0:a]volume=%1,%2[vocals]; \
                        [2:a][vocals]amix=inputs=2:normalize=0,aresample=async=1[wakkamix];%3" 
                        )
                        .arg(vocalVolume)
                        .arg(_filterTreble)
                        .arg(videorama);

    if ( !videorama.isEmpty() ) {
        arguments << "-map" << "[videorama]";    // ensure video mix goes on the pack
    }
    // Map audio output
    arguments  << "-map" << "[wakkamix]"          // ensure audio mix goes on the pack
                << "-ac" << "2";                 // Force stereo

    // Audio switch per container format to set the best options
    if (outputFilePath.endsWith(".mp4", Qt::CaseInsensitive)) {
        // MP4 container: use AAC for audio, with higher bitrate
        arguments << "-c:a" << "aac" << "-b:a" << "320k";
    } else if (outputFilePath.endsWith(".mkv", Qt::CaseInsensitive)) {
        // MKV container: use FLAC for lossless audio
        arguments << "-c:a" << "flac";
    } else if (outputFilePath.endsWith(".avi", Qt::CaseInsensitive)) {
        // AVI container: use PCM for uncompressed audio
        arguments << "-c:a" << "pcm_s16le";
    } else if (outputFilePath.endsWith(".webm", Qt::CaseInsensitive)) {
        // WebM container: use Opus
        arguments << "-c:a" << "libopus" << "-b:a" << "128k";
    }

    arguments << outputFilePath;              // OUTPUT!
    
    // Connect signals to display output and errors
    connect(process, &QProcess::readyReadStandardOutput, [process, progressLabel, this]() {
        QByteArray outputData = process->readAllStandardOutput();
        QString output = QString::fromUtf8(outputData).trimmed();  // Convert to QString
        if (!output.isEmpty()) {
            qDebug() << "FFmpeg (stdout):" << output;
            progressLabel->setText("(FFmpeg is active)");
            logUI("FFmpeg: " + output);
        }
    });
    connect(process, &QProcess::readyReadStandardError, [process, progressLabel, totalDuration, this]() {
        QByteArray errorData = process->readAllStandardError();
        QString output = QString::fromUtf8(errorData).trimmed();  // Convert to QString
        
        if (!output.isEmpty()) {
            qDebug() << "FFmpeg (stderr):" << output;
            progressLabel->setText("(FFmpeg running)");
            logUI("FFmpeg: " + output);
            
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
            logUI("FFmpeg crashed!!");
            
            enable_playback(true);
            renderAgainButton->setVisible(true);
            QMessageBox::critical(this, "FFmpeg crashed :(", "Aborting.. Verify if FFmpeg is correctly installed and in the PATH to be executed. Verify logs for FFmpeg error. This program requires a LV2 plugin callex X42 by Gareus.");
            return;
        }
        else {
            QMessageBox::information(this, "Rendering Done!", "Prepare to preview performance. You can press RenderAgain button to adjust volume again or select a different filename, file type or resolution.");
            logUI("FFmpeg finished.");
        }

        QFile file(outputFilePath);
        if (!file.exists()) {
            qWarning() << "Output path does not exist: " << outputFilePath;
            QMessageBox::critical(this, "Check FFmpeg ?", "Aborted playback. Strange error: output file does not exist.");
            return;
        }

        // And now, for something completely different: play the rendered performance! :D

        isRecording = false; // to enable seeking on the progress bar to be great again

        enable_playback(true);
        renderAgainButton->setVisible(true);
        placeholderLabel->hide();
        videoWidget->show();

        resetMediaComponents(false);

        qDebug() << "Setting media source to" << outputFilePath;
        playVideo(outputFilePath);
        logUI("Playing mix!");
        logUI(QString("System Latency: %1 ms").arg(offset));
        logUI(QString("Cam Offset: %1 ms").arg(videoOffset));
        logUI(QString("Audio Offset: %1 ms").arg(audioOffset));

    });

    // Start the process for FFmpeg command and arguments
    process->start("ffmpeg", arguments);

    if (!process->waitForStarted()) {
        qWarning() << "Failed to start FFmpeg process.";
        progressLabel->setText("Failed to start FFmpeg");
        logUI("Failed to start FFmpeg process.");
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


double MainWindow::getMediaDuration(const QString &filePath) {
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
    return duration;
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
    progressSongFull->setPos((this->progressView->width() - progressSongFull->boundingRect().width()) / 2, 0);
    
    if (progressSong) {
        scene->removeItem(progressSong);
        delete progressSong;
        progressSong = nullptr;
    }
    // Song progress Bar
    progressSong = new QGraphicsRectItem(0, 0, 0, 20);
    progressSong->setBrush(QBrush(highlightColor));
    scene->addItem(progressSong);
    progressSong->setPos((this->progressView->width() - progressSongFull->boundingRect().width()) / 2, 0);

    // Update progress bar as media plays
    connect(player.data(), &QMediaPlayer::positionChanged, this, [=](qint64 currentPosition) {
        qreal progress = qreal(currentPosition) / (duration * 1000);
        progressSong->setRect(0, 0, 640 * progress, 20);
    });

}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {

    // Ensure the event is related to webcamScene and is a mouse press event
    if (object == webcamScene && event->type() == QEvent::GraphicsSceneMousePress) {
        QGraphicsSceneMouseEvent *mouseEvent = static_cast<QGraphicsSceneMouseEvent *>(event);

        // Check which item was clicked
        QGraphicsItem *clickedItem = webcamScene->itemAt(mouseEvent->scenePos(), QTransform());
        
        if (clickedItem == webcamPreviewItem) {  // Check if it's the webcam preview item
            QPointF clickPos = mouseEvent->scenePos();

            addVideoDisplayWidgetInDialog();

            return true; 
        }
    }

    // Check if the event is a mouse press event in a QGraphicsScene
    if (event->type() == QEvent::GraphicsSceneMousePress) {
        // Cast the event to QGraphicsSceneMouseEvent
        QGraphicsSceneMouseEvent *mouseEvent = dynamic_cast<QGraphicsSceneMouseEvent *>(event);
        if (mouseEvent) {
            
            QPointF clickPos = mouseEvent->scenePos();  // Get the mouse click position in scene coordinates
            
            if ( progressSong ) {
                if ( !isRecording && isPlayback ) {
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

                        vizPlayer->seek(newPosition, true); // Seek the media player to the clicked position

#if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
#ifdef __linux__
                    // Avoid breaking sound when seeking (~Qt6.4 bug? on Linux only..)
                        player->pause(); // Pause for a smooth workaround
                        player->setAudioOutput(nullptr); // first, detach the audio output 
                        player->setAudioOutput(audioOutput.data()); // now gimme back my sound mon
#endif
#endif
                        vizPlayer->play();

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

    qint64 lastPos = player->position();
    vizPlayer->stop(); // stop to prevent "Unexpected FFmpeg behaviour"

    QString directory = QFileDialog::getExistingDirectory(this, "Choose Directory to Save Video");
    if (directory.isEmpty()) {
        if ( isPlayback )
            QTimer::singleShot(500, this, [this, lastPos]() {
                vizPlayer->seek(lastPos, true);
                #if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
                #ifdef __linux__
                        player->setAudioOutput(nullptr); // first, detach the audio output 
                        player->setAudioOutput(audioOutput.data()); // now gimme back my sound mon
                #endif
                #endif
                vizPlayer->play();
            }); // resume play

        return;
    }

    fetchButton->setEnabled(false);
    downloadStatusLabel->setText("Downloading...");
    QProcess *process = new QProcess(this);
    
    // Prepare the command for yt-dlp
    QStringList arguments;
    arguments << "--output" << (directory + "/%(title)s.%(ext)s")
              << url;

    connect(process, &QProcess::finished, [this, directory, process, lastPos]() {
        downloadStatusLabel->setText("Download complete.");
        process->deleteLater();

        // Open the choose video dialog in the directory chosen to save video
        QString fetchVideoPath = QFileDialog::getOpenFileName(this, "Choose the downloaded playback or any another", directory, "Videos (*.mp4 *.mkv *.webm *.avi)");
        if (!fetchVideoPath.isEmpty()) {

            resetMediaComponents(false);

            placeholderLabel->hide();
            videoWidget->show();
            
            currentVideoFile = fetchVideoPath;  // Store the video playback file path
            if ( player && vizPlayer ) {
                vizPlayer->stop();
                playbackTimer->stop();
                
                playVideo(currentVideoFile);     

                downloadStatusLabel->setText("Download YouTube URL");
                singButton->setEnabled(true); 
                singAction->setEnabled(true);
                logUI("Previewing playback. Press SING to start recording.");
                fetchButton->setEnabled(true);
            }
            
        } else {
            if ( isPlayback )
                QTimer::singleShot(500, this, [this, lastPos]() {
                    vizPlayer->seek(lastPos, true);
                    #if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
                    #ifdef __linux__
                            player->setAudioOutput(nullptr); // first, detach the audio output 
                            player->setAudioOutput(audioOutput.data()); // now gimme back my sound mon
                    #endif
                    #endif
                    vizPlayer->play();
                    fetchButton->setEnabled(true);
                }); // resume play
        }
    });

    // Connect signals to handle standard output
    connect(process, &QProcess::readyReadStandardOutput, [this, process]() {
        QByteArray outputData = process->readAllStandardOutput();
        QString output = QString::fromUtf8(outputData).trimmed();  // Convert to QString
        if (!output.isEmpty()) {
            qDebug() << "yt-dlp:" << output;
            logUI("yt-dlp: " + output);
        }
    });

    // Connect signals to handle standard error
    connect(process, &QProcess::readyReadStandardError, [this, process]() {
        QByteArray errorData = process->readAllStandardError();
        QString error = QString::fromUtf8(errorData).trimmed();  // Convert to QString
        if (!error.isEmpty()) {
            qDebug() << "yt-dlp Error:" << error;
            logUI("yt-dlp: " + error);
        }
    });

    process->start("yt-dlp", arguments);

}

void MainWindow::onPreviewCheckboxToggled(bool enable) {

    if (enable) {
        qDebug() << "Camera preview will be enabled.";
        webcamView->show();
        if ( camera->isAvailable() && !camera->isActive() )
            camera->start();
    } else {

        if ( webcamDialog )
            webcamDialog->close();

        webcamView->hide();
        qDebug() << "Camera preview hidden.";
    }

}

void MainWindow::onVizCheckboxToggled(bool enable) {

    if (enable) {
        qDebug() << "Audio viz enabled.";
        vizPlayer->mute(false);
    } else {
        vizPlayer->mute(true);
        qDebug() << "Audio viz muted";  
    }

}

void MainWindow::addVideoDisplayWidgetInDialog() {
    if (webcamDialog && webcamDialog->isVisible()) {
        webcamDialog->raise();
        webcamDialog->activateWindow();
        return;
    }

    // workaround for bug in Qt6.4 that freezes videoWidget
    videoWidget->hide();
    placeholderLabel->show();

    // Create and configure the dialog
    webcamDialog = new QDialog(this);
    webcamDialog->setWindowTitle("WakkaQt - Webcam Preview");
    webcamDialog->setFixedSize(960, 544); 
    webcamPreviewLayout->removeWidget(webcamView);

    // Add webcamView to the dialog's layout
    QVBoxLayout *layout = new QVBoxLayout(webcamDialog);
    layout->addWidget(webcamView);
    webcamDialog->setLayout(layout);

    // Set the size of the view and the preview item
    webcamView->setFixedSize(960, 544);
    webcamPreviewItem->setSize(QSizeF(960, 544));

    // Set the scene size based on the view size
    webcamView->scene()->setSceneRect(0, 0, webcamView->width(), webcamView->height());

    // Center the webcamPreviewItem within the scene
    qreal xCenter = (webcamView->scene()->width() - webcamPreviewItem->boundingRect().width()) / 2;
    qreal yCenter = (webcamView->scene()->height() - webcamPreviewItem->boundingRect().height()) / 2;
    webcamPreviewItem->setPos(xCenter, yCenter);

    // Restore original layout and size when the dialog is closed
    connect(webcamDialog, &QDialog::finished, this, [this, layout]() {
        webcamDialog = nullptr;
        if (webcamPreviewLayout) {
            // Put back the view in its original place
            layout->removeWidget(webcamView);
            webcamPreviewLayout->addWidget(webcamView);
            webcamView->setFixedSize(196, 88);  // Set the original size
            webcamPreviewItem->setSize(QSizeF(196, 88));
            webcamPreviewItem->setPos(0, 0);  // Reset position to (0, 0)
            webcamView->scene()->setSceneRect(webcamView->rect());
        }
        
        if ( isPlayback ) {
            placeholderLabel->hide();
            videoWidget->show();
        }
    });

    // Show the dialog
    webcamDialog->show();
}



MainWindow::~MainWindow() {
    
     if (isRecording) {
        mediaRecorder->stop();
        audioRecorder->stopRecording();
        camera->stop();
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

}

// Function to disconnect all signals
void MainWindow::disconnectAllSignals() {

    if ( audioRecorder )
        disconnect(audioRecorder.data(), &AudioRecorder::deviceLabelChanged, this, &MainWindow::updateDeviceLabel);

    // Disconnect media signals
    if (mediaRecorder) {
        disconnect(mediaRecorder.data(), &QMediaRecorder::durationChanged, this, &MainWindow::onRecorderDurationChanged);
        disconnect(mediaRecorder.data(), &QMediaRecorder::recorderStateChanged, this, &MainWindow::onRecorderStateChanged);
        disconnect(mediaRecorder.data(), &QMediaRecorder::errorOccurred, this, &MainWindow::handleRecorderError);
    }

    if (player) {
        disconnect(player.data(), &QMediaPlayer::mediaStatusChanged, this, &MainWindow::onPlayerMediaStatusChanged);
        disconnect(player.data(), &QMediaPlayer::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);
        disconnect(player.data(), &QMediaPlayer::positionChanged, this, &MainWindow::onPlayerPositionChanged);
    }

}

void MainWindow::setBanner(const QString &msg) {
    banner->setText(msg);
}

void MainWindow::logUI(const QString &msg) {
    this->logTextEdit->append(msg);
}

void MainWindow::enable_playback(bool flag) {

    chooseVideoButton->setEnabled(flag);
    loadPlaybackAction->setEnabled(flag);
    fetchButton->setEnabled(flag);

}

void MainWindow::resizeEvent(QResizeEvent* event) {

    QMainWindow::resizeEvent(event);

    // Update the scene size to match the view size
    QRectF sceneRect = progressView->rect(); 
    progressView->scene()->setSceneRect(sceneRect); 
    qreal sceneWidth = progressView->scene()->width();
    
    if ( this->progressSongFull ) {
        progressSongFull->setX((sceneWidth - progressSongFull->boundingRect().width()) / 2);
    }
    if ( this->progressSong ) {
        progressSong->setX((sceneWidth - progressSongFull->boundingRect().width()) / 2);
    }    
    
    durationTextItem->setTextWidth(durationTextItem->boundingRect().width());
    durationTextItem->setX((this->progressView->width() - durationTextItem->boundingRect().width()) / 2);
    
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Here we would do something before closing window;
    // Now allow the window to close
    event->accept(); // Call the base class implementation
}