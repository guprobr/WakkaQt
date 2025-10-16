#include "mainwindow.h"
#include "sndwidget.h"

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
        aboutBox.setFont(QFont("Arial", 11));

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
    durationTextItem->setFont(QFont("Courier", 10, QFont::Bold));
    durationTextItem->setPlainText("00:00:00 / 00:00:00");

    progressScene->addItem(durationTextItem);
    progressScene->setSceneRect(0, 0, viewWidth, viewHeight);

    banner = new QLabel(Wakka_welcome, this);
    banner->setTextFormat(Qt::TextFormat::RichText);
    banner->setFont(QFont("Arial", 10, QFont::Bold));
    banner->setAlignment(Qt::AlignCenter);
    banner->setToolTip("Here be the song title!");
    setBanner(Wakka_welcome);

    // Create buttons
    QPushButton *exitButton = new QPushButton("Exit", this);
    chooseVideoButton = new QPushButton("Load playback from disk", this);
    chooseVideoButton->setToolTip("Load media files from disk");
    singButton = new QPushButton("♪ SING ♪", this);
    singButton->setFont(QFont("Arial", 15));
    singButton->setToolTip("Start/Stop recording");
    abortButton = new QPushButton("* A B O R T *", this);
    abortButton->setFont(QFont("Arial", 15));
    abortButton->setToolTip("TRASH recording");
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
    vizCheckbox->setToolTip("Toggle Audio Visualizer");
    vizCheckbox->setChecked(true);
    
    QHBoxLayout *indicatorLayout = new QHBoxLayout();
    indicatorLayout->addStretch();
    indicatorLayout->addWidget(recordingIndicator, 0, Qt::AlignCenter);
    indicatorLayout->addWidget(vizCheckbox, 0, Qt::AlignRight);
    indicatorLayout->addWidget(previewCheckbox, 0, Qt::AlignRight);
    //indicatorLayout->addStretch();

    // Instantiate SndWidget
    soundLevelWidget = new SndWidget(this);
    soundLevelWidget->setMinimumSize(640, 64);
    soundLevelWidget->setMaximumHeight(84);
    soundLevelWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    soundLevelWidget->setToolTip("Sound input visualization widget");

    // Device label
    deviceLabel = new QLabel("Selected Device: None", this);
    deviceLabel->setFont(QFont("Arial", 8));
    deviceLabel->setToolTip("Changing the default source input in the system cfg will not reflect the information here");
    
    // YT downloader
    urlInput = new QLineEdit(this);
    urlInput->setPlaceholderText("https://www.youtube.com/?v=aBcDFgETc");
    urlInput->setFont(QFont("Arial", 10));
    urlInput->setToolTip("Paste a URL here and fetch your karaoke media");
    fetchButton = new QPushButton("FETCH", this);
    fetchButton->setFont(QFont("Arial", 10));
    fetchButton->setToolTip("Click here and download URL to disk");
    downloadStatusLabel = new QLabel("Download media", this);
    downloadStatusLabel->setFont(QFont("Arial", 7));
    downloadStatusLabel->setToolTip("Several URL besides YouTube will work");
    QHBoxLayout *fetchLayout = new QHBoxLayout;
    fetchLayout->addWidget(urlInput);
    fetchLayout->addWidget(fetchButton);
    fetchLayout->addWidget(downloadStatusLabel);

    // Log text
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logUI(Wakka_welcome);
    logTextEdit->setMinimumHeight(25);
    logTextEdit->setMaximumHeight(90);
    logTextEdit->setFont(QFont("Arial", 8));
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
    layout->addWidget(soundLevelWidget, 1);
    layout->addWidget(placeholderLabel);  
    layout->addWidget(videoWidget);       
    layout->addWidget(singButton);
    layout->addWidget(abortButton);
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
    abortButton->setVisible(false);
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
    connect(abortButton, &QPushButton::clicked, this, &MainWindow::abortRecording);
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

