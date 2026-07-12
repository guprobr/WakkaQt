#include "mainwindow.h"
#include "Logger.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

// Message handler
void messageHandler(QtMsgType, const QMessageLogContext &, const QString &msg) {
    Logger::instance().logMessage(msg);
}

int main(int argc, char *argv[]) {

#ifdef __linux__
     // "wayland" has issues with Ubuntu 24.04  and below, we can force xcb
    //qputenv("QT_QPA_PLATFORM", "xcb");
#endif

#ifdef Q_OS_WIN
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
#endif

    QApplication WakkaQt(argc, argv);

    WakkaQt.setWindowIcon(QIcon(":/images/icon.png"));

    QLockFile lockFile(QDir::tempPath() + "/WakkaQt.lock");
    if (!lockFile.tryLock(100)) {
        QMessageBox::warning(
            nullptr,
            "WakkaQt is already running",
            "Another instance of WakkaQt is already running.\n\n"
            "Please close or kill the existing WakkaQt process before starting a new one."
        );
        return 1;
    }

    qInstallMessageHandler(messageHandler);

    MainWindow w;
    // Connect logger to UI log method
    QObject::connect(&Logger::instance(), &Logger::newMessage,
                     &w, &MainWindow::logUI);

    w.show();
    return WakkaQt.exec();
}


