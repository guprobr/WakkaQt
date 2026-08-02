#include "mainwindow.h"
#include "DownloadDialog.h"
#include "youtubesearchdialog.h"

void MainWindow::playVideo(const QString& playbackVideoPath) {

    if ( player && vizPlayer ) {

        isPlayback = false; // disable seeking while decoding/loading new media

        updateVideoVisibility();
        transportWidget->hide();  // re-shown when PlayingState fires

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

// Single source of truth for whether videoWidget or placeholderLabel is
// shown. Call this instead of toggling the two widgets by hand — several
// call sites used to guess at the right visibility ahead of the player
// actually reaching that state, which could leave the placeholder logo
// stuck on top of a video that was, in fact, still playing.
void MainWindow::updateVideoVisibility() {
    const bool showVideo = player && isPlayback
        && !isAudioOnlyFile(currentPlayback)
        && (player->playbackState() == QMediaPlayer::PlayingState
            || player->playbackState() == QMediaPlayer::PausedState);

    videoWidget->setVisible(showVideo);
    placeholderLabel->setVisible(!showVideo);
}

  void MainWindow::onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status) {

    if ( QMediaPlayer::MediaStatus::LoadingMedia == status || \
        ( QMediaPlayer::MediaStatus::LoadedMedia == status && !isPlayback ) ) {

        setBanner(currentVideoName); // video loaded, set title
        banner->setToolTip(currentVideoName);

        if ( !playbackTimer->isActive())
            playbackTimer->start(1000);

        vizPlayer->play();
        backingTrackButton->setVisible(true);

    }

    if ( QMediaPlayer::MediaStatus::EndOfMedia == status ) {

        if ( isRecording && mediaRecorder->duration() )
            stopRecording();

    }

    // Single source of truth for the placeholder/video toggle: driven by the
    // player's actual state rather than assumptions made at each call site
    // above. EndOfMedia in particular can fire as a transient status without
    // playbackState ever leaving PlayingState (e.g. around a seek), which
    // used to leave the placeholder stuck on top of an actively-playing
    // video — this re-derives visibility from ground truth every time.
    updateVideoVisibility();
}

void MainWindow::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {

    if ( QMediaPlayer::PlayingState == state ) {

        if ( !isPlayback ) {
            addProgressSong(progressScene, static_cast<int>(getMediaDuration(currentPlayback)));
        }

        if ( isRecording )
            sysLatency.restart();

        isPlayback = true; // enable seeking now

        transportWidget->show();
        playPauseButton->setText("⏸");
    }

    if ( QMediaPlayer::PausedState == state || QMediaPlayer::StoppedState == state ) {
        playPauseButton->setText("▶");
    }

    updateVideoVisibility();
}

void MainWindow::onPlayPauseClicked() {
    if (!isPlayback || isRecording || !player || !vizPlayer) return;
    if (player->playbackState() == QMediaPlayer::PlayingState) {
        vizPlayer->pause();
    } else {
        vizPlayer->play();
    }
}

void MainWindow::onPlayerPositionChanged(qint64 position) {
    if ( isRecording ) {
        pos = position;
        //sysLatency.restart();
    }
}

void MainWindow::chooseLast()
{
    qint64 lastPos = 0;
    if ( isPlayback )
        lastPos = player->position();
    
    vizPlayer->stop(); // stop to prevent "Unexpected FFmpeg behaviour"

    if (!currentPlayback.isEmpty()) {

    
            resetMediaComponents(false);

            singButton->setEnabled(true);
            singAction->setEnabled(true);
            chooseLastButton->setVisible(true);

            currentVideoFile = currentPlayback;
            playVideo(currentVideoFile);
            logUI("Press SING to start recording.");

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
                vizPlayer->play();
                updateVideoVisibility();
            }); // resume play
}

void MainWindow::chooseVideo()
{
    qint64 lastPos = 0;

    if ( isPlayback )
        lastPos = player->position();

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
            chooseLastButton->setVisible(true);

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
                vizPlayer->play();
                updateVideoVisibility();
            }); // resume play

    delete fileDialog;
}

void MainWindow::fetchVideo() {
    const QString urlStr = urlInput->text().trimmed();
    if (urlStr.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please enter a YouTube URL.");
        return;
    }

    const QUrl url(urlStr);
    if (!isSingleYouTubeVideoUrl(url)) {
        QMessageBox::warning(
            this, "Invalid URL",
            "Please paste a single *video* URL from YouTube.\n"
            "Tip: Use YouTube's \"Share\" button and copy that link.\n"
            "Playlists are not supported.");
        return;
    }

    const qint64 lastPos = player ? player->position() : 0;
    vizPlayer->stop();
    videoWidget->hide();
    placeholderLabel->show();

    const QString directory = QFileDialog::getExistingDirectory(this, "Choose Directory to Save Video");
    if (directory.isEmpty()) {
        if (isPlayback) {
            QTimer::singleShot(500, this, [this, lastPos]() {
                vizPlayer->seek(lastPos, true);
                #if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
                #ifdef __linux__
                    player->setAudioOutput(nullptr);
                    player->setAudioOutput(audioOutput.data());
                #endif
                #endif
                vizPlayer->play();
                updateVideoVisibility();
            });
        }
        return;
    }

    fetchButton->setEnabled(false);

    DownloadDialog dlg(this);
    dlg.start(urlStr, directory);

    if (dlg.exec() == QDialog::Accepted) {
        // Sucess        
        
        this->downloadedVideoPath = dlg.downloadedFilePath();

        if (QFile::exists(this->downloadedVideoPath)) {
            resetMediaComponents(false);

            currentVideoFile = this->downloadedVideoPath;
            if (player && vizPlayer) {
                vizPlayer->stop();
                playbackTimer->stop();

                playVideo(currentVideoFile);

                downloadStatusLabel->setText("Download YouTube URL");
                singButton->setEnabled(true);
                singAction->setEnabled(true);
                chooseLastButton->setVisible(true);
                logUI("Previewing playback. Press SING to start recording.");
            }
        }
        fetchButton->setEnabled(true);
    } else {

        if (isPlayback) {
            QTimer::singleShot(500, this, [this, lastPos]() {
                vizPlayer->seek(lastPos, true);
                vizPlayer->play();
                updateVideoVisibility();
            });
        }
        fetchButton->setEnabled(true);
    }
}

void MainWindow::openYoutubeBrowser()
{
    YoutubeSearchDialog dlg(this);
    connect(&dlg, &YoutubeSearchDialog::downloadRequested,
            this, [this](const QString &url, const QString &) {
                urlInput->setText(url);
                fetchVideo();
            });
    dlg.exec();
}
