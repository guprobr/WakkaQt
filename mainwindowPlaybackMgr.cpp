#include "mainwindow.h"

void MainWindow::playVideo(const QString& playbackVideoPath) {

    if ( player && vizPlayer ) {

        isPlayback = false; // disable seeking while decoding/loading new media

        videoWidget->hide();
        placeholderLabel->show();

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

  void MainWindow::onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status) {

    if ( QMediaPlayer::MediaStatus::LoadingMedia == status || \
        ( QMediaPlayer::MediaStatus::LoadedMedia == status && !isPlayback ) ) {

        setBanner(currentVideoName); // video loaded, set title
        banner->setToolTip(currentVideoName);

        if ( !playbackTimer->isActive())
            playbackTimer->start(1000);

        if ( !( (currentPlayback.endsWith("mp3", Qt::CaseInsensitive))             \
                ||  (currentPlayback.endsWith("wav", Qt::CaseInsensitive))             \
                ||  (currentPlayback.endsWith("opus", Qt::CaseInsensitive))             \
                ||  (currentPlayback.endsWith("flac", Qt::CaseInsensitive)) )) {
            placeholderLabel->hide();
            videoWidget->show();
        } else {
            videoWidget->hide();
            placeholderLabel->show();
        }
        vizPlayer->play();
                
    }

    if ( QMediaPlayer::MediaStatus::EndOfMedia == status ) {

        if ( isRecording && mediaRecorder->duration() )
            stopRecording();
        
        videoWidget->hide();
        placeholderLabel->show();

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
        pos = position;
        sysLatency.restart();
    }
}


void MainWindow::chooseVideo()
{

    qint64 lastPos = player->position();
    vizPlayer->stop(); // stop to prevent "Unexpected FFmpeg behaviour"
    videoWidget->hide();
    placeholderLabel->show();

    QFileDialog* fileDialog = new QFileDialog(this);
    fileDialog->setWindowTitle("Open Playback File");
    fileDialog->setNameFilter("Video or Audio (*.mp4 *.mkv *.webm *.avi *.mov *.mp3 *.wav *.flac *.opus)");
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
                if ( !( (currentPlayback.endsWith("mp3", Qt::CaseInsensitive))             \
                ||  (currentPlayback.endsWith("wav", Qt::CaseInsensitive))                  \
                ||  (currentPlayback.endsWith("opus", Qt::CaseInsensitive))                  \
                ||  (currentPlayback.endsWith("flac", Qt::CaseInsensitive)) )) {
                    placeholderLabel->hide();
                    videoWidget->show();
                }
                vizPlayer->play();
            }); // resume play

    delete fileDialog;
}

