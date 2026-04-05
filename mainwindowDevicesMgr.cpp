#include "mainwindow.h"

void MainWindow::resetMediaComponents(bool isStarting)
{
    qDebug() << "Resetting media components";

    isRecording = false;
    playbackTimer->stop();

    if (!isStarting)
        disconnectAllSignals();

    // Stop activity first.
    if (vizPlayer)
        vizPlayer->stop();

    if (player) {
        player->stop();
        player->setSource(QUrl());
        player->setVideoOutput(nullptr);
        player->setAudioOutput(nullptr);
    }

    if (mediaRecorder) {
        if (mediaRecorder->recorderState() != QMediaRecorder::StoppedState)
            mediaRecorder->stop();
    }

    if (camera) {
        if (camera->isActive())
            camera->stop();
    }

    if (audioRecorder)
        audioRecorder->stopRecording();

    // Detach the capture graph before destroying any of its nodes.
    if (mediaCaptureSession) {
        mediaCaptureSession->setRecorder(nullptr);
        mediaCaptureSession->setCamera(nullptr);
        mediaCaptureSession->setAudioInput(nullptr);
        mediaCaptureSession->setVideoOutput(nullptr);
    }

    // Destroy helper objects first.
    if (vizPlayer)
        vizPlayer.reset();

    // Destroy the multimedia graph after it has been detached.
    if (mediaCaptureSession)
        mediaCaptureSession.reset();

    if (mediaRecorder)
        mediaRecorder.reset();

    if (camera)
        camera.reset();

    if (format)
        format.reset();

    if (player)
        player.reset();

    if (audioOutput)
        audioOutput.reset();

    if (audioRecorder)
        audioRecorder.reset();

    // Recreate all objects here, since configureMediaComponents()
    // now only configures existing instances.
    audioRecorder.reset(new AudioRecorder(selectedDevice, this));
    player.reset(new QMediaPlayer(this));
    audioOutput.reset(new QAudioOutput(this));
    format.reset(new QMediaFormat);
    camera.reset(new QCamera(selectedCameraDevice, this));
    mediaRecorder.reset(new QMediaRecorder(this));
    mediaCaptureSession.reset(new QMediaCaptureSession(this));
    vizPlayer.reset(new AudioVizMediaPlayer(player.data(), vizUpperLeft, vizUpperRight, this));

    configureMediaComponents();
}

