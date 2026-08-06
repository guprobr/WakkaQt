
#include "mainwindow.h"
#include "complexes.h"
#include "renderjob.h"
#include <QThreadPool>
#include <QPushButton>
#include <atomic>
#include <memory>

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
            trySetState(State::Idle);
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
        trySetState(State::Idle);
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

    if (m_renderJob) {
        m_renderJob->deleteLater();
        m_renderJob = nullptr;
    }
    m_renderJob = new RenderJob(this);

    RenderJob::Params params;
    params.tunedAudioPath      = tunedRecorded;
    params.webcamPath          = webcamRecorded;
    params.playbackPath        = currentVideoFile;
    params.rawVocalPath        = audioRecorded;
    params.outputPath          = outputFilePath;
    params.vocalVolume         = vocalVolume;
    params.audioOffsetMs       = effectiveAudioOffset;
    params.videoOffsetMs       = effectiveVideoOffset;
    params.resolution          = setRez;
    params.hasWebcam           = recordingHasWebcam;
    params.videoEffectChain    = videoEffectChain;
    params.totalDurationSeconds = getMediaDuration(currentVideoFile);

    startRender(params);
}

void MainWindow::startRender(const RenderJob::Params &params)
{
    if (!trySetState(State::Rendering))
        return;

    disconnect(m_renderJob, &RenderJob::progress, this, nullptr);
    disconnect(m_renderJob, &RenderJob::finished, this, nullptr);

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

    QPushButton *abortRenderBtn = new QPushButton("⛔ Abort Render", this);

    layout->insertWidget(0, abortRenderBtn, 0, Qt::AlignCenter);
    layout->insertWidget(0, progressBar,   0, Qt::AlignCenter);
    layout->insertWidget(0, progressLabel, 0, Qt::AlignCenter);

    connect(abortRenderBtn, &QPushButton::clicked, this, [this]() {
        m_renderJob->cancel();
    });

    QProgressBar *pb = this->progressBar;
    connect(m_renderJob, &RenderJob::progress, this, [pb](double frac) {
        pb->setValue(int(frac * 100));
    });

    connect(m_renderJob, &RenderJob::finished, this,
            [this, progressLabel, abortRenderBtn, params](bool success, bool cancelled, const QString &errorMessage) {
        delete progressLabel;
        delete this->progressBar;
        delete abortRenderBtn;

        if (!success) {
            trySetState(State::Idle);
            enable_playback(true);
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            backingTrackButton->setVisible(true);
            if (cancelled) {
                // No cleanup of outputFilePath here: RenderJob only ever
                // writes through a sidecar ".partial" file and commits it
                // onto outputPath atomically on success (see
                // commitPartialOverFinal()) — on cancellation the partial is
                // already removed by RenderJob itself, and outputPath was
                // never touched. Removing it here would delete a
                // pre-existing valid file if the user was re-rendering over
                // an existing output and aborted.
                logUI("Render aborted.");
                return;
            }
            logUI("Render failed.");
            handleRenderFailure(params, errorMessage.isEmpty()
                ? "Rendering failed. Check the logs." : errorMessage);
            return;
        }

        QMessageBox::information(this, "Rendering Done!",
            "Prepare to preview performance.");
        logUI("Rendering finished.");

        trySetState(State::Idle);
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
    });

    m_renderJob->start(params);
}

// RenderJob never leaves anything expensive-to-recompute behind on failure
// (unlike VocalSeparationJob's export, there is no already-separated
// instrumental to salvage) — so the only meaningful recovery choice is
// retrying with the same Params, which are all durable files already on
// disk, not throwaway intermediate results.
void MainWindow::handleRenderFailure(const RenderJob::Params &params, const QString &errorMessage)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle("Render Error");
    box.setText(errorMessage);
    QPushButton *tryAgainBtn = box.addButton("Try Again", QMessageBox::ActionRole);
    box.addButton("Discard", QMessageBox::DestructiveRole);
    box.setDefaultButton(tryAgainBtn);
    box.exec();

    if (box.clickedButton() == tryAgainBtn)
        startRender(params);
}

QString MainWindow::millisecondsToSecondsString(qint64 milliseconds) {
    double seconds = milliseconds / 1000.0;
    return QString::number(seconds, 'f', 3); 
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