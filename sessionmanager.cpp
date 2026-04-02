#include "sessionmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QDateTime>
#include <QDebug>
#include <algorithm>

SessionManager::SessionManager() {}

QString SessionManager::libraryRoot()
{
    return QDir::homePath() + "/.WakkaQt/library";
}

QString SessionManager::metaPath(const QString &sessionDir)
{
    return sessionDir + "/session.json";
}

bool SessionManager::copyFile(const QString &src, const QString &dst)
{
    if (src.isEmpty() || !QFile::exists(src)) {
        qWarning() << "SessionManager::copyFile: source does not exist:" << src;
        return false;
    }
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        qWarning() << "SessionManager::copyFile: cannot remove existing dst:" << dst;
        return false;
    }
    if (!QFile::copy(src, dst)) {
        qWarning() << "SessionManager::copyFile: failed to copy" << src << "->" << dst;
        return false;
    }
    return true;
}

// ── saveSession ───────────────────────────────────────────────────────────────
QString SessionManager::saveSession(
    const QString &webcamRecorded,
    const QString &audioRecorded,
    const QString &tunedRecorded,
    const QString &extractedTmpPlayback,
    const QString &currentVideoFile,
    const QString &currentVideoName,
    qint64 audioOffset,
    qint64 videoOffset,
    qint64 sysOffset)
{
    QDir dir;
    if (!dir.mkpath(libraryRoot())) {
        qWarning() << "SessionManager: cannot create library root" << libraryRoot();
        return {};
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    const QString sessionDir = libraryRoot() + "/" + id;
    if (!dir.mkpath(sessionDir)) {
        qWarning() << "SessionManager: cannot create session dir" << sessionDir;
        return {};
    }

    const bool hasWebcam      = QFile::exists(webcamRecorded)
                             && QFileInfo(webcamRecorded).size() > 0;
    const bool hasAudio       = QFile::exists(audioRecorded)
                             && QFileInfo(audioRecorded).size() > 0;
    const bool hasTuned       = QFile::exists(tunedRecorded)
                             && QFileInfo(tunedRecorded).size() > 0;
    const bool hasPlaybackWav = QFile::exists(extractedTmpPlayback)
                             && QFileInfo(extractedTmpPlayback).size() > 0;

    if (hasWebcam)      copyFile(webcamRecorded,      sessionDir + "/webcam.mkv");
    if (hasAudio)       copyFile(audioRecorded,        sessionDir + "/audio.wav");
    if (hasTuned)       copyFile(tunedRecorded,        sessionDir + "/tuned.wav");
    if (hasPlaybackWav) copyFile(extractedTmpPlayback, sessionDir + "/playback.wav");

    // offsets.json — store qint64 as strings to avoid double precision loss
    {
        QJsonObject off;
        off["audioOffset"]  = QString::number(audioOffset);
        off["videoOffset"]  = QString::number(videoOffset);
        off["sysOffset"]    = QString::number(sysOffset);
        off["playbackFile"] = currentVideoFile;
        off["playbackName"] = currentVideoName;
        QFile f(sessionDir + "/offsets.json");
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(off).toJson());
        else
            qWarning() << "SessionManager: cannot write offsets.json";
    }

    // session.json — display metadata
    {
        const QString label = currentVideoName
                            + QString::fromUtf8(" \xe2\x80\x94 ")   // em dash
                            + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm");
        QJsonObject obj;
        obj["id"]          = id;
        obj["label"]       = label;
        obj["savedAt"]     = QDateTime::currentDateTime().toString(Qt::ISODate);
        obj["playbackFile"]= currentVideoFile;
        obj["playbackName"]= currentVideoName;
        obj["hasWebcam"]   = hasWebcam;
        obj["hasAudio"]    = hasAudio;

        QFile f(sessionDir + "/session.json");
        if (!f.open(QIODevice::WriteOnly))
            qWarning() << "SessionManager: cannot write session.json";
        else
            f.write(QJsonDocument(obj).toJson());
    }

    qDebug() << "SessionManager: saved session" << id << "for" << currentVideoName;
    return id;
}

// ── readMetadata ──────────────────────────────────────────────────────────────
SessionEntry SessionManager::readMetadata(const QString &sessionDir)
{
    SessionEntry entry;
    entry.sessionDir = sessionDir;

    QFile f(sessionDir + "/session.json");
    if (!f.open(QIODevice::ReadOnly)) return entry;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull() || !doc.isObject()) return entry;

    const QJsonObject obj = doc.object();
    entry.id           = obj["id"].toString();
    entry.label        = obj["label"].toString();
    entry.savedAt      = QDateTime::fromString(obj["savedAt"].toString(), Qt::ISODate);
    entry.playbackFile = obj["playbackFile"].toString();
    entry.playbackName = obj["playbackName"].toString();
    entry.hasWebcam    = obj["hasWebcam"].toBool();
    entry.hasAudio     = obj["hasAudio"].toBool();
    return entry;
}

