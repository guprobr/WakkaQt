#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication WakkaQt(argc, argv);
    MainWindow w;
    w.show();
    return WakkaQt.exec();
}

