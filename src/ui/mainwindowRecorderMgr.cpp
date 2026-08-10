#include "mainwindow.h"
#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#endif


void MainWindow::abortRecording() {
    if (!trySetState(State::Aborting))
        return;
    qWarning() << "Stop recording.";
    stopRecording();
    return;
}

void MainWindow::startRecording() {
    try {
        if (currentVideoFile.isEmpty()) {
            QMessageBox::warning(this, "No Playback Set", "No playback loaded! Please load a playback to sing.");
            singButton->setEnabled(false);
            singAction->setEnabled(false);
            return;
        }

        if (m_state == State::Recording) {
            qWarning() << "Stop recording.";
            stopRecording();
            return;
        }

        // Just-in-time device check — the real safety net against a device
        // that vanished sometime between selection and now. A missing mic
        // blocks starting outright; a missing camera just degrades this take
        // to audio-only, the same as if the user had never selected one.
        if (!isAudioDeviceStillAvailable(selectedDevice)) {
            QMessageBox::warning(this, "No Microphone",
                "The selected microphone is no longer available.\n"
                "Choose an input device (File → Choose Input Devices) and try again.");
            return;
        }
        if (hasCamera && !isCameraDeviceStillAvailable(selectedCameraDevice)) {
            logUI("Selected camera is no longer available — recording audio-only.");
            hasCamera = false;
            previewCheckbox->setChecked(false);
            previewCheckbox->setEnabled(false);
            webcamView->hide();
        }

        if (!trySetState(State::Recording))
            return;

        // A live recording always targets the canonical /tmp paths — never
        // whatever a previous session restore may have repointed
        // webcamRecorded/audioRecorded/extractedTmpPlayback to.
        clearRestoreWorkspace();

        // Disable buttons while recording starts
        singButton->setEnabled(false);
        singAction->setEnabled(false);
        chooseInputButton->setEnabled(false);
        chooseInputAction->setEnabled(false);
        enable_playback(false);

        // Set up the house for recording
        offset = 0;
        // Blocked until right before player->play() below — vizPlayer->seek(0)
        // a few lines down also fires positionChanged, and if the guard were
        // already open that seek would consume the one-shot sync mark before
        // the real recording (and its AudioRecorder gate) even exists, so
        // playback's real positionChanged would never arm it and the whole
        // take would get silently discarded.
        audioSyncArmed = true;
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

            reattachAudioOutputWorkaround();
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

            // Only now is the real gate (created inside startRecording()
            // above) waiting to be armed by the next positionChanged.
            audioSyncArmed = false;
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
        if (m_state != State::Recording && m_state != State::Aborting) {
            qWarning() << "Not recording.";
            logUI("Tried to stop recording, but no recording is in progress.");
            QMessageBox::critical(this, "Not Recording", "Tried to stop recording, but no recording is in progress.");
            return;
        }
        setBanner(".. .Finishing VIDEO.. .");
        disconnect(player.data(), &QMediaPlayer::positionChanged, this, &MainWindow::onPlayerPositionChanged);

        recordingIndicator->hide();
        //webcamView->hide();
        previewCheckbox->setChecked(false);

        if ( player ) {
            vizPlayer->stop();
            playbackTimer->stop();
        }

        if ( mediaRecorder && mediaRecorder->isAvailable() ) {
            mediaRecorder->stop();
        }

        if ( audioRecorder && audioRecorder->isRecording() )
            audioRecorder->stopRecording();

        if ( camera && camera->isAvailable() && camera->isActive() )
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

        if ( m_state == State::Aborting ) {
            handleRecordingError();
            //resetMediaComponents(false);
            return;
        }

        QFile fileAudio(audioRecorded);
        QFile fileCam(webcamRecorded);
        if (fileAudio.size() > 0 && (!recordingHasWebcam || fileCam.size() > 0)) {

            if (!trySetState(State::Finalizing))
                return;
            setBanner("Finalizing recording, please wait...");
            auto finalizeRecording = [this]() {
                // Now video (if any) is ready, proceed safely
                qWarning() << "Proceeding with finalization...";

                // audioOffset is always 0: AudioRecorder gates every captured
                // buffer until the sync mark (first playback position tick —
                // see onPlayerPositionChanged), so the WAV file has zero
                // pre-roll by construction, no post-hoc trim needed.
                // videoOffset was captured at that same mark, since camera
                // and mic recording start a couple of lines apart in
                // startRecording() — negligible skew between them, so the
                // audio-side pre-roll applies equally to the webcam file.
                if (!recordingHasWebcam)
                    videoOffset = 0;

                qWarning() << "System Latency: " << offset << " ms";
                qWarning() << "Audio Gap: " << audioOffset << " ms";
                qWarning() << "Video Gap: " << videoOffset << " ms";
                logUI(QString("System Latency: %1 ms").arg(offset));
                logUI(QString("Calculated Camera Offset: %1 ms").arg(videoOffset));
                logUI(QString("Calculated Audio Offset: %1 ms").arg(audioOffset));



                QString sourceFilePath = extractedPlayback;
                QString destinationFilePath = extractedTmpPlayback;

                QFile sourceFile(sourceFilePath);
                bool playbackCopyOk = false;

                if (sourceFile.exists()) {
                    // Check if the destination file exists
                    if (QFile::exists(destinationFilePath) && !QFile::remove(destinationFilePath)) {
                        qWarning() << "Failed to remove existing file:" << destinationFilePath;
                    } else if (QFile::copy(sourceFilePath, destinationFilePath)) {
                        qDebug() << "File copied successfully to" << destinationFilePath;
                        playbackCopyOk = true;
                    } else {
                        qWarning() << "Failed to copy file to" << destinationFilePath;
                    }
                } else {
                    qWarning() << "Source file does not exist:" << sourceFilePath;
                }

                if (!playbackCopyOk) {
                    qWarning() << "*FAILURE* Could not prepare playback audio for render.";
                    logUI("Recording ERROR: failed to prepare playback audio for render.");
                    setBanner("Recording ERROR: could not prepare playback audio.");
                    trySetState(State::Idle);
                    enable_playback(true);
                    chooseInputButton->setEnabled(true);
                    chooseInputAction->setEnabled(true);
                    QMessageBox::critical(this, "Recording Error",
                        "The extracted playback audio could not be copied.\n"
                        "Rendering cannot continue for this recording.");
                    return;
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

            trySetState(State::Idle);
            enable_playback(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            singButton->setEnabled(false);
            singAction->setEnabled(false);
            
            //(false);
            QMessageBox::critical(this, "Recording Error", "The recorded file is empty (0 bytes).");
        }

    } catch (const std::exception &e) {
        logUI("Error during stopRecording: " + QString::fromStdString(e.what()));
        handleRecordingError();
    }

}

void MainWindow::handleRecorderError(QMediaRecorder::Error error) {
    if (!mediaRecorder) return;

    const QString errorString = mediaRecorder->errorString();
    qWarning() << "Detected camera error:" << error << errorString;
    logUI("Camera Error: " + errorString);

    if ( m_state != State::Recording ) {
        return;
    }

    handleRecordingError();
    QMessageBox::critical(this, "Recording Error", "An error occurred while recording: " + errorString);

}

// QCamera-level errors — distinct from QMediaRecorder's own errorOccurred
// above. The most common real-world trigger is the webcam being unplugged.
void MainWindow::handleCameraError(QCamera::Error error, const QString &errorString) {
    qWarning() << "Detected camera device error:" << error << errorString;
    logUI("Camera Error: " + errorString);

    if ( m_state == State::Recording ) {
        handleRecordingError();
        QMessageBox::critical(this, "Recording Error", "The camera reported an error: " + errorString);
        return;
    }

    // Not recording — WakkaQt already treats "no camera" as a fully
    // supported mode throughout, so losing the camera while just idling or
    // previewing is a graceful degrade into that same mode, not a failure
    // worth interrupting the user with a dialog for.
    logUI("Camera unavailable — falling back to audio-only.");
    setBanner("Camera unavailable — continuing audio-only.");
    hasCamera = false;
    previewCheckbox->setChecked(false);
    previewCheckbox->setEnabled(false);
    webcamView->hide();
    if (camera && camera->isActive())
        camera->stop();
}

// AudioRecorder::captureError — the mic disappearing mid-recording. This is
// the one failure mode that previously had zero detection: QMediaRecorder's
// errorOccurred only covers the video/camera side of the capture graph.
void MainWindow::handleAudioCaptureError(const QString &message) {
    qWarning() << "Detected audio capture error:" << message;
    logUI("Audio Error: " + message);

    if ( m_state != State::Recording )
        return;

    handleRecordingError();
    QMessageBox::critical(this, "Recording Error", message);
}

void MainWindow::handleRecordingError() {

    logUI("Attempting to recover from recording error...");
    setBanner("Attempting to recover from recording error...");

    trySetState(State::Idle);

    qWarning() << "Cleaning up..";

    if ( vizPlayer && player )
        vizPlayer->stop();

    if ( audioRecorder && audioRecorder->isRecording() )
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
        logUI("Recording ERROR: video did not finalize in time.");
        setBanner("Recording ERROR: video did not finalize in time.");
        trySetState(State::Idle);
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
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