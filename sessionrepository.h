#ifndef SESSIONREPOSITORY_H
#define SESSIONREPOSITORY_H

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

// Bundles the genuinely session-specific data — used both as saveSession()'s
// input and restoreSession()'s output. webcamRecorded/audioRecorded/
// extractedTmpPlayback/tunedRecorded are deliberately NOT part of this: they
// are fixed /tmp paths declared as globals in complexes.h, never session-
// specific values — saveSession()/restoreSession() read/write those globals'
// paths directly instead of threading them through here.
struct SessionSnapshot {
    QString currentVideoFile;
    QString currentVideoName;
    qint64  audioOffset = 0;
    qint64  videoOffset = 0;
    qint64  sysOffset = 0;
};

struct SaveResult {
    bool    ok = false;
    QString sessionId; // empty if !ok
    QString error;     // human-readable reason if !ok
};

struct RestoreResult {
    bool            ok = false;
    QString         error;
    SessionSnapshot snapshot;
    // Whether *this session* actually has a webcam recording — ground-truthed
    // against the session folder's own webcam.mkv, not any caller state — so
    // restore/render can tell an audio-only session apart from a
    // currently-connected camera that has nothing to do with it.
    bool            hasWebcam = false;
};

struct OperationResult {
    bool    ok = false;
    QString error;
};

class SessionRepository
{
public:
    SessionRepository();

    // Returns the library root: ~/.WakkaQt/library/
    static QString libraryRoot();

    // Save current session files into a new library entry.
    SaveResult saveSession(const SessionSnapshot &snapshot);

    // Load all sessions from the library directory, sorted newest first.
    QList<SessionEntry> loadAll();

    // Delete a session by id.
    OperationResult deleteSession(const QString &id);

    // Rename/relabel a session.
    OperationResult renameSession(const QString &id, const QString &newLabel);

    // Load a single session's metadata.
    SessionEntry loadEntry(const QString &id);

    // Restore session files back to the tmp paths.
    RestoreResult restoreSession(const QString &id);

private:
    QString metaPath(const QString &sessionDir);
    bool writeMetadata(const SessionEntry &entry);
    SessionEntry readMetadata(const QString &sessionDir);
    bool copyFile(const QString &src, const QString &dst);
    // Guards against path traversal via a malformed/tampered id (e.g. "../../etc")
    // reaching QDir/QFile calls built from libraryRoot() + "/" + id. Session ids
    // are always QUuid-generated internally, but callers (Library UI, restore
    // flows) pass them back as plain strings with no enforcement otherwise.
    static bool isValidSessionId(const QString &id);
};

#endif // SESSIONREPOSITORY_H
