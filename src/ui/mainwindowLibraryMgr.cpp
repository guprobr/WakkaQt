#include "mainwindow.h"
#include "librarydialog.h"
#include "sessionrepository.h"
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
    if (m_state != State::Idle)
        return;

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
    // Re-enable SING only if a playback was already loaded before the dialog
    // opened — same gating startRecording() itself uses.
    if (!currentVideoFile.isEmpty()) {
        singButton->setEnabled(true);
        singAction->setEnabled(true);
    }

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
    SessionSnapshot snapshot;
    snapshot.currentVideoFile = currentVideoFile;
    snapshot.currentVideoName = currentVideoName;
    snapshot.audioOffset      = audioOffset;
    snapshot.videoOffset      = videoOffset;
    snapshot.sysOffset        = offset; // system-measured latency

    SessionRepository repo;
    const SaveResult result = repo.saveSession(snapshot);

    if (!result.ok)
        logUI("Library: \xe2\x9a\xa0 WARNING — session could not be saved to library: " + result.error);
    else
        logUI("Library: session saved \xe2\x86\x92 " + result.sessionId);
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
    if (!trySetState(State::Restoring))
        return;

    // Drop any workspace left over from a previous restore cycle before
    // starting a new one — safe here since the state machine only lets us
    // reach Restoring from Idle, which means whatever used the old
    // workspace (render, or a cancel branch) has already finished with it.
    clearRestoreWorkspace();

    // Closes a stale-isPlayback window: without this, transport buttons
    // re-enabled below (enable_playback(true)) could operate on whatever
    // media the *previous* session left loaded in player/vizPlayer.
    isPlayback = false;

    // ── Restore artefacts ─────────────────────────────────────────────────
    SessionRepository repo;
    const RestoreResult result = repo.restoreSession(sessionId);

    if (!result.ok) {
        trySetState(State::Idle);
        QMessageBox::critical(this, "Library Error",
            "Could not restore session files.\n"
            + result.error);
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        return;
    }

    // mixAndRender() (called further below) must judge THIS session, not
    // whatever camera happens to be plugged into the machine today — a
    // restored audio-only session must not pull in a stale/unrelated webcam
    // file, and a restored session that does have webcam footage must not
    // get rendered audio-only just because no camera is attached right now.
    // Only assigned once restore has actually succeeded, so a failed
    // restore never mutates this active-session flag.
    recordingHasWebcam = result.hasWebcam;

    // Render works directly out of this restore's own workspace: repoint
    // the shared tmp-path globals so every downstream consumer (collision
    // check below, PreviewDialog, mixAndRender()'s RenderJob::Params) reads
    // this session's files without needing its own path plumbing. Left
    // empty/canonical for an artefact this session doesn't have (e.g. no
    // webcam.mkv in an audio-only session).
    m_activeRestoreWorkspaceDir = result.workspaceDir;
    if (!result.webcamPath.isEmpty())
        webcamRecorded = result.webcamPath;
    if (!result.audioPath.isEmpty())
        audioRecorded = result.audioPath;
    if (!result.playbackPath.isEmpty())
        extractedTmpPlayback = result.playbackPath;

    currentVideoFile = result.snapshot.currentVideoFile;
    currentVideoName = result.snapshot.currentVideoName;
    audioOffset       = result.snapshot.audioOffset;
    videoOffset       = result.snapshot.videoOffset;
    offset            = result.snapshot.sysOffset;

    logUI("Library: restored session " + sessionId);
    for (const QString &warning : result.warnings)
        logUI("Library: \xe2\x9a\xa0 WARNING — " + warning);
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
    // Restricted to audio-only containers when the restored session itself
    // has no webcam footage — mirrors renderAgain()'s handling for a live
    // recording, driven by the same recordingHasWebcam flag set above.
    const QStringList kAllowedExts = allowedRenderExtensions(recordingHasWebcam);
    const QString &defaultRestoreSuffix = kAllowedExts.first();

    QString restoredOutput;
    while (true) {
        QFileDialog outDlg(this, "Mix destination (default ." + defaultRestoreSuffix.toUpper() + ")",
                           restoredOutput, renderSaveFilter(recordingHasWebcam));
        outDlg.setAcceptMode(QFileDialog::AcceptSave);
        outDlg.setOption(QFileDialog::DontUseNativeDialog);
        // Only used when the user types a filename with no extension at all —
        // an extension the user does type (in any filter) is always kept as-is.
        outDlg.setDefaultSuffix(kAllowedExts.first());
        syncDefaultSuffixToFilter(outDlg);
        restoredOutput = (outDlg.exec() == QDialog::Accepted)
                         ? outDlg.selectedFiles().value(0) : QString{};

        if (restoredOutput.isEmpty()) {
            trySetState(State::Idle);
            logUI("Library: restore cancelled at output-file step.");
            enable_playback(true);
            // Matches renderAgain()'s equivalent cancel branch — previously
            // left disabled here, an inconsistency with the live-record path.
            chooseInputButton->setEnabled(true);
            chooseInputAction->setEnabled(true);
            return;
        }

        if (!validateRenderOutputPath(restoredOutput, kAllowedExts, recordingHasWebcam))
            continue;

        break; // valid extension and no collision with inputs
    }

    // ── Resolution choice — identical to renderAgain() ────────────────────
    promptRenderResolution();
    qDebug() << "Restored session will render at:" << setRez;

    // ── PreviewDialog — volume / offset / pitch adjustment ────────────────
    outputFilePath = restoredOutput;

    showPreviewAndRender([this]() {
        trySetState(State::Idle);
        logUI("Library: restore cancelled during preview/adjustment.");
        enable_playback(true);
        chooseInputButton->setEnabled(true);
        chooseInputAction->setEnabled(true);
        singButton->setEnabled(false);
        singAction->setEnabled(false);
        QMessageBox::warning(this, "Restore Cancelled",
            "Session restore cancelled during volume adjustment.\n"
            "Open the Library again to try once more.");
    });
}
