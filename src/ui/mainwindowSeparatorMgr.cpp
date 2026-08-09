#include "mainwindow.h"
#include "complexes.h" // mediaHasVideoStream()
#include "vocalseparator.h"
#include "vocalseparationjob.h"
#include "modeldownloadjob.h"

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QAbstractButton>
#include <QDesktopServices>
#include <QUrl>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <functional>

namespace {

struct ProgressDialogHandle {
    QPointer<QDialog> dialog;
    QProgressBar *bar = nullptr;
};

// Builds the "label + progress bar + Abort button" modal dialog shared by the
// model download, separation, and export steps below, and wires both the
// Abort button and the window's [X] close button to the same cancel callback.
ProgressDialogHandle makeAbortableProgressDialog(QWidget *parent, const QString &title,
                                                  const QString &message, int minWidth,
                                                  std::function<void()> onCancel)
{
    auto *dlg = new QDialog(parent);
    dlg->setWindowTitle(title);
    dlg->setModal(true);
    dlg->setMinimumWidth(minWidth);

    auto *label = new QLabel(message, dlg);
    label->setAlignment(Qt::AlignCenter);
    auto *bar = new QProgressBar(dlg);
    auto *cancelBtn = new QPushButton("Abort", dlg);

    auto *layout = new QVBoxLayout(dlg);
    layout->addWidget(label);
    layout->addWidget(bar);
    layout->addWidget(cancelBtn);
    dlg->setLayout(layout);
    dlg->show();

    QObject::connect(cancelBtn, &QPushButton::clicked, parent, onCancel);
    // Closing the window (X button) is equivalent to Abort
    QObject::connect(dlg, &QDialog::rejected, parent, onCancel);

    return { QPointer<QDialog>(dlg), bar };
}

} // namespace

void MainWindow::generateBackingTrack() {
    if (currentPlayback.isEmpty()) return;

    if (!trySetState(State::Separating))
        return;

    // Stop playback to free audio resources for the separation process
    vizPlayer->stop();

    // --- Step 1: ensure model is present, offer to download ---
    if (VocalSeparator::modelExists()) {
        runVocalSeparation();
        return;
    }

    auto btn = QMessageBox::question(
        this,
        "Download MDX-Net Model",
        "The UVR-MDX-NET-Inst_HQ_3 separation model (~80 MB) needs to be downloaded once.\n"
        "It will be stored in ~/.WakkaQt/models/ for future use.\n\n"
        "Proceed with download?",
        QMessageBox::Yes | QMessageBox::No);

    if (btn != QMessageBox::Yes) {
        trySetState(State::Idle);
        return;
    }

    if (m_modelDownloadJob) {
        m_modelDownloadJob->deleteLater();
        m_modelDownloadJob = nullptr;
    }
    m_modelDownloadJob = new ModelDownloadJob(this);

    auto dl = makeAbortableProgressDialog(this, "Downloading MDX-Net model…",
        "Downloading UVR-MDX-NET-Inst_HQ_3.onnx from GitHub…", 420,
        [this]() { if (m_modelDownloadJob) m_modelDownloadJob->cancel(); });
    dl.bar->setRange(0, 100);

    connect(m_modelDownloadJob, &ModelDownloadJob::progress, this, [bar = dl.bar](int pct) {
        if (pct < 0) return;
        bar->setValue(pct);
    });

    QPointer<QDialog> dlDlgGuard(dl.dialog);
    connect(m_modelDownloadJob, &ModelDownloadJob::finished, this,
            [this, dlDlgGuard](bool success, bool cancelled, QString errorMessage) {
        if (dlDlgGuard) {
            dlDlgGuard->accept();
            dlDlgGuard->deleteLater();
        }

        if (!success) {
            trySetState(State::Idle);
            if (cancelled) return;
            QMessageBox::critical(this, "Download Failed",
                                  "Could not download the model:\n" + errorMessage +
                                  "\n\nCheck your internet connection and try again.");
            return;
        }

        logUI("MDX-Net model downloaded to " + VocalSeparator::modelPath());
        runVocalSeparation();
    });

    m_modelDownloadJob->start(VocalSeparator::modelUrl(), VocalSeparator::modelPath(),
                              VocalSeparator::modelSha256());
}

