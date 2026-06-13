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

    // Poll the atomic from the main thread every 200 ms
    auto *pollTimer = new QTimer(this);
    pollTimer->start(200);
    connect(pollTimer, &QTimer::timeout, this, [progBar, progress]() {
        progBar->setValue(progress->load());
    });

    // Result: first = path to temp WAV (empty on error), second = error string
    using Result = QPair<QString, QString>;
    auto *watcher = new QFutureWatcher<Result>(this);

    connect(watcher, &QFutureWatcher<Result>::finished, this, [=]() mutable {
        pollTimer->stop();
        pollTimer->deleteLater();

        Result res = watcher->result();
        watcher->deleteLater();

        progDlg->accept();
        progDlg->deleteLater();

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
            defaultSavePath = QDir::homePath() + "/" + baseName + "_instrumental." + origExt;
            saveFilter = "MP4 Files (*.mp4);;MKV Files (*.mkv);;WAV Files (*.wav);;MP3 Files (*.mp3)";
        } else {
            defaultSavePath = QDir::homePath() + "/" + baseName + "_instrumental.wav";
            saveFilter = "WAV Files (*.wav);;MP3 Files (*.mp3)";
        }

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
        const QString savePath = saveDlg.selectedFiles().value(0);
        if (savePath.isEmpty()) {
            QFile::remove(tempOut);
            return;
        }

        // Determine if user chose a video-capable container
        const bool saveAsVideo = inputHasVideo &&
                                 !savePath.endsWith(".wav", Qt::CaseInsensitive) &&
                                 !savePath.endsWith(".mp3", Qt::CaseInsensitive);

        bool saveOk = true;
        if (saveAsVideo) {
#ifdef WAKKAQT_FFMPEG_NATIVE
            if (!FFmpegNative::muxVideoWithAudio(inputFile, tempOut, savePath)) {
                QMessageBox::critical(this, "Export Failed",
                                      "Native video muxing failed. Check log for details.");
                saveOk = false;
            }
            QFile::remove(tempOut);
#else
            QProcess mux;
            mux.start("ffmpeg", {"-y", "-i", inputFile, "-i", tempOut,
                                  "-c:v", "copy", "-c:a", "aac",
                                  "-map", "0:v:0", "-map", "1:a:0",
                                  savePath});
            mux.waitForFinished(300000);
            QFile::remove(tempOut);
            if (mux.exitCode() != 0) {
                QMessageBox::critical(this, "Export Failed",
                                      "ffmpeg video muxing failed.\n" +
                                      QString(mux.readAllStandardError()).left(300));
                saveOk = false;
            }
#endif
        } else if (savePath.endsWith(".mp3", Qt::CaseInsensitive)) {
#ifdef WAKKAQT_FFMPEG_NATIVE
            if (!FFmpegNative::transcodeAudio(tempOut, savePath)) {
                QMessageBox::critical(this, "Export Failed",
                                      "Native MP3 encoding failed. Check log for details.");
                saveOk = false;
            }
            QFile::remove(tempOut);
#else
            QProcess mp3;
            mp3.start("ffmpeg", {"-y", "-i", tempOut, "-q:a", "2", savePath});
            mp3.waitForFinished(120000);
            QFile::remove(tempOut);
            if (mp3.exitCode() != 0) {
                QMessageBox::critical(this, "Export Failed",
                                      "ffmpeg MP3 encoding failed.\n" +
                                      QString(mp3.readAllStandardError()).left(300));
                saveOk = false;
            }
#endif
        } else {
            if (QFile::exists(savePath))
                QFile::remove(savePath);
            if (!QFile::rename(tempOut, savePath)) {
                if (QFile::copy(tempOut, savePath)) {
                    QFile::remove(tempOut);
                } else {
                    QMessageBox::critical(this, "Save Failed",
                                          "Could not write to:\n" + savePath +
                                          "\n\nTemp file preserved at:\n" + tempOut);
                    saveOk = false;
                }
            }
        }

        if (saveOk) {
            logUI("Backing track saved: " + savePath);
            QMessageBox::information(this, "Done",
                                     "Backing track saved to:\n" + savePath);
        }
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
