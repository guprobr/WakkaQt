
#include "mainwindow.h"
#include "complexes.h"
#include <QtConcurrent/QtConcurrentRun>
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif

// render //
void MainWindow::renderAgain()
{
    videoWidget->hide();
    placeholderLabel->show();
    
    vizPlayer->stop();
    playbackTimer->stop();
    
    resetMediaComponents(false);

    isPlayback = false; // to avoid seeking while rendering

    renderAgainButton->setVisible(false);
    enable_playback(false);
    if (progressSongFull)
        progressSongFull->setToolTip("Nothing to seek");
    setBanner("Let's preview performance and render!");

    // Loop until the user picks a valid output path or cancels.
    // (Previously used recursion here which could stack-overflow on repeated
    // bad-extension choices — replaced with a safe while loop.)
    const QStringList allowedExtensions = {"mp4","mkv","webm","avi","mp3","flac","wav","opus"};
    while (true) {
        outputFilePath = QFileDialog::getSaveFileName(
            this, "Mix destination (default .MP4)", "",
            "Video or Audio Files (*.mp4 *.mkv *.webm *.avi *.mp3 *.flac *.wav *.opus)");

        if (outputFilePath.isEmpty()) {
            enable_playback(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            singButton->setEnabled(false);
            singAction->setEnabled(false);
            QMessageBox::warning(this, "Performance cancelled", "Performance cancelled!");
            return;
        }

        if (!allowedExtensions.contains(QFileInfo(outputFilePath).suffix().toLower())) {
            QMessageBox::warning(this, "Invalid File Extension",
                "Please choose a file with one of the following extensions:\n"
                ".mp4, .mkv, .webm, .avi, .mp3, .flac, .wav, .opus");
            continue; // show the dialog again — no stack growth
        }

        break; // valid extension
    }

    // Save session to library BEFORE asking for resolution
    saveCurrentSession();

    int response = QMessageBox::question(
        this, 
        "Resolution", 
        "Do you want 1920x1080 high-resolution video? Low resolution 640x480 renders much faster.", 
        QMessageBox::Yes | QMessageBox::No, 
        QMessageBox::No
    );
    setRez = (response == QMessageBox::Yes) ? "1920x1080" : "640x480";
    qDebug() << "Will overlay each video with resolution:" << setRez;

    // Show the preview dialog
    previewDialog.reset(new PreviewDialog(audioOffset, offset, this));
    previewDialog->setAudioFile(audioRecorded);
    if (previewDialog->exec() == QDialog::Accepted)
    {
        double vocalVolume = previewDialog->getVolume();
        qint64 manualOffset = previewDialog->getOffset();
        previewDialog.reset();
        mixAndRender(vocalVolume, manualOffset);
    } else {
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        singButton->setEnabled(false);
        singAction->setEnabled(false);
        previewDialog.reset();
        QMessageBox::warning(this, "Performance cancelled", "Performance cancelled during volume adjustment.");
    }
}

void MainWindow::mixAndRender(double vocalVolume, qint64 manualOffset) {

    videoWidget->hide();
    placeholderLabel->show();
    singButton->setEnabled(false);
    singAction->setEnabled(false);
    chooseInputButton->setEnabled(true);
    chooseInputAction->setEnabled(true);

    progressBar = new QProgressBar(this);
    progressBar->setMinimumSize(640, 25);
    progressBar->setRange(0, 100);

    QLabel *progressLabel = new QLabel("Rendering...", this);
    progressLabel->setAlignment(Qt::AlignCenter);
    progressLabel->setFont(QFont("Arial", 8));
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    layout->insertWidget(0, progressLabel, 0, Qt::AlignCenter);
    layout->insertWidget(0, progressBar,   0, Qt::AlignCenter);

    const qint64 effectiveAudioOffset = audioOffset + manualOffset;
    const qint64 effectiveVideoOffset = videoOffset + manualOffset;

    auto onFinished = [this, progressLabel](bool success) {
        delete progressLabel;
        delete this->progressBar;

        if (!success) {
            logUI("Render failed.");
            enable_playback(true);
            renderAgainButton->setVisible(true);
            QMessageBox::critical(this, "Render Error", "Rendering failed. Check the logs.");
            return;
        }

        QFile file(outputFilePath);
        if (!file.exists()) {
            qWarning() << "Output file missing:" << outputFilePath;
            QMessageBox::critical(this, "Render Error", "Output file was not created.");
            return;
        }

        QMessageBox::information(this, "Rendering Done!",
            "Prepare to preview performance. You can press Render Again to adjust volume "
            "or select a different filename / format / resolution.");
        logUI("Rendering finished.");

        isRecording = false;
        enable_playback(true);
        renderAgainButton->setVisible(true);
        placeholderLabel->hide();
        videoWidget->show();

        resetMediaComponents(false);
        playVideo(outputFilePath);
        logUI("Playing mix!");
        logUI(QString("System Latency: %1 ms").arg(offset));
        logUI(QString("Cam Offset: %1 ms").arg(videoOffset));
        logUI(QString("Audio Offset: %1 ms").arg(audioOffset));
    };

#ifdef WAKKAQT_FFMPEG_NATIVE
    // Native render — runs in a background thread; progress bar updated via lambda
    QProgressBar *pb = this->progressBar;
    QtConcurrent::run([=]() {
        bool ok = FFmpegNative::renderVideo(
            tunedRecorded,
            webcamRecorded,
            currentVideoFile,
            outputFilePath,
            vocalVolume,
            effectiveAudioOffset,
            effectiveVideoOffset,
            setRez,
            _audioMasterization,
            [pb](double p) {
                QMetaObject::invokeMethod(pb, [pb, p]() {
                    pb->setValue(int(p * 100));
                }, Qt::QueuedConnection);
            });
        QMetaObject::invokeMethod(qApp, [=]() { onFinished(ok); }, Qt::QueuedConnection);
    });
#else
    // QProcess fallback (ffmpeg must be in PATH)
    const QString offsetFilter = (manualOffset < 0)
        ? QString("adelay=%1|%1").arg(-manualOffset)
        : QString("atrim=start=%1,asetpts=PTS-STARTPTS").arg(manualOffset / 1000.0);

    QString videorama;
    if (outputFilePath.endsWith(".mp4", Qt::CaseInsensitive) ||
        outputFilePath.endsWith(".avi", Qt::CaseInsensitive) ||
        outputFilePath.endsWith(".mkv", Qt::CaseInsensitive) ||
        outputFilePath.endsWith(".webm", Qt::CaseInsensitive))
    {
        videorama = QString("[1:v]scale=s=%1[videorama];").arg(setRez);
    }

    QStringList arguments;
    arguments << "-y"
              << "-i" << tunedRecorded
              << "-ss" << QString("%1ms").arg(effectiveVideoOffset)
              << "-i" << webcamRecorded
              << "-i" << currentVideoFile
              << "-filter_complex"
              << QString("[0:a]%1,volume=%2,%3[vocals];"
                         "[2:a][vocals]amix=inputs=2:normalize=0,aresample=async=1[wakkamix];%4")
                     .arg(offsetFilter).arg(vocalVolume)
                     .arg(_audioMasterization).arg(videorama)
              << "-map" << "[wakkamix]";
    if (!videorama.isEmpty())
        arguments << "-map" << "[videorama]";
    arguments << outputFilePath;

    int totalDuration = static_cast<int>(getMediaDuration(currentVideoFile));

    QProcess *process = new QProcess(this);
    connect(process, &QProcess::readyReadStandardError,
            [process, totalDuration, this]() {
        const QString out = QString::fromUtf8(process->readAllStandardError()).trimmed();
        if (!out.isEmpty()) updateProgress(out, this->progressBar, totalDuration);
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, process, onFinished](int exitCode, QProcess::ExitStatus exitStatus) {
        process->deleteLater();
        onFinished(exitStatus == QProcess::NormalExit && exitCode == 0);
    });

    process->start("ffmpeg", arguments);
    if (!process->waitForStarted()) {
        process->deleteLater();
        layout->removeWidget(this->progressBar);
        layout->removeWidget(progressLabel);
        delete this->progressBar;
        this->progressBar = nullptr;
        progressLabel->deleteLater();
        enable_playback(true);
        renderAgainButton->setVisible(true);
        QMessageBox::critical(this, "FFmpeg not found",
            "Failed to start FFmpeg. Verify it is installed and available in PATH.");
        return;
    }
#endif
}

QString MainWindow::millisecondsToSecondsString(qint64 milliseconds) {
    double seconds = milliseconds / 1000.0;
    return QString::number(seconds, 'f', 3); 
}

void MainWindow::updateProgress(const QString& output, QProgressBar* progressBar, int totalDuration) {
    QRegularExpression timeRegex("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
    QRegularExpressionMatch match = timeRegex.match(output);

    if (match.hasMatch()) {
        bool ok1, ok2, ok3, ok4;
        int hours       = match.captured(1).toInt(&ok1);
        int minutes     = match.captured(2).toInt(&ok2);
        int seconds     = match.captured(3).toInt(&ok3);
        int centiseconds = match.captured(4).toInt(&ok4);

        if (!ok1 || !ok2 || !ok3 || !ok4) {
            qWarning() << "Error parsing FFmpeg time components";
            return;
        }

        // Convert time to milliseconds (FFmpeg outputs centiseconds in the .xx field)
        int elapsedMilliseconds = (hours * 3600 + minutes * 60 + seconds) * 1000
                                  + centiseconds * 10;

        int totalDurationMilliseconds = totalDuration * 1000;
        if (totalDurationMilliseconds <= 0) {
            qWarning() << "Total duration is not valid";
            return;
        }

        int progressValue = static_cast<int>(100.0 * elapsedMilliseconds / totalDurationMilliseconds);
        progressBar->setValue(qBound(0, progressValue, 100));
    }
}


double MainWindow::getMediaDuration(const QString &filePath) {
#ifdef WAKKAQT_FFMPEG_NATIVE
    double duration = FFmpegNative::getDuration(filePath);
    if (duration <= 0)
        qWarning() << "FFmpegNative::getDuration returned 0 for" << filePath;
    else
        qDebug() << "Media duration:" << int(duration) << "seconds";
    return duration;
#else
    QProcess ffprobeProcess;
    ffprobeProcess.start("ffprobe", QStringList() << "-v" << "error" << "-show_entries"
                         << "format=duration" << "-of" << "default=noprint_wrappers=1:nokey=1"
                         << filePath);
    ffprobeProcess.waitForFinished();
    QString durationStr = QString::fromUtf8(ffprobeProcess.readAllStandardOutput()).trimmed();
    bool ok;
    double duration = durationStr.toDouble(&ok);
    if (!ok || duration <= 0) {
        qWarning() << "Failed to get duration:" << durationStr;
        return 0;
    }
    qDebug() << "Media duration:" << int(duration) << "seconds";
    return duration;
#endif
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