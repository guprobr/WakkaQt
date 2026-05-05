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

    auto *progLayout = new QVBoxLayout(progDlg);
    progLayout->addWidget(progLbl);
    progLayout->addWidget(progBar);
    progDlg->setLayout(progLayout);
    progDlg->show();

    // --- Step 3: run separation in background thread ---
    const QString inputFile = currentPlayback;

    // shared_ptr<atomic> allows safe cross-thread progress reporting
    auto progress = std::make_shared<std::atomic<int>>(0);

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
            QMessageBox::critical(this, "Separation Failed",
                                  "Could not generate the backing track:\n" + err);
            return;
        }

        // --- Step 4: ask user where to save ---
        const QString savePath = QFileDialog::getSaveFileName(
            this,
            "Save Backing Track",
            QDir::homePath() + "/" + QFileInfo(inputFile).completeBaseName() + "_instrumental.wav",
            "WAV Files (*.wav);;MP3 Files (*.mp3)");

        if (savePath.isEmpty()) {
            QFile::remove(tempOut);
            return;
        }

        bool saveOk = true;
        if (savePath.endsWith(".mp3", Qt::CaseInsensitive)) {
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

    QFuture<Result> future = QtConcurrent::run([inputFile, progress]() -> Result {
        QString err;
        QString path = VocalSeparator::separate(inputFile, [&](int pct) {
            progress->store(pct);
        }, err);
        return {path, err};
    });

    watcher->setFuture(future);
}