// The separation+export flow proper — runs once the model is confirmed
// present, either immediately (generateBackingTrack() found it already
// downloaded) or from the download job's finished-signal handler above.
void MainWindow::runVocalSeparation() {
    // --- Step 2/3: run separation in the background via VocalSeparationJob ---
    const QString inputFile = currentPlayback;
    const bool inputHasVideo = mediaHasVideoStream(inputFile);

    if (m_separationJob) {
        m_separationJob->deleteLater();
        m_separationJob = nullptr;
    }
    m_separationJob = new VocalSeparationJob(this);

    auto prog = makeAbortableProgressDialog(this, "Generating Backing Track",
        "Separating vocals with UVR-MDX-NET-Inst_HQ_3…\n"
        "This may take a few minutes depending on song length.", 380,
        [this]() { if (m_separationJob) m_separationJob->cancelSeparate(); });
    prog.bar->setRange(0, 100);

    connect(m_separationJob, &VocalSeparationJob::separationProgress, this, [bar = prog.bar](int pct) {
        bar->setValue(pct);
    });

    // Guard with QPointer: if the user closed the dialog before the job
    // finished, progDlgGuard will still be non-null (dialog is just hidden,
    // not deleted), but using it explicitly signals our intent and is safe
    // even if WA_DeleteOnClose were ever added.
    QPointer<QDialog> progDlgGuard(prog.dialog);

    connect(m_separationJob, &VocalSeparationJob::separationFailed, this,
            [this, progDlgGuard](QString err, bool wasCancelled) {
        if (progDlgGuard) {
            progDlgGuard->accept();
            progDlgGuard->deleteLater();
        }
        trySetState(State::Idle);
        if (wasCancelled) return;
        QMessageBox::critical(this, "Separation Failed",
                              "Could not generate the backing track:\n" + err);
    });

    connect(m_separationJob, &VocalSeparationJob::separated, this,
            [this, progDlgGuard, inputFile, inputHasVideo](QString tempOut) {
        if (progDlgGuard) {
            progDlgGuard->accept();
            progDlgGuard->deleteLater();
        }

        // --- Step 4: ask user where to save ---
        const QString baseName = QFileInfo(inputFile).completeBaseName();
        QString defaultSavePath;
        QString saveFilter;
        if (inputHasVideo) {
            // Default to same container as input so video is preserved
            defaultSavePath = QDir::homePath() + "/" + baseName + "_instrumental.mp4";
            saveFilter = "MP4 Files (*.mp4);;MKV Files (*.mkv);;WAV Files (*.wav);;MP3 Files (*.mp3)";
        } else {
            defaultSavePath = QDir::homePath() + "/" + baseName + "_instrumental.wav";
            saveFilter = "WAV Files (*.wav);;MP3 Files (*.mp3)";
        }

        const QStringList allowedExts = inputHasVideo
            ? QStringList{"mp4", "mkv", "wav", "mp3"}
            : QStringList{"wav", "mp3"};

        QString savePath;
        while (true) {
            QFileDialog saveDlg(this, "Save Backing Track", defaultSavePath, saveFilter);
            saveDlg.setAcceptMode(QFileDialog::AcceptSave);
            saveDlg.setOption(QFileDialog::DontUseNativeDialog);
            QObject::connect(&saveDlg, &QFileDialog::filterSelected, &saveDlg,
                [&saveDlg](const QString &filter) {
                    int star = filter.lastIndexOf("*.");
                    if (star < 0) return;
                    QString ext = filter.mid(star + 2).section(')', 0, 0).trimmed().toLower();
                    if (ext.isEmpty()) return;
                    QStringList sel = saveDlg.selectedFiles();
                    if (sel.isEmpty()) return;
                    QFileInfo fi(sel.first());
                    if (fi.completeBaseName().isEmpty()) return;
                    saveDlg.selectFile(fi.dir().filePath(fi.completeBaseName() + "." + ext));
                });
            if (saveDlg.exec() != QDialog::Accepted) {
                trySetState(State::Idle);
                if (m_separationJob) m_separationJob->discardWorkspace();
                return;
            }
            savePath = saveDlg.selectedFiles().value(0);
            if (savePath.isEmpty()) {
                trySetState(State::Idle);
                if (m_separationJob) m_separationJob->discardWorkspace();
                return;
            }

            if (!allowedExts.contains(QFileInfo(savePath).suffix().toLower())) {
                QMessageBox::warning(this, "Invalid File Extension",
                    "Please save the backing track with one of these extensions:\n."
                    + allowedExts.join(", ."));
                defaultSavePath = savePath;
                continue;
            }

            if (QFileInfo(savePath).absoluteFilePath() ==
                    QFileInfo(inputFile).absoluteFilePath()) {
                QMessageBox::warning(this, "Invalid Output Path",
                    "The output file cannot overwrite the input file.\n"
                    "Please choose a different name or location.");
                defaultSavePath = savePath;
                continue;
            }

            break;
        }

        // Determine if user chose a video-capable container
        const bool saveAsVideo = inputHasVideo &&
                                 !savePath.endsWith(".wav", Qt::CaseInsensitive) &&
                                 !savePath.endsWith(".mp3", Qt::CaseInsensitive);
        const bool saveAsMp3 = savePath.endsWith(".mp3", Qt::CaseInsensitive);

        const ExportRecoveryContext ctx{tempOut, inputFile, savePath, saveAsVideo};

        // WAV: fast copy — no progress dialog or job needed. A failure here
        // gets the same recovery choices (Try Again / Save WAV / Open
        // Folder / Discard) as a mux/encode failure instead of just an
        // error box, since the expensive-to-recompute separated instrumental
        // is just as much at stake.
        if (!saveAsVideo && !saveAsMp3) {
            if (!finishWavSave(tempOut, savePath))
                handleExportFailure(ctx, "Could not write to:\n" + savePath);
            return;
        }

        // Video mux or MP3 encode: run in background via the job, with a progress dialog
        startExport(ctx);
    });

    m_separationJob->separate(inputFile);
}

