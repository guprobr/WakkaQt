#include "mainwindow.h"
#include "vocalseparator.h"

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QProcess>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <atomic>
#include <memory>

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

        if (btn != QMessageBox::Yes) return;

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

    // --- Step 3: run separation in background thread ---
    const QString inputFile = currentPlayback;
#ifdef WAKKAQT_FFMPEG_NATIVE
    const bool inputHasVideo = FFmpegNative::hasVideoStream(inputFile);
#else
    const bool inputHasVideo = hasVideoStream(inputFile);
#endif

    // shared_ptr<atomic> allows safe cross-thread progress reporting and cancellation
    auto progress  = std::make_shared<std::atomic<int>>(0);
    auto cancelled = std::make_shared<std::atomic<bool>>(false);

    connect(cancelBtn, &QPushButton::clicked, this, [cancelled]() {
        cancelled->store(true);
    });
    // Closing the window (X button) is equivalent to Abort
    connect(progDlg, &QDialog::rejected, this, [cancelled]() {
        cancelled->store(true);
    });

    // Poll the atomic from the main thread every 200 ms
    auto *pollTimer = new QTimer(this);
    pollTimer->start(200);
    connect(pollTimer, &QTimer::timeout, this, [progBar, progress]() {
        progBar->setValue(progress->load());
    });

    // Result: first = path to temp WAV (empty on error), second = error string
    using Result = QPair<QString, QString>;
    auto *watcher = new QFutureWatcher<Result>(this);

    // Guard with QPointer: if the user closed the dialog before the watcher
    // fires, progDlgGuard will still be non-null (dialog is just hidden, not
    // deleted), but using it explicitly signals our intent and is safe even if
    // WA_DeleteOnClose were ever added.
    QPointer<QDialog> progDlgGuard(progDlg);

    connect(watcher, &QFutureWatcher<Result>::finished, this, [=]() mutable {
        pollTimer->stop();
        pollTimer->deleteLater();

        Result res = watcher->result();
        watcher->deleteLater();

        if (progDlgGuard) {
            progDlgGuard->accept();
            progDlgGuard->deleteLater();
        }

        const QString &tempOut = res.first;
        const QString &err     = res.second;

        if (tempOut.isEmpty()) {
            if (err == "Cancelled") return;
            QMessageBox::critical(this, "Separation Failed",
                                  "Could not generate the backing track:\n" + err);
            return;
        }

        // --- Step 4: ask user where to save ---
        const QString baseName = QFileInfo(inputFile).completeBaseName();
        const QString origExt  = QFileInfo(inputFile).suffix();
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
                QFile::remove(tempOut);
                return;
            }
            savePath = saveDlg.selectedFiles().value(0);
            if (savePath.isEmpty()) {
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

        // WAV: fast rename/copy — no progress dialog needed
        if (!saveAsVideo && !saveAsMp3) {
            if (QFile::exists(savePath))
                QFile::remove(savePath);
            if (!QFile::rename(tempOut, savePath)) {
                if (QFile::copy(tempOut, savePath)) {
                    QFile::remove(tempOut);
                } else {
                    QMessageBox::critical(this, "Save Failed",
                                          "Could not write to:\n" + savePath +
                                          "\n\nTemp file preserved at:\n" + tempOut);
                    return;
                }
            }
            logUI("Backing track saved: " + savePath);
            QMessageBox::information(this, "Done",
                                     "Backing track saved to:\n" + savePath);
            return;
        }

        // Video mux or MP3 encode: run in background with progress dialog
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

        auto saveProgress = std::make_shared<std::atomic<int>>(0);

        auto *saveTimer = new QTimer(this);
        saveTimer->start(200);
        connect(saveTimer, &QTimer::timeout, this, [saveProgBar, saveProgress]() {
            saveProgBar->setValue(saveProgress->load());
        });

        using SaveResult = QPair<bool, QString>;
        auto *saveWatcher = new QFutureWatcher<SaveResult>(this);

        QPointer<QDialog> saveProgDlgGuard(saveProgDlg);
        connect(saveWatcher, &QFutureWatcher<SaveResult>::finished, this, [=]() {
            saveTimer->stop();
            saveTimer->deleteLater();
            const SaveResult res = saveWatcher->result();
            saveWatcher->deleteLater();
            if (saveProgDlgGuard) {
                saveProgDlgGuard->accept();
                saveProgDlgGuard->deleteLater();
            }

            if (res.first) {
                logUI("Backing track saved: " + savePath);
                QMessageBox::information(this, "Done",
                                         "Backing track saved to:\n" + savePath);
            } else {
                QMessageBox::critical(this, "Export Failed", res.second);
            }
        });

        QFuture<SaveResult> saveFuture = QtConcurrent::run([=]() -> SaveResult {
            if (saveAsVideo) {
#ifdef WAKKAQT_FFMPEG_NATIVE
                const bool ok = FFmpegNative::muxVideoWithAudio(inputFile, tempOut, savePath,
                    [saveProgress](int pct) { saveProgress->store(pct); });
                QFile::remove(tempOut);
                return {ok, ok ? QString()
                              : "Native video muxing failed. Check console debug log for details."};
#else
                QProcess mux;
                mux.start("ffmpeg", {"-y", "-i", inputFile, "-i", tempOut,
                                      "-c:v", "copy", "-c:a", "aac",
                                      "-map", "0:v:0", "-map", "1:a:0",
                                      savePath});
                mux.waitForFinished(300000);
                const bool ok = mux.exitCode() == 0;
                const QString err = ok ? QString()
                    : "ffmpeg video muxing failed.\n" + QString(mux.readAllStandardError()).left(300);
                QFile::remove(tempOut);
                return {ok, err};
#endif
            } else {
#ifdef WAKKAQT_FFMPEG_NATIVE
                const bool ok = FFmpegNative::transcodeAudio(tempOut, savePath,
                    [saveProgress](int pct) { saveProgress->store(pct); });
                QFile::remove(tempOut);
                return {ok, ok ? QString()
                              : "Native MP3 encoding failed. Check log for details."};
#else
                QProcess mp3;
                mp3.start("ffmpeg", {"-y", "-i", tempOut, "-q:a", "2", savePath});
                mp3.waitForFinished(120000);
                const bool ok = mp3.exitCode() == 0;
                const QString err = ok ? QString()
                    : "ffmpeg MP3 encoding failed.\n" + QString(mp3.readAllStandardError()).left(300);
                QFile::remove(tempOut);
                return {ok, err};
#endif
            }
        });
        saveWatcher->setFuture(saveFuture);
    });

    QFuture<Result> future = QtConcurrent::run([inputFile, progress, cancelled]() -> Result {
        QString err;
        QString path = VocalSeparator::separate(inputFile, [&](int pct) {
            progress->store(pct);
        }, err, cancelled.get());
        return {path, err};
    });

    watcher->setFuture(future);
}