// ── writeMetadata — used by renameSession ─────────────────────────────────────
bool SessionManager::writeMetadata(const SessionEntry &entry)
{
    QFile f(metaPath(entry.sessionDir));

    // Preserve any unknown fields by reading first
    QJsonObject obj;
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isNull() && doc.isObject())
            obj = doc.object();
        f.close();
    }

    obj["id"]          = entry.id;
    obj["label"]       = entry.label;
    obj["savedAt"]     = entry.savedAt.toString(Qt::ISODate);
    obj["playbackFile"]= entry.playbackFile;
    obj["playbackName"]= entry.playbackName;
    obj["hasWebcam"]   = entry.hasWebcam;
    obj["hasAudio"]    = entry.hasAudio;

    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "SessionManager::writeMetadata: cannot open for writing:" << f.fileName();
        return false;
    }
    f.write(QJsonDocument(obj).toJson());
    return true;
}

// ── loadAll ───────────────────────────────────────────────────────────────────
QList<SessionEntry> SessionManager::loadAll()
{
    QList<SessionEntry> list;
    const QDir root(libraryRoot());
    if (!root.exists()) return list;

    for (const QString &sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString sessionDir = libraryRoot() + "/" + sub;
        const SessionEntry e = readMetadata(sessionDir);
        if (!e.id.isEmpty())
            list.append(e);
    }

    std::sort(list.begin(), list.end(), [](const SessionEntry &a, const SessionEntry &b) {
        return a.savedAt > b.savedAt;
    });

    return list;
}

// ── deleteSession ─────────────────────────────────────────────────────────────
bool SessionManager::deleteSession(const QString &id)
{
    const QString sessionDir = libraryRoot() + "/" + id;
    QDir dir(sessionDir);
    if (!dir.exists()) {
        qWarning() << "SessionManager::deleteSession: not found:" << sessionDir;
        return false;
    }
    return dir.removeRecursively();
}

// ── renameSession ─────────────────────────────────────────────────────────────
bool SessionManager::renameSession(const QString &id, const QString &newLabel)
{
    const QString sessionDir = libraryRoot() + "/" + id;
    SessionEntry e = readMetadata(sessionDir);
    if (e.id.isEmpty()) {
        qWarning() << "SessionManager::renameSession: session not found:" << id;
        return false;
    }
    e.label = newLabel;
    return writeMetadata(e);
}

// ── loadEntry ─────────────────────────────────────────────────────────────────
SessionEntry SessionManager::loadEntry(const QString &id)
{
    return readMetadata(libraryRoot() + "/" + id);
}

// ── restoreSession ────────────────────────────────────────────────────────────
bool SessionManager::restoreSession(const QString &id,
                                    QString &webcamRecorded,
                                    QString &audioRecorded,
                                    QString &tunedRecorded,
                                    QString &extractedTmpPlayback,
                                    QString &currentVideoFile,
                                    QString &currentVideoName,
                                    qint64  &audioOffset,
                                    qint64  &videoOffset,
                                    qint64  &sysOffset)
{
    const QString sessionDir = libraryRoot() + "/" + id;
    if (!QDir(sessionDir).exists()) {
        qWarning() << "SessionManager::restoreSession: dir not found:" << sessionDir;
        return false;
    }

    // Read offsets.json
    {
        QFile f(sessionDir + "/offsets.json");
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "SessionManager::restoreSession: cannot open offsets.json";
            // Non-fatal: continue, offsets stay at their current values
        } else {
            const QJsonObject off = QJsonDocument::fromJson(f.readAll()).object();
            audioOffset      = off["audioOffset"].toString().toLongLong();
            videoOffset      = off["videoOffset"].toString().toLongLong();
            sysOffset        = off["sysOffset"].toString().toLongLong();
            currentVideoFile = off["playbackFile"].toString();
            currentVideoName = off["playbackName"].toString();
        }
    }

    // Copy artefacts back to their /tmp/ paths.
    // The destination paths (webcamRecorded etc.) always point to the same
    // fixed /tmp/WakkaQt_tmp_* locations; we never reassign them here.
    auto restoreOne = [&](const QString &srcName, const QString &dst) {
        const QString src = sessionDir + "/" + srcName;
        if (!QFile::exists(src)) {
            qDebug() << "SessionManager: optional file not in session:" << srcName;
            return;
        }
        if (QFile::exists(dst) && !QFile::remove(dst))
            qWarning() << "SessionManager: cannot remove old tmp file:" << dst;
        if (!QFile::copy(src, dst))
            qWarning() << "SessionManager: failed to restore" << srcName << "->" << dst;
        else
            qDebug() << "SessionManager: restored" << srcName << "->" << dst;
    };

    restoreOne("webcam.mkv",  webcamRecorded);
    restoreOne("audio.wav",   audioRecorded);
    restoreOne("tuned.wav",   tunedRecorded);
    restoreOne("playback.wav",extractedTmpPlayback);

    qDebug() << "SessionManager: restore complete for session" << id
             << " | video:" << currentVideoName
             << " | audioOffset:" << audioOffset
             << " | videoOffset:" << videoOffset
             << " | sysOffset:"   << sysOffset;
    return true;
}