// Streams tempOut into savePath via QSaveFile instead of remove()-then-
// rename(): QSaveFile::commit() replaces an existing file at savePath
// atomically at the OS level, so a failed copy or a commit failure never
// touches (or loses) a pre-existing valid file there — it just leaves it
// alone. On success, discards the job's workspace and returns to Idle; on
// failure, leaves both untouched (still Separating, workspace still intact)
// so the caller can offer recovery instead of losing the result.
//
// Deliberately not atomicfilecommit.h's commitPartialOverFinal() — see the
// comment at the top of that header for why: tempOut lives in this job's
// private /tmp workspace, which the user's chosen savePath may not share a
// filesystem with, so this needs an actual byte copy (what QSaveFile does)
// rather than a same-directory sidecar rename (what that helper does).
bool MainWindow::finishWavSave(const QString &tempOut, const QString &savePath)
{
    QFile src(tempOut);
    bool copyOk = src.open(QIODevice::ReadOnly);
    QSaveFile out(savePath);
    copyOk = copyOk && out.open(QIODevice::WriteOnly);
    if (copyOk) {
        char buf[64 * 1024];
        while (!src.atEnd()) {
            const qint64 n = src.read(buf, sizeof(buf));
            if (n < 0 || out.write(buf, n) != n) { copyOk = false; break; }
        }
    }
    src.close();
    if (!copyOk || !out.commit())
        return false;
    QFile::remove(tempOut);

    if (m_separationJob) m_separationJob->discardWorkspace();

    trySetState(State::Idle);
    logUI("Backing track saved: " + savePath);
    QMessageBox::information(this, "Done", "Backing track saved to:\n" + savePath);
    return true;
}

// Runs (or re-runs, for "Try Again") the mux/MP3-encode export via
// VocalSeparationJob. Disconnects any previous exportProgress/exported/
// exportFailed connections first — this can be called more than once on the
// same m_separationJob instance (retries from handleExportFailure()), and
// without disconnecting, each retry would stack another set of listeners
// that all fire on the next export.
void MainWindow::startExport(const ExportRecoveryContext &ctx)
{
    disconnect(m_separationJob, &VocalSeparationJob::exportProgress, this, nullptr);
    disconnect(m_separationJob, &VocalSeparationJob::exported, this, nullptr);
    disconnect(m_separationJob, &VocalSeparationJob::exportFailed, this, nullptr);

    auto save = makeAbortableProgressDialog(this, "Saving Backing Track",
        ctx.saveAsVideo ? "Muxing audio into video…" : "Encoding MP3…", 340,
        [this]() { if (m_separationJob) m_separationJob->cancelExport(); });
#ifdef WAKKAQT_FFMPEG_NATIVE
    save.bar->setRange(0, 100);
#else
    save.bar->setRange(0, 0); // indeterminate — QProcess gives no sub-step progress
#endif

    QPointer<QDialog> saveProgDlgGuard(save.dialog);

    connect(m_separationJob, &VocalSeparationJob::exportProgress, this,
            [bar = save.bar](int pct) {
        if (pct < 0) return; // indeterminate fallback path — range already (0,0)
        bar->setValue(pct);
    });

    connect(m_separationJob, &VocalSeparationJob::exported, this,
            [this, saveProgDlgGuard](QString destPath) {
        if (saveProgDlgGuard) {
            saveProgDlgGuard->accept();
            saveProgDlgGuard->deleteLater();
        }
        trySetState(State::Idle);
        logUI("Backing track saved: " + destPath);
        QMessageBox::information(this, "Done",
                                 "Backing track saved to:\n" + destPath);
    });

    connect(m_separationJob, &VocalSeparationJob::exportFailed, this,
            [this, saveProgDlgGuard, ctx](QString err) {
        if (saveProgDlgGuard) {
            saveProgDlgGuard->accept();
            saveProgDlgGuard->deleteLater();
        }
        if (err == "Cancelled") {
            trySetState(State::Idle);
            return;
        }
        handleExportFailure(ctx, err);
    });

    VocalSeparationJob::ExportParams exportParams;
    exportParams.tempWavPath = ctx.tempOut;
    exportParams.inputFile   = ctx.inputFile;
    exportParams.savePath    = ctx.savePath;
    exportParams.saveAsVideo = ctx.saveAsVideo;
    m_separationJob->exportResult(exportParams);
}

