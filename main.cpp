#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

#ifdef __linux__
#include <glib-2.0/glib.h> 
#include <cstring>         

// Custom log handler to suppress specific messages caused by strange Qt multimedia and GStreamer behaviour
void my_log_handler(const gchar *log_domain,
                    GLogLevelFlags log_level,
                    const gchar *message,
                    gpointer user_data) {

    // Will suppress critical messages that contain "g_object_unref"

    if (log_level & G_LOG_LEVEL_CRITICAL && message != nullptr) {
        if (strstr(message, "g_object_unref") != nullptr) {
            return; // Do nothing for messages containing "g_object_unref"
        }
    }

    // *do* Print all other messages!
    g_log_default_handler(log_domain, log_level, message, user_data);

}
#endif

////////// DARK MODE cfg detection //////////
#ifdef Q_OS_WIN
#include <QSettings>
bool isDarkModeEnabled() {
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", QSettings::NativeFormat);
    return settings.value("AppsUseLightTheme", 1).toInt() == 0; // 0 means dark mode
}
#endif

#ifdef Q_OS_MAC
#include <QProcess>
bool isDarkModeEnabled() {
    QProcess process;
    process.start("defaults read -g AppleInterfaceStyle");
    process.waitForFinished();
    return process.exitCode() == 0 && process.readAllStandardOutput().trimmed() == "Dark"; // Check output for dark mode
}
#endif

#ifdef Q_OS_LINUX
#include <QProcess>
bool isDarkModeEnabled() {
    QProcess process;
    // Ensure the process inherits the environment variables
    process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process.start("gsettings", QStringList() << "get" << "org.gnome.desktop.interface" << "color-scheme");
    process.waitForFinished();
    QString output = process.readAllStandardOutput().trimmed();
    return output.contains("dark");
}
#endif
///////////////////////////////////////////////

int main(int argc, char *argv[]) {

#ifdef __linux__
    // Set our custom log handler
    g_log_set_default_handler(my_log_handler, NULL);
    
     // "wayland" has issues with Ubuntu 24.04  and below, we can force xcb
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif

#ifdef Q_OS_WIN
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
#endif

    qputenv("QT_FFMPEG_BLACKLIST_CODECS",
           "hevc,hevc_vaapi,hevc_vulkan,vp8_vaapi");

    // (opt) -- more stable, less performance
    // setenv("QT_FFMPEG_FORCE_SOFTWARE_ENCODERS", "1", 1);

    QApplication WakkaQt(argc, argv);

    WakkaQt.setWindowIcon(QIcon(":/images/icon.png"));
    //WakkaQt.setStyle(QStyleFactory::create("Fusion"));

    // If enabled on the OS, Switch to dark mode on supported platforms (Windows/Mac/GNOME)
    //if ( isDarkModeEnabled() ) {
    if ( 0 ) {
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
        darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::black);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::gray);
        darkPalette.setColor(QPalette::PlaceholderText, Qt::gray); 
        darkPalette.setColor(QPalette::BrightText, Qt::red);

        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);

        WakkaQt.setPalette(darkPalette);
    }

    MainWindow w;
    w.show();
    return WakkaQt.exec();
}