void MainWindow::fetchVideo() {

    QString url = urlInput->text();
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please enter a YouTube URL.");
        return;
    }

    qint64 lastPos = player->position();
    vizPlayer->stop();
    videoWidget->hide();
    placeholderLabel->show();

    QString directory = QFileDialog::getExistingDirectory(this, "Choose Directory to Save Video");
    if (directory.isEmpty()) {
        if (isPlayback)
            QTimer::singleShot(500, this, [this, lastPos]() {
                vizPlayer->seek(lastPos, true);
                #if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
                #ifdef __linux__
                        player->setAudioOutput(nullptr);
                        player->setAudioOutput(audioOutput.data());
                #endif
                #endif
                if (!(currentPlayback.endsWith("mp3", Qt::CaseInsensitive) ||
                      currentPlayback.endsWith("wav", Qt::CaseInsensitive) ||
                      currentPlayback.endsWith("opus", Qt::CaseInsensitive) ||
                      currentPlayback.endsWith("flac", Qt::CaseInsensitive))) {
                    placeholderLabel->hide();
                    videoWidget->show();
                }
                vizPlayer->play();
            });

        return;
    }

    fetchButton->setEnabled(false);
    downloadStatusLabel->setText("Getting file name...");
    QProcess *filenameProcess = new QProcess(this);

    QString outputTemplate = directory + "/%(title)s.%(ext)s";  // Using title for filename
    QStringList filenameArgs;
    filenameArgs << "--print" << "filename"
                 << "--output" << outputTemplate
                 << url;

    this->downloadedVideoPath.clear();

    connect(filenameProcess, &QProcess::readyReadStandardOutput, this, [this, directory, filenameProcess]() {
        QByteArray outputData = filenameProcess->readAllStandardOutput();
        QString output = QString::fromUtf8(outputData).trimmed();
        if (!output.isEmpty()) {
            // Get the base filename and extension
            QFileInfo fileInfo(output);
            QString baseName = fileInfo.completeBaseName();  // Get the title part
            QString extension = fileInfo.suffix();  // Get the extension

            // Sanitize the title part only (baseName)
            QRegularExpression regex("[^a-zA-Z0-9_\\-\\.\\ ]");  // Allow alphanumeric, space, hyphen, dot, and underscore
            baseName.replace(regex, "_");  // Replace invalid characters with underscore

            // Rebuild the complete file path with the sanitized base name and the original extension
            this->downloadedVideoPath = directory + "/" + baseName + "." + extension;

            qDebug() << "Predicted filename:" << this->downloadedVideoPath;
            logUI("yt-dlp predicted filename: " + this->downloadedVideoPath);
        }
    });

    connect(filenameProcess, &QProcess::finished, this, [this, filenameProcess, directory, url, lastPos]() {
        filenameProcess->deleteLater();

        if (this->downloadedVideoPath.isEmpty()) {
            downloadStatusLabel->setText("Failed to get filename.");
            fetchButton->setEnabled(true);
            return;
        }

        // Now that we have the filename, start the actual download
        downloadStatusLabel->setText("Downloading...");
        QProcess *downloadProcess = new QProcess(this);

        QStringList downloadArgs;
        downloadArgs << "--output" << this->downloadedVideoPath  // Use sanitized filename here
                     << url;

        connect(downloadProcess, &QProcess::finished, this, [this, downloadProcess, lastPos]() {
            downloadStatusLabel->setText("Download complete.");
            downloadProcess->deleteLater();

            if (QFile::exists(this->downloadedVideoPath)) {
                resetMediaComponents(false);
                placeholderLabel->hide();
                videoWidget->show();

                currentVideoFile = this->downloadedVideoPath;
                if (player && vizPlayer) {
                    vizPlayer->stop();
                    videoWidget->hide();
                    placeholderLabel->show();
                    playbackTimer->stop();

                    playVideo(currentVideoFile);

                    downloadStatusLabel->setText("Download YouTube URL");
                    singButton->setEnabled(true);
                    singAction->setEnabled(true);
                    logUI("Previewing playback. Press SING to start recording.");
                    fetchButton->setEnabled(true);
                }
            } else {
                if (isPlayback)
                    QTimer::singleShot(500, this, [this, lastPos]() {
                        vizPlayer->seek(lastPos, true);
                        if (!(currentPlayback.endsWith("mp3", Qt::CaseInsensitive) ||
                              currentPlayback.endsWith("wav", Qt::CaseInsensitive) ||
                              currentPlayback.endsWith("opus", Qt::CaseInsensitive) ||
                              currentPlayback.endsWith("flac", Qt::CaseInsensitive))) {
                            placeholderLabel->hide();
                            videoWidget->show();
                        }
                        vizPlayer->play();
                        fetchButton->setEnabled(true);
                    });
            }
        });

        connect(downloadProcess, &QProcess::readyReadStandardError, this, [this, downloadProcess]() {
            QByteArray errorData = downloadProcess->readAllStandardError();
            QString error = QString::fromUtf8(errorData).trimmed();
            if (!error.isEmpty()) {
                qDebug() << "yt-dlp Error:" << error;
                logUI("yt-dlp: " + error);
            }
        });

        downloadProcess->start("yt-dlp", downloadArgs);
    });

    filenameProcess->start("yt-dlp", filenameArgs);
}