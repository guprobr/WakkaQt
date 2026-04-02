#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QDir>
#include <QJsonObject>

struct SessionEntry {
    QString id;           // UUID-based folder name
    QString label;        // user-friendly name (defaults to song title + timestamp)
    QDateTime savedAt;
    QString playbackFile; // original path of karaoke video
    QString playbackName; // song/video title
    QString sessionDir;   // full path to ~/.WakkaQt/library/<id>/
    bool hasWebcam;
    bool hasAudio;
};

class SessionManager
{
public:
    SessionManager();

    // Returns the library root: ~/.WakkaQt/library/
    static QString libraryRoot();

    // Save current session files into a new library entry.
    // Returns the session id on success, or empty string on failure.
    QString saveSession(
        const QString &webcamRecorded,
        const QString &audioRecorded,
        const QString &tunedRecorded,
        const QString &extractedTmpPlayback,
        const QString &currentVideoFile,
        const QString &currentVideoName,
        qint64 audioOffset,
        qint64 videoOffset,
        qint64 sysOffset
    );

    // Load all sessions from the library directory, sorted newest first.
    QList<SessionEntry> loadAll();

    // Delete a session by id.
    bool deleteSession(const QString &id);

    // Rename/relabel a session.
    bool renameSession(const QString &id, const QString &newLabel);

    // Load a single session's metadata.
    SessionEntry loadEntry(const QString &id);

    // Restore session files back to the tmp paths.
    // Returns true if successful.
    bool restoreSession(const QString &id,
                        QString &webcamRecorded,
                        QString &audioRecorded,
                        QString &tunedRecorded,
                        QString &extractedTmpPlayback,
                        QString &currentVideoFile,
                        QString &currentVideoName,
                        qint64 &audioOffset,
                        qint64 &videoOffset,
                        qint64 &sysOffset);

private:
    QString metaPath(const QString &sessionDir);
    bool writeMetadata(const SessionEntry &entry);
    SessionEntry readMetadata(const QString &sessionDir);
    bool copyFile(const QString &src, const QString &dst);
};

#endif // SESSIONMANAGER_H
