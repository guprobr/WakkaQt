#include "mainwindow.h"
#include "vocalseparator.h"
#include "vocalseparationjob.h"

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>

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
    if (!VocalSeparator::modelExists()) {
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

        QDialog dlDlg(this);
        dlDlg.setWindowTitle("Downloading MDX-Net model…");
        dlDlg.setModal(true);
        dlDlg.setMinimumWidth(420);

        auto *dlLbl = new QLabel("Downloading UVR-MDX-NET-Inst_HQ_3.onnx from GitHub…", &dlDlg);
        dlLbl->setAlignment(Qt::AlignCenter);
        auto *dlBar = new QProgressBar(&dlDlg);
        dlBar->setRange(0, 100);

        auto *dlLayout = new QVBoxLayout(&dlDlg);
        dlLayout->addWidget(dlLbl);
        dlLayout->addWidget(dlBar);
        dlDlg.setLayout(dlLayout);
        dlDlg.show();

        QString dlErr;
        bool ok = VocalSeparator::downloadModel([&](int pct) {
            dlBar->setValue(pct);
            QApplication::processEvents();
        }, dlErr);

        dlDlg.accept();

        if (!ok) {
            trySetState(State::Idle);
            QMessageBox::critical(this, "Download Failed",
                                  "Could not download the model:\n" + dlErr +
                                  "\n\nCheck your internet connection and try again.");
            return;
        }

        logUI("MDX-Net model downloaded to " + VocalSeparator::modelPath());
    }

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
                QFile::remove(tempOut);
                return;
            }
            savePath = saveDlg.selectedFiles().value(0);
            if (savePath.isEmpty()) {
                trySetState(State::Idle);
                QFile::remove(tempOut);
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

        // WAV: fast rename/copy — no progress dialog or job needed
        if (!saveAsVideo && !saveAsMp3) {
            if (QFile::exists(savePath))
                QFile::remove(savePath);
            if (!QFile::rename(tempOut, savePath)) {
                if (QFile::copy(tempOut, savePath)) {
                    QFile::remove(tempOut);
                } else {
                    trySetState(State::Idle);
                    QMessageBox::critical(this, "Save Failed",
                                          "Could not write to:\n" + savePath +
                                          "\n\nTemp file preserved at:\n" + tempOut);
                    return;
                }
            }
            trySetState(State::Idle);
            logUI("Backing track saved: " + savePath);
            QMessageBox::information(this, "Done",
                                     "Backing track saved to:\n" + savePath);
            return;
        }

        // Video mux or MP3 encode: run in background via the job, with a progress dialog
        auto *saveProgDlg = new QDialog(this);
        saveProgDlg->setWindowTitle("Saving Backing Track");
        saveProgDlg->setModal(true);
        saveProgDlg->setMinimumWidth(340);
        // No cancellation for mux/encode — disable close button so the user
        // can't accidentally discard the operation mid-way.
        saveProgDlg->setWindowFlags(saveProgDlg->windowFlags() & ~Qt::WindowCloseButtonHint);

        auto *saveProgLbl = new QLabel(
            saveAsVideo ? "Muxing audio into video…" : "Encoding MP3…", saveProgDlg);
        saveProgLbl->setAlignment(Qt::AlignCenter);

        auto *saveProgBar = new QProgressBar(saveProgDlg);
#ifdef WAKKAQT_FFMPEG_NATIVE
        saveProgBar->setRange(0, 100);
#else
        saveProgBar->setRange(0, 0); // indeterminate — QProcess gives no sub-step progress
#endif

        auto *saveLayout = new QVBoxLayout(saveProgDlg);
        saveLayout->addWidget(saveProgLbl);
        saveLayout->addWidget(saveProgBar);
        saveProgDlg->setLayout(saveLayout);
        saveProgDlg->show();

        QPointer<QDialog> saveProgDlgGuard(saveProgDlg);

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
                [this, saveProgDlgGuard](QString err) {
            if (saveProgDlgGuard) {
                saveProgDlgGuard->accept();
                saveProgDlgGuard->deleteLater();
            }
            trySetState(State::Idle);
            QMessageBox::critical(this, "Export Failed", err);
        });

        VocalSeparationJob::ExportParams exportParams;
        exportParams.tempWavPath = tempOut;
        exportParams.inputFile = inputFile;
        exportParams.savePath = savePath;
        exportParams.saveAsVideo = saveAsVideo;
        m_separationJob->exportResult(exportParams);
    });

    m_separationJob->separate(inputFile);
}
