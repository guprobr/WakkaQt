
#include "mainwindow.h"
#include "complexes.h"
#include <QThreadPool>
#include <QPushButton>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <memory>
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

    enable_playback(false);
    if (progressSongFull)
        progressSongFull->setToolTip("Nothing to seek");
    setBanner("Let's preview performance and render!");

    // Loop until the user picks a valid output path or cancels.
    // (Previously used recursion here which could stack-overflow on repeated
    // bad-extension choices — replaced with a safe while loop.)
    // No camera was used for this recording, so there is no video track to
    // mux — restrict the choice to audio-only containers. Uses
    // recordingHasWebcam (fixed for this specific session), not hasCamera
    // (today's device state) — renderAgain() only ever runs right after
    // stopRecording() finalizes THIS recording, so they're equal here, but
    // recordingHasWebcam is the one that actually means "does this session
    // have webcam material".
    static const QString kRenderFilter =
        "MP4 Files (*.mp4);;MKV Files (*.mkv);;WebM Files (*.webm);;AVI Files (*.avi);;"
        "MP3 Files (*.mp3);;FLAC Files (*.flac);;WAV Files (*.wav);;Opus Files (*.opus)";
    static const QString kAudioOnlyRenderFilter =
        "MP3 Files (*.mp3);;FLAC Files (*.flac);;WAV Files (*.wav);;Opus Files (*.opus)";
    const QStringList allowedExtensions = recordingHasWebcam
        ? QStringList{"mp4","mkv","webm","avi","mp3","flac","wav","opus"}
        : QStringList{"mp3","flac","wav","opus"};
    const QString &defaultSuffix = allowedExtensions.first();
    while (true) {
        QFileDialog dlg(this, "Mix destination (default ." + defaultSuffix.toUpper() + ")", "",
                        recordingHasWebcam ? kRenderFilter : kAudioOnlyRenderFilter);
        dlg.setAcceptMode(QFileDialog::AcceptSave);
        dlg.setOption(QFileDialog::DontUseNativeDialog);
        // Only used when the user types a filename with no extension at all —
        // an extension the user does type (in any filter) is always kept as-is.
        dlg.setDefaultSuffix(allowedExtensions.first());
        QObject::connect(&dlg, &QFileDialog::filterSelected, &dlg,
            [&dlg](const QString &filter) {
                const int star = filter.lastIndexOf("*.");
                if (star < 0) return;
                const QString ext = filter.mid(star + 2).section(')', 0, 0).trimmed().toLower();
                if (!ext.isEmpty())
                    dlg.setDefaultSuffix(ext);
            });
        outputFilePath = (dlg.exec() == QDialog::Accepted)
                         ? dlg.selectedFiles().value(0) : QString{};

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
                + (recordingHasWebcam ? QString(".mp4, .mkv, .webm, .avi, .mp3, .flac, .wav, .opus")
                                      : QString(".mp3, .flac, .wav, .opus (no camera was used — audio-only)")));
            continue;
        }

        {
            const QString outAbs = QFileInfo(outputFilePath).absoluteFilePath();
            bool collides = false;
            for (const QString &inp : {audioRecorded, webcamRecorded, currentVideoFile,
                                       tunedRecorded, extractedTmpPlayback}) {
                if (!inp.isEmpty() && QFileInfo(inp).absoluteFilePath() == outAbs) {
                    collides = true;
                    break;
                }
            }
            if (collides) {
                QMessageBox::warning(this, "Invalid Output Path",
                    "The output file cannot overwrite one of the input files.\n"
                    "Please choose a different name or location.");
                continue;
            }
        }

        break; // valid extension and no collision with inputs
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
    previewDialog.reset(new PreviewDialog(audioOffset, this));
    previewDialog->setAudioFile(audioRecorded);
    previewDialog->setVideoFile(webcamRecorded, videoOffset);
    if (previewDialog->exec() == QDialog::Accepted)
    {
        double vocalVolume = previewDialog->getVolume();
        qint64 manualOffset = previewDialog->getOffset();
        QString videoEffectChain = previewDialog->getVideoEffectChain();
        previewDialog.reset();
        mixAndRender(vocalVolume, manualOffset, videoEffectChain);
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

void MainWindow::mixAndRender(double vocalVolume, qint64 manualOffset, const QString &videoEffectChain) {

    videoWidget->hide();
    placeholderLabel->show();

    // Disable everything that shouldn't be reachable while FFmpeg is running.
    // enable_playback covers: transport buttons, load-playback, library, fetch.
    // isPlayback=false blocks the progress-bar seek event filter as well.
    isPlayback = false;
    enable_playback(false);
    if (progressSongFull)
        progressSongFull->setToolTip("Nothing to seek");

    singButton->setEnabled(false);
    singAction->setEnabled(false);
    chooseInputButton->setEnabled(false);
    chooseInputAction->setEnabled(false);
    backingTrackButton->setVisible(false);

    progressBar = new QProgressBar(this);
    progressBar->setMinimumSize(640, 25);
    progressBar->setRange(0, 100);

    QLabel *progressLabel = new QLabel("Rendering...", this);
    progressLabel->setAlignment(Qt::AlignCenter);
    progressLabel->setFont(QFont("Arial", 8));
    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());

    // Member (not a local) so closeEvent() can request cancellation and wait
    // on renderWatcher if the window is closed mid-render.
    renderCancelled = std::make_shared<std::atomic<bool>>(false);
    QPushButton *abortRenderBtn = new QPushButton("⛔ Abort Render", this);

    layout->insertWidget(0, abortRenderBtn, 0, Qt::AlignCenter);
    layout->insertWidget(0, progressBar,   0, Qt::AlignCenter);
    layout->insertWidget(0, progressLabel, 0, Qt::AlignCenter);

    // effectiveAudioOffset and effectiveVideoOffset are intentionally identical.
    //
    // Both audio and video recordings start at the same instant and share the same
    // pre-roll length (offset ms before the song begins).  The rendered output must
    // apply the same shift to both streams so their relative timing is preserved:
    //
    //   manualOffset > 0  →  trim manualOffset ms from the start of both audio
    //                        (decodeAudioToFloat skip) and video (avformat_seek_file).
    //   manualOffset < 0  →  prepend |manualOffset| ms of silence to audio AND delay
    //                        the video stream by the same |manualOffset| ms.
    //                        Both files are read from t=0; their pre-roll content lands
    //                        at the same output time → streams stay in sync.
    //
    // The old formula (max(-offset, offset+manualOffset)) was wrong for the negative
    // case: it produced a delay smaller than |manualOffset|, causing audio to lag video
    // by (|manualOffset| - |effectiveVideoOffset|) ms.
    const qint64 effectiveAudioOffset = manualOffset;
    const qint64 effectiveVideoOffset = manualOffset;

    auto onFinished = [this, progressLabel, abortRenderBtn](bool success) {
        delete progressLabel;
        delete this->progressBar;
        delete abortRenderBtn;

        if (!success) {
            enable_playback(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            backingTrackButton->setVisible(true);
            if (renderCancelled->load()) {
                logUI("Render aborted.");
                QFile::remove(outputFilePath);
                return;
            }
            logUI("Render failed.");
            QMessageBox::critical(this, "Render Error", "Rendering failed. Check the logs.");
            return;
        }

        QFile file(outputFilePath);
        if (!file.exists()) {
            qWarning() << "Output file missing:" << outputFilePath;
            enable_playback(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            backingTrackButton->setVisible(true);
            QMessageBox::critical(this, "Render Error", "Output file was not created.");
            return;
        }

        QMessageBox::information(this, "Rendering Done!",
            "Prepare to preview performance.");
        logUI("Rendering finished.");

        isRecording = false;
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        backingTrackButton->setVisible(true);
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
    // Native render — runs in a background thread; progress bar updated via lambda.
    // Owned by renderWatcher (see mainwindow.h) instead of a bare
    // QThreadPool::globalInstance()->start([=]{...}): the worker lambda below
    // captures only local value copies of the QStrings it needs (never
    // `this`), and the completion callback is delivered through a
    // QFutureWatcher connected with `this` as context — auto-disconnected by
    // Qt if `this` is destroyed first, unlike the previous
    // QMetaObject::invokeMethod(qApp, ...) dispatch, where using the
    // (effectively immortal) qApp as context gave no such protection at all
    // for the captured `this` inside onFinished().
    QProgressBar *pb = this->progressBar;
    connect(abortRenderBtn, &QPushButton::clicked, this, [this]() {
        renderCancelled->store(true);
    });

    if (renderWatcher) {
        renderWatcher->deleteLater();
        renderWatcher = nullptr;
    }
    renderWatcher = new QFutureWatcher<bool>(this);
    connect(renderWatcher, &QFutureWatcher<bool>::finished, this, [this, onFinished]() {
        const bool ok = renderWatcher->result();
        QFutureWatcher<bool> *finishedWatcher = renderWatcher;
        renderWatcher = nullptr;
        finishedWatcher->deleteLater();
        onFinished(ok);
    });

    const QString tunedRecordedCopy    = tunedRecorded;
    const QString webcamRecordedCopy   = webcamRecorded;
    const QString currentVideoFileCopy = currentVideoFile;
    const QString outputFilePathCopy   = outputFilePath;
    const QString audioRecordedCopy    = audioRecorded;
    const QString setRezCopy           = setRez;
    const QString videoEffectChainCopy = videoEffectChain;
    auto renderCancelledCopy = renderCancelled; // shared_ptr, safe to copy across threads

    auto future = QtConcurrent::run([=]() {
        return FFmpegNative::renderVideo(
            tunedRecordedCopy,
            webcamRecordedCopy,
            currentVideoFileCopy,
            outputFilePathCopy,
            vocalVolume,
            effectiveAudioOffset,
            effectiveVideoOffset,
            setRezCopy,
            audioRecordedCopy,
            renderCancelledCopy.get(),
            [pb](double p) {
                QMetaObject::invokeMethod(pb, [pb, p]() {
                    pb->setValue(int(p * 100));
                }, Qt::QueuedConnection);
            },
            videoEffectChainCopy);
    });
    renderWatcher->setFuture(future);
#else
    // QProcess fallback (ffmpeg must be in PATH)
    const QString offsetFilter = (manualOffset < 0)
        ? QString("adelay=%1|%1").arg(-manualOffset)
        : QString("atrim=start=%1,asetpts=PTS-STARTPTS").arg(manualOffset / 1000.0);

    QString videorama;
    if (recordingHasWebcam &&
        (outputFilePath.endsWith(".mp4", Qt::CaseInsensitive) ||
         outputFilePath.endsWith(".avi", Qt::CaseInsensitive) ||
         outputFilePath.endsWith(".mkv", Qt::CaseInsensitive) ||
         outputFilePath.endsWith(".webm", Qt::CaseInsensitive)))
    {
        videorama = QString("[1:v]scale=s=%1[videorama];").arg(setRez);
    }

    // No camera recording exists to open as an input in this case — the
    // playback track shifts from index 2 to index 1.
    const QString playbackIdx = recordingHasWebcam ? "2" : "1";

    QStringList arguments;
    arguments << "-y"
              << "-i" << tunedRecorded;
    // -ss applies to whichever -i immediately follows it — that must stay
    // the webcam input (it seeks past its pre-roll); with no camera there is
    // no such input to seek into, so drop it rather than let it silently
    // reassign to currentVideoFile below.
    if (recordingHasWebcam) {
        arguments << "-ss" << QString("%1ms").arg(effectiveVideoOffset)
                  << "-i" << webcamRecorded;
    }
    arguments << "-i" << currentVideoFile
              << "-filter_complex"
              // tunedRecorded already went through the audio-masterization filter
              // chain upstream (before VocalEnhancer), so this only applies volume/offset.
              << QString("[0:a]%1,volume=%2[vocals];"
                         "[%4:a][vocals]amix=inputs=2:normalize=0,aresample=async=1[wakkamix];%3")
                     .arg(offsetFilter).arg(vocalVolume).arg(videorama).arg(playbackIdx)
              << "-map" << "[wakkamix]";
    if (!videorama.isEmpty())
        arguments << "-map" << "[videorama]";
    arguments << outputFilePath;

    int totalDuration = static_cast<int>(getMediaDuration(currentVideoFile));

    QProcess *process = new QProcess(this);
    connect(abortRenderBtn, &QPushButton::clicked, this, [this, process]() {
        renderCancelled->store(true);
        process->kill();
    });
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
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        backingTrackButton->setVisible(true);
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
    // This function is called synchronously by all of its callers (they use
    // the returned duration immediately for arithmetic), so it can't be made
    // truly async without restructuring every call site — but a hung
    // ffprobe used to be able to block the GUI thread forever regardless
    // (waitForFinished() with no timeout at all). Bounding the wait turns
    // "frozen indefinitely" into "treated as duration=0 after 5s", the same
    // failure this function already returns for a normal parse failure.
    QProcess ffprobeProcess;
    ffprobeProcess.start("ffprobe", QStringList() << "-v" << "error" << "-show_entries"
                         << "format=duration" << "-of" << "default=noprint_wrappers=1:nokey=1"
                         << filePath);
    if (!ffprobeProcess.waitForFinished(5000)) {
        qWarning() << "ffprobe timed out getting duration for" << filePath << "— killing it";
        ffprobeProcess.kill();
        ffprobeProcess.waitForFinished();
        return 0;
    }
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

    // Self-healing safety net: re-derive placeholder/video visibility every
    // second regardless of which signal path got us here.
    updateVideoVisibility();
}