#include "mainwindow.h"
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

#ifdef WAKKAQT_FFMPEG_NATIVE
#  include "ffmpegnative.h"
#else
static bool hasVideoStream(const QString &filePath) {
    QProcess p;
    p.start("ffprobe", {"-v", "quiet", "-select_streams", "v:0",
                        "-show_entries", "stream=codec_type",
                        "-of", "default=noprint_wrappers=1:nokey=1", filePath});
    p.waitForFinished(10000);
    return p.exitCode() == 0 && !p.readAllStandardOutput().trimmed().isEmpty();
}
#endif

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

    auto *dlDlg = new QDialog(this);
    dlDlg->setWindowTitle("Downloading MDX-Net model…");
    dlDlg->setModal(true);
    dlDlg->setMinimumWidth(420);

    auto *dlLbl = new QLabel("Downloading UVR-MDX-NET-Inst_HQ_3.onnx from GitHub…", dlDlg);
    dlLbl->setAlignment(Qt::AlignCenter);
    auto *dlBar = new QProgressBar(dlDlg);
    dlBar->setRange(0, 100);
    auto *dlCancelBtn = new QPushButton("Abort", dlDlg);

    auto *dlLayout = new QVBoxLayout(dlDlg);
    dlLayout->addWidget(dlLbl);
    dlLayout->addWidget(dlBar);
    dlLayout->addWidget(dlCancelBtn);
    dlDlg->setLayout(dlLayout);
    dlDlg->show();

    if (m_modelDownloadJob) {
        m_modelDownloadJob->deleteLater();
        m_modelDownloadJob = nullptr;
    }
    m_modelDownloadJob = new ModelDownloadJob(this);

    connect(dlCancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_modelDownloadJob) m_modelDownloadJob->cancel();
    });
    connect(dlDlg, &QDialog::rejected, this, [this]() {
        if (m_modelDownloadJob) m_modelDownloadJob->cancel();
    });

    connect(m_modelDownloadJob, &ModelDownloadJob::progress, this, [dlBar](int pct) {
        if (pct < 0) return;
        dlBar->setValue(pct);
    });

    QPointer<QDialog> dlDlgGuard(dlDlg);
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
    // --- Step 2: progress dialog for separation ---
    auto *progDlg = new QDialog(this);
    progDlg->setWindowTitle("Generating Backing Track");
    progDlg->setModal(true);
    progDlg->setMinimumWidth(380);

    auto *progLbl = new QLabel(
        "Separating vocals with UVR-MDX-NET-Inst_HQ_3…\n"
        "This may take a few minutes depending on song length.", progDlg);
    progLbl->setAlignment(Qt::AlignCenter);

    auto *progBar = new QProgressBar(progDlg);
    progBar->setRange(0, 100);

    auto *cancelBtn = new QPushButton("Abort", progDlg);

    auto *progLayout = new QVBoxLayout(progDlg);
    progLayout->addWidget(progLbl);
    progLayout->addWidget(progBar);
    progLayout->addWidget(cancelBtn);
    progDlg->setLayout(progLayout);
    progDlg->show();

    // --- Step 3: run separation in the background via VocalSeparationJob ---
    const QString inputFile = currentPlayback;
#ifdef WAKKAQT_FFMPEG_NATIVE
    const bool inputHasVideo = FFmpegNative::hasVideoStream(inputFile);
#else
    const bool inputHasVideo = hasVideoStream(inputFile);
#endif

    if (m_separationJob) {
        m_separationJob->deleteLater();
        m_separationJob = nullptr;
    }
    m_separationJob = new VocalSeparationJob(this);

    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_separationJob) m_separationJob->cancelSeparate();
    });
    // Closing the window (X button) is equivalent to Abort
    connect(progDlg, &QDialog::rejected, this, [this]() {
        if (m_separationJob) m_separationJob->cancelSeparate();
    });

    connect(m_separationJob, &VocalSeparationJob::separationProgress, this, [progBar](int pct) {
        progBar->setValue(pct);
    });

    // Guard with QPointer: if the user closed the dialog before the job
    // finished, progDlgGuard will still be non-null (dialog is just hidden,
    // not deleted), but using it explicitly signals our intent and is safe
    // even if WA_DeleteOnClose were ever added.
    QPointer<QDialog> progDlgGuard(progDlg);

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

    auto *saveProgDlg = new QDialog(this);
    saveProgDlg->setWindowTitle("Saving Backing Track");
    saveProgDlg->setModal(true);
    saveProgDlg->setMinimumWidth(340);

    auto *saveProgLbl = new QLabel(
        ctx.saveAsVideo ? "Muxing audio into video…" : "Encoding MP3…", saveProgDlg);
    saveProgLbl->setAlignment(Qt::AlignCenter);

    auto *saveProgBar = new QProgressBar(saveProgDlg);
#ifdef WAKKAQT_FFMPEG_NATIVE
    saveProgBar->setRange(0, 100);
#else
    saveProgBar->setRange(0, 0); // indeterminate — QProcess gives no sub-step progress
#endif

    auto *saveCancelBtn = new QPushButton("Abort", saveProgDlg);

    auto *saveLayout = new QVBoxLayout(saveProgDlg);
    saveLayout->addWidget(saveProgLbl);
    saveLayout->addWidget(saveProgBar);
    saveLayout->addWidget(saveCancelBtn);
    saveProgDlg->setLayout(saveLayout);
    saveProgDlg->show();

    QPointer<QDialog> saveProgDlgGuard(saveProgDlg);

    connect(saveCancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_separationJob) m_separationJob->cancelExport();
    });
    // Closing the window (X button) is equivalent to Abort
    connect(saveProgDlg, &QDialog::rejected, this, [this]() {
        if (m_separationJob) m_separationJob->cancelExport();
    });

    connect(m_separationJob, &VocalSeparationJob::exportProgress, this,
            [saveProgBar](int pct) {
        if (pct < 0) return; // indeterminate fallback path — range already (0,0)
        saveProgBar->setValue(pct);
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
