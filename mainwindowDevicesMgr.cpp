#include "mainwindow.h"

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
    format->setVideoCodec(QMediaFormat::VideoCodec::MPEG4);
    format->setAudioCodec(QMediaFormat::AudioCodec::AAC);

    // Setup Media recorder
    mediaRecorder->setMediaFormat(*format);
    mediaRecorder->setOutputLocation(QUrl::fromLocalFile(webcamRecorded));
    mediaRecorder->setQuality(QMediaRecorder::VeryHighQuality);
    //mediaRecorder->setVideoFrameRate(30);
    //mediaRecorder->setVideoBitRate(5000000);
    
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