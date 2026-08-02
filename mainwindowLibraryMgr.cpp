#include "mainwindow.h"
#include "librarydialog.h"
#include "sessionmanager.h"
#include "complexes.h"

// ─────────────────────────────────────────────────────────────────────────────
// openLibrary
//
// Opens the Library dialog. Every interactive main-window control is disabled
// while the dialog is open so the user cannot accidentally start a recording
// or load a new file during the modal session.
//
// If something is currently playing, it is paused before the dialog opens and
// resumed automatically when the dialog is closed without a restore selection.
// If the user picks a session to restore, playback is not resumed (it will be
// replaced by the restored session's render flow).
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::openLibrary()
{
    // ── Pause active playback so music doesn't keep playing behind the dialog
    const bool wasPlaying = isPlayback
                         && player
                         && player->playbackState() == QMediaPlayer::PlayingState;
    if (wasPlaying) {
        vizPlayer->pause();
        logUI("Library: playback paused.");
    }

    // ── Disable everything while dialog is open ───────────────────────────
    enable_playback(false);                   // covers chooseVideoButton, chooseLastButton,
                                              // libraryButton, fetchButton, menu actions
    singButton->setEnabled(false);
    singAction->setEnabled(false);
    chooseInputButton->setEnabled(false);
    chooseInputAction->setEnabled(false);

    LibraryDialog dlg(this);

    if (dlg.exec() == QDialog::Accepted) {
        const QString id = dlg.selectedSessionId();
        if (!id.isEmpty()) {
            restoreAndRender(id);
            // restoreAndRender manages its own control state — return immediately.
            // Playback is intentionally NOT resumed here; the restore flow takes over.
            return;
        }
    }

    // ── Dialog closed without a restore — put controls back ───────────────
    enable_playback(true);
    chooseInputButton->setEnabled(true);
    chooseInputAction->setEnabled(true);
    // singButton stays disabled unless a video is already loaded

    // ── Resume playback if it was running when the dialog was opened ───────
    if (wasPlaying) {
        vizPlayer->play();
        logUI("Library: playback resumed.");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// saveCurrentSession
//
// Called by renderAgain() right before the resolution dialog, i.e. the
// earliest point at which all recording artefacts are confirmed ready on disk.
// Silently saves into ~/.WakkaQt/library/<uuid>/.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::saveCurrentSession()
{
    SessionManager mgr;
    const QString id = mgr.saveSession(
        webcamRecorded,
        audioRecorded,
        tunedRecorded,
        extractedTmpPlayback,
        currentVideoFile,
        currentVideoName,
        audioOffset,
        videoOffset,
        offset          // system-measured latency
    );

    if (id.isEmpty())
        logUI("Library: \xe2\x9a\xa0 WARNING — session could not be saved to library.");
    else
        logUI("Library: session saved \xe2\x86\x92 " + id);
}

// ─────────────────────────────────────────────────────────────────────────────
// restoreAndRender
//
// 1. Copies library artefacts back into the fixed /tmp/ paths.
// 2. Restores currentVideoFile/Name and all offsets.
// 3. Resumes at the exact same step as saveCurrentSession() — i.e. the
//    output-file dialog → resolution question → PreviewDialog → mixAndRender.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::restoreAndRender(const QString &sessionId)
{
    // ── Restore artefacts ─────────────────────────────────────────────────
    SessionManager mgr;
    const bool ok = mgr.restoreSession(
        sessionId,
        webcamRecorded,
        audioRecorded,
        tunedRecorded,
        extractedTmpPlayback,
        currentVideoFile,
        currentVideoName,
        audioOffset,
        videoOffset,
        offset
    );

    if (!ok) {
        QMessageBox::critical(this, "Library Error",
            "Could not restore session files.\n"
            "The session folder may be missing or its files have been removed.");
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        return;
    }

    logUI("Library: restored session " + sessionId);
    logUI("Library: resuming at render step…");
    setBanner("Session restored \xe2\x80\x94 choose output and adjust!");

    // Keep currentPlayback in sync so audio-format detection and
    // chooseLast() work correctly with the restored session's file.
    currentPlayback = currentVideoFile;

    // ── Reset UI to post-recording state ──────────────────────────────────
    // Mirrors the state MainWindow is in just before renderAgain() runs.
    videoWidget->hide();
    placeholderLabel->show();
    singButton->setEnabled(false);
    singAction->setEnabled(false);

    // Re-enable playback-load controls so the user can cancel and pick a
    // different file if they accidentally triggered the wrong session.
    enable_playback(true);
    chooseInputButton->setEnabled(true);
    chooseInputAction->setEnabled(true);

    // ── Output file dialog (loop until valid path or cancelled) ─────────────
    static const QString kRenderFilter =
        "MP4 Files (*.mp4);;MKV Files (*.mkv);;WebM Files (*.webm);;AVI Files (*.avi);;"
        "MP3 Files (*.mp3);;FLAC Files (*.flac);;WAV Files (*.wav);;Opus Files (*.opus)";
    static const QStringList kAllowedExts =
        {"mp4", "mkv", "webm", "avi", "mp3", "flac", "wav", "opus"};

    QString restoredOutput;
    while (true) {
        QFileDialog outDlg(this, "Mix destination (default .MP4)", restoredOutput, kRenderFilter);
        outDlg.setAcceptMode(QFileDialog::AcceptSave);
        outDlg.setOption(QFileDialog::DontUseNativeDialog);
        // Only used when the user types a filename with no extension at all —
        // an extension the user does type (in any filter) is always kept as-is.
        outDlg.setDefaultSuffix(kAllowedExts.first());
        QObject::connect(&outDlg, &QFileDialog::filterSelected, &outDlg,
            [&outDlg](const QString &filter) {
                const int star = filter.lastIndexOf("*.");
                if (star < 0) return;
                const QString ext = filter.mid(star + 2).section(')', 0, 0).trimmed().toLower();
                if (!ext.isEmpty())
                    outDlg.setDefaultSuffix(ext);
            });
        restoredOutput = (outDlg.exec() == QDialog::Accepted)
                         ? outDlg.selectedFiles().value(0) : QString{};

        if (restoredOutput.isEmpty()) {
            logUI("Library: restore cancelled at output-file step.");
            enable_playback(true);
            return;
        }

        if (!kAllowedExts.contains(QFileInfo(restoredOutput).suffix().toLower())) {
            QMessageBox::warning(this, "Invalid File Extension",
                "Please choose a file with one of the following extensions:\n"
                ".mp4, .mkv, .webm, .avi, .mp3, .flac, .wav, .opus");
            continue;
        }

        {
            const QString outAbs = QFileInfo(restoredOutput).absoluteFilePath();
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

    // ── Resolution choice — identical to renderAgain() ────────────────────
    const int rezResponse = QMessageBox::question(
        this,
        "Resolution",
        "Do you want 1920x1080 high-resolution video?\n"
        "Low resolution 640x480 renders much faster.",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    setRez = (rezResponse == QMessageBox::Yes) ? "1920x1080" : "640x480";
    qDebug() << "Restored session will render at:" << setRez;

    // ── PreviewDialog — volume / offset / pitch adjustment ────────────────
    outputFilePath = restoredOutput;

    previewDialog.reset(new PreviewDialog(audioOffset, this));
    previewDialog->setAudioFile(audioRecorded);
    previewDialog->setVideoFile(webcamRecorded, videoOffset);

    if (previewDialog->exec() == QDialog::Accepted) {
        const double vocalVolume  = previewDialog->getVolume();
        const qint64 manualOffset = previewDialog->getOffset();
        const QString videoEffectChain = previewDialog->getVideoEffectChain();
        previewDialog.reset();
        mixAndRender(vocalVolume, manualOffset, videoEffectChain);
    } else {
        previewDialog.reset();
        logUI("Library: restore cancelled during preview/adjustment.");
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        singButton->setEnabled(false);
        singAction->setEnabled(false);
        QMessageBox::warning(this, "Restore Cancelled",
            "Session restore cancelled during volume adjustment.\n"
            "Open the Library again to try once more.");
    }
}