void MainWindow::addProgressSong(QGraphicsScene *scene, qint64 duration) {
    disconnect(player.data(), &QMediaPlayer::positionChanged, this, nullptr);

    // Remove and delete existing progressSongFull
    if (progressSongFull) {
        scene->removeItem(progressSongFull);
        delete progressSongFull;
        progressSongFull = nullptr;
    }

    // Centered position for progressSongFull
    qreal progressBarWidth = 640;
    qreal progressBarHeight = 10;
    qreal progressBarX = (this->progressView->width() - progressBarWidth) / 2;
    qreal progressBarY = (this->progressView->height() - progressBarHeight * 2);

    // Song progress full bar
    progressSongFull = new QGraphicsRectItem(0, 0, progressBarWidth, progressBarHeight);
    progressSongFull->setToolTip("Click to seek");
    scene->addItem(progressSongFull);
    progressSongFull->setPos(progressBarX, progressBarY);

    // Remove and delete existing progressSong
    if (progressSong) {
        scene->removeItem(progressSong);
        delete progressSong;
        progressSong = nullptr;
    }

    // Song progress bar (current progress)
    progressSong = new QGraphicsRectItem(0, 0, 0, progressBarHeight);
    progressSong->setBrush(QBrush(highlightColor));
    scene->addItem(progressSong);
    progressSong->setPos(progressBarX, progressBarY);

    // Update progress bar as media plays
    connect(player.data(), &QMediaPlayer::positionChanged, this, [=](qint64 currentPosition) {
        qreal progress = qreal(currentPosition) / (duration * 1000);
        progressSong->setRect(0, 0, progressBarWidth * progress, progressBarHeight);
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
                        if ( !( (currentPlayback.endsWith("mp3", Qt::CaseInsensitive))             \
                        ||  (currentPlayback.endsWith("wav", Qt::CaseInsensitive))             \
                        ||  (currentPlayback.endsWith("opus", Qt::CaseInsensitive))         \
                        ||  (currentPlayback.endsWith("flac", Qt::CaseInsensitive)) )) {
                            placeholderLabel->hide();
                            videoWidget->show();
                        }
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
    webcamDialog->setFixedSize(512, 400); 
    webcamPreviewLayout->removeWidget(webcamView);

    // Add webcamView to the dialog's layout
    QVBoxLayout *layout = new QVBoxLayout(webcamDialog);
    layout->addWidget(webcamView);
    webcamDialog->setLayout(layout);

    // Set the size of the view and the preview item
    webcamView->setFixedSize(480, 320);
    webcamPreviewItem->setSize(QSizeF(480, 320));

    // Set the scene size based on the view size
    webcamView->scene()->setSceneRect(0, 0, webcamView->width(), webcamView->height());

    // Center the webcamPreviewItem within the scene
    qreal xCenter = (webcamView->scene()->width() - webcamPreviewItem->boundingRect().width()) / 2;
    qreal yCenter = (webcamView->scene()->height() - webcamPreviewItem->boundingRect().height()) / 2;
    webcamPreviewItem->setPos(0, 0);

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
        
        if ( isPlayback ) 
            if ( !( (currentPlayback.endsWith("mp3", Qt::CaseInsensitive))             \
                ||  (currentPlayback.endsWith("wav", Qt::CaseInsensitive))             \
                ||  (currentPlayback.endsWith("opus", Qt::CaseInsensitive))             \
                ||  (currentPlayback.endsWith("flac", Qt::CaseInsensitive)) )) 
                {
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
    int response = QMessageBox::question(
        this, 
        "The show must go on!", 
        "Are you really really sure you want to leave?", 
        QMessageBox::Yes | QMessageBox::No, 
        QMessageBox::No
    );
    if (response == QMessageBox::Yes) // Now allow the window to close
        event->accept(); // Call the base class implementation
    else event->ignore(); // Baby come back!
}


