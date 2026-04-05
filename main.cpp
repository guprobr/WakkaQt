#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

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
  
    MainWindow w;
    w.show();
    return WakkaQt.exec();
}