// The separated instrumental (ctx.tempOut) is the expensive-to-recompute
// result of the whole ONNX pass, and VocalSeparationJob deliberately never
// discards its workspace on export failure — so a failed save doesn't have
// to mean losing it. m_separationJob is only ever destroyed (by the next
// generateBackingTrack() call) after the user resolves this one way or
// another, since the Separating state stays held until then.
void MainWindow::handleExportFailure(const ExportRecoveryContext &ctx, const QString &errorMessage)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle("Export Failed");
    box.setText("Could not save the backing track:\n" + errorMessage);
    QPushButton *tryAgainBtn   = box.addButton("Try Again", QMessageBox::ActionRole);
    QPushButton *saveWavBtn    = box.addButton("Save as WAV", QMessageBox::ActionRole);
    QPushButton *openFolderBtn = box.addButton("Open Folder", QMessageBox::ActionRole);
    box.addButton("Discard", QMessageBox::DestructiveRole);
    box.setDefaultButton(tryAgainBtn);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == tryAgainBtn) {
        startExport(ctx);
        return;
    }
    if (clicked == saveWavBtn) {
        promptSaveInstrumentalAsWav(ctx, errorMessage);
        return;
    }
    if (clicked == openFolderBtn) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(ctx.tempOut).absolutePath()));
        // Opening the folder isn't itself a resolution — the user still
        // needs to pick what happens to the job/workspace next.
        handleExportFailure(ctx, errorMessage);
        return;
    }
    // "Discard", or the dialog was closed without picking a button — treat
    // a bare close the same as Discard rather than leaving the job stuck in
    // Separating with no dialog left to resolve it.
    if (m_separationJob) m_separationJob->discardWorkspace();
    trySetState(State::Idle);
}

// "Save WAV" recovery choice: skips mux/encode entirely and saves
// ctx.tempOut directly. Backing out of the save dialog re-shows
// handleExportFailure() (with the same original error) instead of silently
// abandoning the workspace, so the user always lands on an explicit choice.
void MainWindow::promptSaveInstrumentalAsWav(const ExportRecoveryContext &ctx, const QString &priorError)
{
    QString path;
    while (true) {
        QFileDialog saveDlg(this, "Save Backing Track as WAV",
                            QDir::homePath() + "/" + QFileInfo(ctx.inputFile).completeBaseName()
                                + "_instrumental.wav",
                            "WAV Files (*.wav)");
        saveDlg.setAcceptMode(QFileDialog::AcceptSave);
        saveDlg.setOption(QFileDialog::DontUseNativeDialog);
        saveDlg.setDefaultSuffix("wav");
        if (saveDlg.exec() != QDialog::Accepted) {
            handleExportFailure(ctx, priorError);
            return;
        }
        path = saveDlg.selectedFiles().value(0);
        if (path.isEmpty()) {
            handleExportFailure(ctx, priorError);
            return;
        }
        if (QFileInfo(path).absoluteFilePath() == QFileInfo(ctx.inputFile).absoluteFilePath()) {
            QMessageBox::warning(this, "Invalid Output Path",
                "The output file cannot overwrite the input file.\n"
                "Please choose a different name or location.");
            continue;
        }
        break;
    }

    if (!finishWavSave(ctx.tempOut, path)) {
        QMessageBox::critical(this, "Save Failed",
                              "Could not write to:\n" + path +
                              "\n\nThe separated instrumental is still available in:\n"
                              + QFileInfo(ctx.tempOut).absolutePath());
        handleExportFailure(ctx, priorError);
    }
}
