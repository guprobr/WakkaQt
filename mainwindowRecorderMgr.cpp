#include "mainwindow.h"
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif


void MainWindow::abortRecording() {
    isAborting = true;
    qWarning() << "Stop recording.";
    stopRecording();
    return;
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
        // Fixes the webcam-having-ness of *this* recording at the moment it
        // starts (the device won't change mid-recording), so later stages
        // (stopRecording()/renderAgain()/mixAndRender()) judge this specific
        // session instead of whatever camera happens to be attached by the
        // time it's rendered.
        recordingHasWebcam = hasCamera;

        if (audioRecorder && player && vizPlayer) {

            connect(player.data(), &QMediaPlayer::positionChanged, this, &MainWindow::onPlayerPositionChanged);

            if (hasCamera && camera && mediaRecorder) {
                connect(mediaRecorder.data(), &QMediaRecorder::durationChanged, this, &MainWindow::onRecorderDurationChanged);
                camera->start(); // prep camera first
            }

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
            if (hasCamera && mediaRecorder) {
                mediaRecorder->record(); // start recording video
            } else {
                // No camera: mediaRecorder::recorderStateChanged (connected
                // only in the hasCamera branch of configureMediaComponents())
                // never fires to drive the "recording started" UI update, so
                // trigger it directly here.
                onRecorderStateChanged(QMediaRecorder::RecordingState);
            }

            player->play(); // start the show

        } else {
            qWarning() << "Failed to initialize audio recorder or player.";
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
        abortButton->setVisible(true);

        pitchMonitor->reset();

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
        //webcamView->hide();
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
        abortButton->setVisible(false);
        singAction->setText("SING");
        singAction->setEnabled(false);
        vizCheckbox->setEnabled(true);
        if (progressSongFull)
            progressSongFull->setToolTip("Nothing to seek");
        
        videoWidget->hide();
        placeholderLabel->show();

        if ( isAborting ) {
            isAborting = false;
            handleRecordingError();
            //resetMediaComponents(false);
            return;
        }

        QFile fileAudio(audioRecorded);
        QFile fileCam(webcamRecorded);
        if (fileAudio.size() > 0 && (!recordingHasWebcam || fileCam.size() > 0)) {

            setBanner("Finalizing recording, please wait...");
            auto finalizeRecording = [this]() {
                // Now video (if any) is ready, proceed safely
                qWarning() << "Proceeding with finalization...";

                // DETERMINE audioOffset
                // Pre-roll = how long the recording ran before the song started.
                // recDuration - pos gives this directly: positive means the file
                // has pre-roll that must be trimmed; negative means recording
                // started late and silence must be prepended.
                // Using the file duration avoids the sysLatency approximation,
                // which only measures time to the next recorder tick and diverges
                // from the real pre-roll at above-average system latency.
                qWarning() << "Recording duration:";
                qint64 recDuration = 1000 * getMediaDuration(audioRecorded);
                audioOffset = recDuration - pos;

                // DETERMINE videoOffset (no camera means no webcam file to measure)
                if (recordingHasWebcam) {
                    recDuration = 1000 * getMediaDuration(webcamRecorded);
                    videoOffset = recDuration - pos;
                } else {
                    videoOffset = 0;
                }

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
            };

            if (recordingHasWebcam)
                waitForFileFinalization(webcamRecorded, finalizeRecording);
            else
                finalizeRecording();

        } else {
            qWarning() << "*FAILURE* File size is zero.";
            logUI("Recording ERROR. File size is zero.");
            setBanner("Recording ERROR. File size is zero.");

            enable_playback(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            singButton->setEnabled(false);
            singAction->setEnabled(false);
            
            //(false);
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
    //webcamView->hide();
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
    abortButton->hide();

    isPlayback = false;

    // Defer the full media reset so we are not destroying the mediaRecorder
    // (the signal sender) while still executing inside its errorOccurred handler.
    // Destroying the sender during a direct-connection signal call is UB and
    // causes a segfault when Qt unwinds through the now-freed object.
    QTimer::singleShot(0, this, [this]() { resetMediaComponents(false); });

}

void MainWindow::waitForFileFinalization(const QString &filePath, std::function<void()> callback) {
    pollFileFinalization(filePath, 0, callback);
}

// Polls until `filePath` has a valid video stream, or bails after 30 tries.
// Self-scheduling (each attempt only queues the next one after the current
// check actually completes) instead of a fixed-interval repeating QTimer, so
// checks can never overlap — the previous design relied on
// QProcess::waitForFinished() blocking the GUI thread to guarantee that.
// Rewritten to be fully async: the non-native ffprobe path now runs the
// QProcess via its finished() signal instead of waitForFinished() (which had
// no timeout at all and could block the UI thread indefinitely on a stuck
// probe), backed by a watchdog QTimer that kills a hung process outright.
void MainWindow::pollFileFinalization(const QString &filePath, int attempts, std::function<void()> callback) {
    attempts++;

    if (attempts > 30) {
        qWarning() << "Timeout reached after" << attempts << "attempts.";
        QMessageBox::critical(this, "Recorder Error", "Timeout reached. Video did not finalize properly.");
        return;
    }

#ifdef WAKKAQT_FFMPEG_NATIVE
    const bool isValid = FFmpegNative::hasVideoStream(filePath);
    if (isValid) {
        qDebug() << "File is a valid video.";
        callback();
    } else {
        qDebug() << "File not finalized yet, retrying...";
        QTimer::singleShot(222, this, [this, filePath, attempts, callback]() {
            pollFileFinalization(filePath, attempts, callback);
        });
    }
#else
    QProcess *process = new QProcess(this);
    QStringList arguments;
    arguments << "-v" << "error"
              << "-select_streams" << "v:0"
              << "-show_entries" << "stream=codec_type"
              << "-of" << "default=noprint_wrappers=1:nokey=1"
              << filePath;

    // ffprobe on a local file should return almost instantly; if it ever
    // hangs, kill it instead of blocking forever the way an unbounded
    // waitForFinished() could.
    QTimer *watchdog = new QTimer(process);
    watchdog->setSingleShot(true);
    connect(watchdog, &QTimer::timeout, process, [process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, filePath, attempts, callback](int, QProcess::ExitStatus) {
        const QByteArray output = process->readAllStandardOutput();
        process->deleteLater();
        const bool isValid = (!output.isEmpty() && output.trimmed() == "video");

        if (isValid) {
            qDebug() << "File is a valid video.";
            callback();
        } else {
            qDebug() << "File not finalized yet, retrying...";
            QTimer::singleShot(222, this, [this, filePath, attempts, callback]() {
                pollFileFinalization(filePath, attempts, callback);
            });
        }
    });

    watchdog->start(5000);
    process->start("ffprobe", arguments);
#endif
}