void MainWindow::configureMediaComponents()
{
    qDebug() << "Reconfiguring media components";

    if (!audioRecorder || !player || !audioOutput || !format || !camera || !mediaRecorder || !mediaCaptureSession || !vizPlayer) {
        qWarning() << "configureMediaComponents called with missing media objects";
        return;
    }

    connect(audioRecorder.data(),
            &AudioRecorder::deviceLabelChanged,
            this,
            &MainWindow::updateDeviceLabel,
            Qt::UniqueConnection);

    audioRecorder->initialize();

    // Setup media player.
    player->setVideoOutput(videoWidget);
    player->setAudioOutput(audioOutput.data());

    onVizCheckboxToggled(vizCheckbox->isChecked());

    // Setup sound level widget.
    soundLevelWidget->setInputDevice(selectedDevice);

    // Rebind the current camera device explicitly.
    camera->setCameraDevice(selectedCameraDevice);

    // Prefer a JPEG camera input format when available.
    QCameraFormat preferredCameraFormat;
    bool foundPreferredCameraFormat = false;
    bool foundPreferredCameraFormatAt30fps = false;
    qint64 bestPixels = -1;
    float bestMaxFps = -1.0f;

    const auto cameraFormats = selectedCameraDevice.videoFormats();
    for (const QCameraFormat &cameraFormat : cameraFormats) {
        if (cameraFormat.pixelFormat() != QVideoFrameFormat::Format_Jpeg)
            continue;

        const QSize resolution = cameraFormat.resolution();
        const qint64 pixels = qint64(resolution.width()) * qint64(resolution.height());
        const bool supports30fps = cameraFormat.maxFrameRate() >= 30.0f;

        if (!foundPreferredCameraFormat
            || (supports30fps && !foundPreferredCameraFormatAt30fps)
            || (supports30fps == foundPreferredCameraFormatAt30fps && pixels > bestPixels)
            || (supports30fps == foundPreferredCameraFormatAt30fps
                && pixels == bestPixels
                && cameraFormat.maxFrameRate() > bestMaxFps)) {
            preferredCameraFormat = cameraFormat;
            foundPreferredCameraFormat = true;
            foundPreferredCameraFormatAt30fps = supports30fps;
            bestPixels = pixels;
            bestMaxFps = cameraFormat.maxFrameRate();
        }
    }

    qreal targetFrameRate = 30.0;

    if (foundPreferredCameraFormat) {
        camera->setCameraFormat(preferredCameraFormat);

        if (preferredCameraFormat.maxFrameRate() > 0.0f && preferredCameraFormat.maxFrameRate() < targetFrameRate)
            targetFrameRate = preferredCameraFormat.maxFrameRate();

        mediaRecorder->setVideoResolution(preferredCameraFormat.resolution());

        qDebug() << "Selected camera input format:"
                 << "resolution=" << preferredCameraFormat.resolution()
                 << "fps=" << preferredCameraFormat.minFrameRate() << ".." << preferredCameraFormat.maxFrameRate()
                 << "pixelFormat=JPEG";
    } else {
        qWarning() << "No JPEG camera format found; keeping backend/default camera format";
    }

    // This recorder branch is video-only.
    using FF = QMediaFormat::FileFormat;
    using VC = QMediaFormat::VideoCodec;
    using AC = QMediaFormat::AudioCodec;

    const QList<FF> filePreference = {
        FF::Matroska,
        FF::AVI,
        FF::QuickTime,
        FF::MPEG4,
        FF::WebM
    };

    const QList<VC> videoPreference = {
        VC::MotionJPEG,
        VC::H264,
        VC::H265,
        VC::VP9,
        VC::VP8,
        VC::AV1,
        VC::MPEG4,
        VC::MPEG2,
        VC::MPEG1,
        VC::WMV,
        VC::Theora
    };

    bool selectedRecorderFormat = false;

    for (FF fileFormatCandidate : filePreference) {
        QMediaFormat probe;
        probe.setFileFormat(fileFormatCandidate);
        probe.setAudioCodec(AC::Unspecified);

        const auto supportedVideos = probe.supportedVideoCodecs(QMediaFormat::Encode);
        if (supportedVideos.isEmpty())
            continue;

        for (VC videoCodecCandidate : videoPreference) {
            if (!supportedVideos.contains(videoCodecCandidate))
                continue;

            QMediaFormat candidate;
            candidate.setFileFormat(fileFormatCandidate);
            candidate.setAudioCodec(AC::Unspecified);
            candidate.setVideoCodec(videoCodecCandidate);

            if (!candidate.isSupported(QMediaFormat::Encode))
                continue;

            *format = candidate;
            selectedRecorderFormat = true;
            break;
        }

        if (selectedRecorderFormat)
            break;
    }

    if (!selectedRecorderFormat) {
        qWarning() << "Preferred recorder formats are not supported; resolving via backend";

        format->setFileFormat(FF::Matroska);
        format->setAudioCodec(AC::Unspecified);
        format->setVideoCodec(VC::Unspecified);
        format->resolveForEncoding(QMediaFormat::RequiresVideo);
    }

    qDebug() << "Selected recorder media format:"
             << "container=" << QMediaFormat::fileFormatName(format->fileFormat())
             << "videoCodec=" << QMediaFormat::videoCodecName(format->videoCodec())
             << "audioCodec=" << QMediaFormat::audioCodecName(format->audioCodec());

    mediaRecorder->setMediaFormat(*format);
    mediaRecorder->setOutputLocation(QUrl::fromLocalFile(webcamRecorded));
    mediaRecorder->setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
    mediaRecorder->setQuality(QMediaRecorder::HighQuality);
    mediaRecorder->setVideoFrameRate(targetFrameRate);
    mediaRecorder->setVideoBitRate(8'000'000);

    qDebug() << "Configuring mediaCaptureSession..";
    mediaCaptureSession->setVideoOutput(webcamPreviewItem);
    mediaCaptureSession->setCamera(camera.data());
    mediaCaptureSession->setAudioInput(nullptr);
    mediaCaptureSession->setRecorder(mediaRecorder.data());

    connect(mediaRecorder.data(),
            &QMediaRecorder::recorderStateChanged,
            this,
            &MainWindow::onRecorderStateChanged,
            Qt::UniqueConnection);
    connect(mediaRecorder.data(),
            &QMediaRecorder::errorOccurred,
            this,
            &MainWindow::handleRecorderError,
            Qt::UniqueConnection);
    connect(player.data(),
            &QMediaPlayer::mediaStatusChanged,
            this,
            &MainWindow::onPlayerMediaStatusChanged,
            Qt::UniqueConnection);
    connect(player.data(),
            &QMediaPlayer::playbackStateChanged,
            this,
            &MainWindow::onPlaybackStateChanged,
            Qt::UniqueConnection);

    camera->start();

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
        exit(-1);
        return;
    }

    // Create a dialog to show the lists of devices
    QDialog *deviceDialog = new QDialog(this);
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

    // Connect the exit button to close the program cleanly
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