#include "mainwindow.h"
#include "Logger.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QPalette>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTime>

// Message handler — turns raw qDebug/qWarning/qCritical/qFatal traffic into
// a timestamped, tagged, colorized (and occasionally comical) line for the
// on-screen log widget. See MainWindow::logUI.
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    static const QString timeColor = "#8a8a8a";
    static const QString tagColor  = "#4a6fa5";

    QString typeColor;
    QString emoji;
    switch (type) {
    case QtDebugMsg:
        typeColor = "#888888";
        emoji = "\xF0\x9F\x94\xA7";     // 🔧
        break;
    case QtInfoMsg:
        typeColor = "#2f86eb";
        emoji = "\xE2\x84\xB9\xEF\xB8\x8F"; // ℹ️
        break;
    case QtWarningMsg:
        typeColor = "#d99a00";
        emoji = "\xE2\x9A\xA0\xEF\xB8\x8F"; // ⚠️
        break;
    case QtCriticalMsg:
        typeColor = "#e5393a";
        emoji = "\xF0\x9F\x94\xA5";     // 🔥
        break;
    case QtFatalMsg:
        typeColor = "#b71c1c";
        emoji = "\xF0\x9F\x92\x80";     // 💀
        break;
    }

    // Cute emoji roulette for the "everything's fine" chatter, so the log
    // doesn't read like a wall of the same wrench forever.
    if (type == QtDebugMsg) {
        static const char *debugEmojis[] = {
            "\xF0\x9F\x94\xA7", // 🔧
            "\xF0\x9F\x90\x9B", // 🐛
            "\xF0\x9F\x93\x9D", // 📝
            "\xF0\x9F\x8E\xB6", // 🎶
        };
        emoji = QString::fromUtf8(debugEmojis[qHash(msg) % 4]);
    }

    QString tag = "WakkaQt";
    if (context.file) {
        const QString fileTag = QFileInfo(QString::fromUtf8(context.file)).completeBaseName();
        if (!fileTag.isEmpty())
            tag = fileTag;
    }

    const QString timestamp = QTime::currentTime().toString("HH:mm:ss.zzz");

    const QString formatted = QStringLiteral(
        "<span style=\"color:%1\">[%2]</span> "
        "<span style=\"color:%3;font-weight:bold\">[%4]</span> "
        "<span style=\"color:%5\">%6 %7</span>"
    ).arg(timeColor, timestamp, tagColor, tag, typeColor, emoji, msg.toHtmlEscaped());

    Logger::instance().logMessage(formatted);
}

int main(int argc, char *argv[]) {

#ifdef __linux__
     // "wayland" has issues with Ubuntu 24.04  and below, we can force xcb
    //qputenv("QT_QPA_PLATFORM", "xcb");
#endif

#ifdef Q_OS_WIN
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
#endif

    // Webcam recordings are baseline MJPEG in 4:4:4 chroma (yuvj444p), a
    // combination VAAPI/CUDA hwaccel decoders on this FFmpeg backend can't
    // initialize ("Failed to upload decode parameters: invalid parameter"),
    // which spams errors and leaves the video preview blank. Must be set
    // before QApplication/the FFmpeg plugin initializes — setting it later
    // (e.g. right before QMediaPlayer::setSource) has no effect, it's read
    // once at plugin load.
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "");

    QApplication WakkaQt(argc, argv);

    // frei0r-backed video effects (Vertigo, ...) need FREI0R_PATH pointing at
    // the plugin directory — ffmpeg's frei0r wrapper does not auto-discover
    // per-distro/per-OS install locations, only a handful of hardcoded ones.
    // Only set it if the user/environment hasn't already, and only to a
    // directory that actually exists, so this is a no-op on machines without
    // frei0r-plugins installed (those effects are just hidden from the UI —
    // see VideoEffectProcessor::isChainAvailable). Needs QApplication to
    // already exist (applicationDirPath() requires it), so this can't run
    // any earlier than here; it's still well before the first QMediaPlayer
    // is constructed in MainWindow, which is what actually matters.
    if (qEnvironmentVariableIsEmpty("FREI0R_PATH")) {
#if defined(Q_OS_WIN)
        // Prebuilt Windows binaries are "no setup required" — the expected
        // path is a "frei0r-1" folder bundled next to WakkaQt.exe. The
        // Program Files locations cover a standalone frei0r-plugins install
        // (e.g. via MSYS2's mingw-w64-x86_64-frei0r-plugins package copied
        // there manually), and the Shotcut path reuses that app's bundled
        // copy if the user happens to have it installed already.
        const QStringList candidates = {
            QCoreApplication::applicationDirPath() + "/frei0r-1",
            qEnvironmentVariable("ProgramFiles(x86)") + "/frei0r-1",
            qEnvironmentVariable("ProgramFiles") + "/frei0r-1",
            qEnvironmentVariable("ProgramFiles") + "/Shotcut/lib/frei0r-1",
        };
#elif defined(Q_OS_MACOS)
        const QStringList candidates = {
            QCoreApplication::applicationDirPath() + "/frei0r-1",
            "/opt/homebrew/lib/frei0r-1",
            "/usr/local/lib/frei0r-1",
            "/Applications/Shotcut.app/Contents/lib/frei0r-1",
        };
#else
        const QStringList candidates = {
            "/usr/lib/x86_64-linux-gnu/frei0r-1",
            "/usr/lib/aarch64-linux-gnu/frei0r-1",
            "/usr/lib64/frei0r-1",
            "/usr/lib/frei0r-1",
            "/usr/local/lib/frei0r-1",
        };
#endif
        // A single directory, not a joined list: ffmpeg's frei0r loader
        // splits FREI0R_PATH on ':', which would misparse a Windows drive
        // letter (e.g. "C:\...") if we joined multiple candidates. One
        // match is all we need.
        for (const QString &dir : candidates) {
            if (QDir(dir).exists()) {
                qputenv("FREI0R_PATH", dir.toUtf8());
                break;
            }
        }
    }

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


