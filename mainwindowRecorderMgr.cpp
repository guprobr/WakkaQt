#include "mainwindow.h"

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
        setBanner(".. .Finishing VIDEO.. .");
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
        singButton->setEnabled(false);
        singAction->setText("SING");
        singAction->setEnabled(false);
        vizCheckbox->setEnabled(true);
        progressSongFull->setToolTip("Nothing to seek");
        
        videoWidget->hide();
        placeholderLabel->show();

        QFile fileAudio(audioRecorded);
        QFile fileCam(webcamRecorded);
        if (fileAudio.size() > 0 && fileCam.size() > 0 ) {

            waitForFileFinalization(webcamRecorded, [this]() {
                // Now video is ready, proceed safely
                qWarning() << "VIDEO is ready. Proceeding...";
            
                // DETERMINE audioOffset
                qint64 recDuration = 1000 * getMediaDuration(audioRecorded);
                audioOffset = offset + recDuration - pos;
                // DETERMINE videoOffset
                recDuration = 1000 * getMediaDuration(webcamRecorded);
                videoOffset = offset + recDuration - pos;

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
                            qWarning() << "Failed to remove existing file:" << destinationFilePath;
                            return;
                        }
                    }

                    // Attempt to copy the file
                    if (QFile::copy(sourceFilePath, destinationFilePath)) {
                        qDebug() << "File copied successfully to" << destinationFilePath;
                    } else {
                        qWarning() << "Failed to copy file to" << destinationFilePath;
                    }
                } else {
                    qWarning() << "Source file does not exist:" << sourceFilePath;
                }

                qWarning() << "Recording saved successfully";
                setBanner("Recording saved successfully!");
                renderAgain();

            });

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

void MainWindow::waitForFileFinalization(const QString &filePath, std::function<void()> callback) {
    QTimer *timer = new QTimer(this);
    int attempts = 0;
    bool fileIsValid = false;

    connect(timer, &QTimer::timeout, this, [this, filePath, timer, callback, attempts, fileIsValid]() mutable {
        
        attempts++;

        if (attempts > 30) {
            qWarning() << "Timeout reached. After " << attempts << "  check attempts it did not finalize properly.";
            timer->stop();
            timer->deleteLater();
            QMessageBox::critical(this, "Recorder Error", "Timeout reached. Video did not finalize properly.");
            return;
        }

        // Probe with ffprobe to check if the file is a valid MP4
        QString command = "ffprobe";
        QStringList arguments;
        arguments << "-v" << "error"
                  << "-select_streams" << "v:0"
                  << "-show_entries" << "stream=codec_type"
                  << "-of" << "default=noprint_wrappers=1:nokey=1"
                  << filePath;

        QProcess *process = new QProcess(this);
        process->start(command, arguments);

        process->waitForFinished();

        QByteArray output = process->readAllStandardOutput();
        process->deleteLater();

        // If ffprobe output indicates the file is a valid video stream
        if (!output.isEmpty() && output.trimmed() == "video") {
            qDebug() << "File is a valid MP4 video.";
            fileIsValid = true;
            timer->stop();
            timer->deleteLater();
            callback();  // Proceed with render logic
        } else {
            qDebug() << "File is not finalized yet or invalid MP4.";
        }
    });

    timer->start(1000); // Check every second
   
}