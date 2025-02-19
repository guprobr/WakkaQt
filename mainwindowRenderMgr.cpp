
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
    progressSongFull->setToolTip("Nothing to seek");
    setBanner("Let's preview performance and render!");
    // choose where to save rendered file
    outputFilePath = QFileDialog::getSaveFileName(this, "Mix destination (default .MP4)", "", "Video or Audio Files (*.mp4 *.mkv *.webm *.avi *.mp3 *.flac *.wav *.opus)");
    if (!outputFilePath.isEmpty()) {
        // Check if the file path has a valid extension
        QStringList allowedExtensions = QStringList() << "mp4" << "mkv" << "webm" << "avi" << "mp3" << "flac" << "wav" << "opus";
        QString selectedExtension = QFileInfo(outputFilePath).suffix().toLower(); 

        if (!allowedExtensions.contains(selectedExtension)) {
            QMessageBox::warning(this, "Invalid File Extension", "Please choose a file with one of the following extensions:\n.mp4, .mkv, .webm, .avi, .mp3, .flac, .wav, .opus");
            // Go back to the save destination dialog
            return renderAgain();
        }

        // High resolution or fast render?
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
    } else {
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        singButton->setEnabled(false);
        singAction->setEnabled(false);
        QMessageBox::warning(this, "Performance cancelled", "Performance cancelled!");
    }
}

void MainWindow::mixAndRender(double vocalVolume, qint64 manualOffset) {
    
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
    QString offsetFilter;

    if (videoOffset + manualOffset < 0) {
        offsetFilter = QString("delay=delay=%1|%1").arg(-videoOffset + manualOffset);
    } else {
        offsetFilter = QString("trim=%1ms").arg(videoOffset + manualOffset);
    }

    // Get input durations using ffprobe
    double videoDuration = getMediaDuration(currentVideoFile);
    double webcamDuration = getMediaDuration(webcamRecorded);
    double audioDuration = getMediaDuration(tunedRecorded);

    // Determine the longest duration to use for padding
    double maxDuration = std::max({videoDuration, webcamDuration, audioDuration});
    double webcamPadding = maxDuration - webcamDuration;
    double videoPadding = maxDuration - videoDuration;

    // Construct filters (Correct Order of Filters)
    QString webcamScale = QString("scale=s=%1").arg(setRez);
    QString videoScale = "";

    // Calculate Picture-in-Picture size (25% of the main video)
    QStringList mainResParts = setRez.split("x");
    int pipWidth = mainResParts[0].toInt() / 4;  // 25% width
    int pipHeight = mainResParts[1].toInt() / 4; // 25% height
    QString pipSize = QString("%1x%2").arg(pipWidth).arg(pipHeight);
    videoScale = QString("scale=s=%1").arg(pipSize);

    QString webcamFilter = QString("[1:v]%1,setpts=PTS-STARTPTS,").arg(offsetFilter);
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
        offsetFilter = QString("adelay=%1|%1").arg(-manualOffset);
    } else {
        offsetFilter = QString("atrim=%1ms").arg(manualOffset); //atrim needs start and end
    }

    arguments << "-y"
            << "-i" << tunedRecorded
            << "-i" << webcamRecorded
            << "-i" << currentVideoFile
            << "-filter_complex"
            << QString("[0:a]%1,volume=%2,%3,asetpts=PTS-STARTPTS[vocals];"
                        "[2:a][vocals]amix=inputs=2:normalize=0,aresample=async=1[wakkamix];%4")
                    .arg(offsetFilter)
                    .arg(vocalVolume)
                    .arg(_filterTreble)
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