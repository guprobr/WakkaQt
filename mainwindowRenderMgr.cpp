
#include "mainwindow.h"
#include "complexes.h"

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

    // Cache durations — each call spawns an ffprobe subprocess
    double videoDuration  = getMediaDuration(currentVideoFile);
    int totalDuration     = static_cast<int>(videoDuration);
    int recordingDuration = static_cast<int>(getMediaDuration(webcamRecorded));
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
    QString offsetFilter;
/*
    if (manualOffset < 0) {
        offsetFilter = QString("setpts=PTS+%1/TB,").arg((-videoOffset - manualOffset) / 1000.0);
    } else {
        offsetFilter = QString("trim=start=%1, setpts=PTS-STARTPTS,").arg((videoOffset + manualOffset) / 1000.0);
    } */

    // Get input durations using ffprobe — videoDuration already cached above
    double webcamDuration = getMediaDuration(webcamRecorded);
    double audioDuration  = getMediaDuration(tunedRecorded);

    // Determine the longest duration to use for padding
    double maxDuration = std::max({videoDuration, webcamDuration, audioDuration});
    double webcamPadding = maxDuration - webcamDuration;
    double videoPadding = maxDuration - videoDuration;

    // Construct filters (Correct Order of Filters)
    QString webcamScale = QString("scale=s=%1").arg(setRez);
    QString videoScale = "";

    // Calculate Picture-in-Picture size (25% of the main video)
    QStringList mainResParts = setRez.split("x");
    int pipWidth = mainResParts[0].toInt() * 0.35;  // 25% width
    int pipHeight = mainResParts[1].toInt() * 0.35; // 25% height
    QString pipSize = QString("%1x%2").arg(pipWidth).arg(pipHeight);
    videoScale = QString("scale=s=%1").arg(pipSize);

    QString webcamFilter = QString("[1:v]%1").arg(offsetFilter);
    webcamFilter += webcamScale;
    if (webcamPadding > 0) {
        webcamFilter += QString(",pad=width=%1:height=%2:x=(ow-iw)/2:y=(oh-ih)/2:color=black").arg(mainResParts[0]).arg(mainResParts[1]);
    }
    webcamFilter += "[webcam];";

    QString videoFilter = videoScale;
    if (videoPadding > 0) {
        videoFilter += QString(",pad=width=%1:height=%2:x=(ow-iw)/2:y=(oh-ih)/2:color=black").arg(pipWidth).arg(pipHeight);
    }
    videoFilter += "[video];";

    if ((outputFilePath.endsWith(".mp4", Qt::CaseInsensitive) ||
        outputFilePath.endsWith(".avi", Qt::CaseInsensitive) ||
        outputFilePath.endsWith(".mkv", Qt::CaseInsensitive) ||
        outputFilePath.endsWith(".webm", Qt::CaseInsensitive))) {

        if (!(currentVideoFile.endsWith("mp3", Qt::CaseInsensitive) ||
            currentVideoFile.endsWith("wav", Qt::CaseInsensitive) ||
            currentVideoFile.endsWith("opus", Qt::CaseInsensitive) ||
            currentVideoFile.endsWith("flac", Qt::CaseInsensitive))) {

            // Combine both recorded and playback videos using overlay (Picture-in-Picture)
            int x = mainResParts[0].toInt() - pipWidth;   // X position (top right)
            int y = 0;                                   // Y position (top right)

            videorama = webcamFilter + videoFilter + QString("[webcam][video]overlay=x=%1:y=%2[videorama];").arg(x).arg(y);
        } else {
            // Output is VIDEO but input playback is AUDIO only. Create a blank video.
            QStringList partsRez = setRez.split("x");
            int width = partsRez[0].toInt();
            int height = partsRez[1].toInt();

            // Create a black video of the required duration
            videorama = QString("color=c=black:s=%1:d=%2:r=30[black];[1:v]setpts=PTS-STARTPTS,scale=s=%3[webcam];[black][webcam]overlay=0:0[videorama];").arg(QString::number(width) + "x" + QString::number(height)).arg(maxDuration).arg(setRez);
        }
    }
    
    if (manualOffset < 0) {
        offsetFilter = QString("adelay=%1|%1").arg((-1*manualOffset));
    } else {
        offsetFilter = QString("atrim=start=%1, asetpts=PTS-STARTPTS").arg((manualOffset) / 1000.0);
    }


    arguments << "-y"
            << "-i" << tunedRecorded
            << "-ss" << QString("%1ms").arg(videoOffset + manualOffset) << "-i" << webcamRecorded
            << "-i" << currentVideoFile
            << "-filter_complex"
            << QString("[0:a]%1,volume=%2,%3[vocals];[2:a][vocals]amix=inputs=2:normalize=0,aresample=async=1[wakkamix];%4")
                    .arg(offsetFilter)
                    .arg(vocalVolume)
                    .arg(_audioMasterization)
                    .arg(videorama)
            << "-map" << "[wakkamix]";

    if (!videorama.isEmpty()) {
        arguments << "-map" << "[videorama]";
    }

    arguments << outputFilePath;
    
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
        logUI("Failed to start FFmpeg process.");
        // Remove and free the progress widgets that would otherwise stay in
        // the layout permanently (the finished-lambda won't fire if we never started)
        layout->removeWidget(this->progressBar);
        layout->removeWidget(progressLabel);
        delete this->progressBar;
        this->progressBar = nullptr;
        progressLabel->deleteLater();
        enable_playback(true);
        renderAgainButton->setVisible(true);
        process->deleteLater();
        QMessageBox::critical(this, "FFmpeg not found",
            "Failed to start FFmpeg. Verify it is installed and available in PATH.");
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