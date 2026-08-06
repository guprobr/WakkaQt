#include "sessionrepository.h"
#include "complexes.h" // webcamRecorded/audioRecorded/extractedTmpPlayback fixed tmp paths + parseWavPcm()/mediaHasVideoStream()

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QDateTime>
#include <QDebug>
#include <algorithm>

SessionRepository::SessionRepository() {}

QString SessionRepository::libraryRoot()
{
    // Test-only override: unit tests need a throwaway root instead of the
    // real ~/.WakkaQt/library so save/restore/delete round-trips don't touch
    // (or race with) an actual user's library. Unset in every normal run,
    // so production behavior is exactly the unconditional path below.
    const QString override = qEnvironmentVariable("WAKKAQT_LIBRARY_ROOT_OVERRIDE");
    if (!override.isEmpty())
        return override;
    return QDir::homePath() + "/.WakkaQt/library";
}

QString SessionRepository::metaPath(const QString &sessionDir)
{
    return sessionDir + "/session.json";
}

bool SessionRepository::copyFile(const QString &src, const QString &dst)
{
    if (src.isEmpty() || !QFile::exists(src)) {
        qWarning() << "SessionRepository::copyFile: source does not exist:" << src;
        return false;
    }
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        qWarning() << "SessionRepository::copyFile: cannot remove existing dst:" << dst;
        return false;
    }
    if (!QFile::copy(src, dst)) {
        qWarning() << "SessionRepository::copyFile: failed to copy" << src << "->" << dst;
        return false;
    }
    return true;
}

// QUuid::toString(WithoutBraces) never produces "..", "/", or "\", so this
// only ever rejects an id that didn't originate from saveSession() itself
// (e.g. hand-edited or otherwise tampered before reaching deleteSession()/
// renameSession()/restoreSession(), all of which build a filesystem path
// directly from libraryRoot() + "/" + id).
bool SessionRepository::isValidSessionId(const QString &id)
{
    if (id.isEmpty() || id.contains('/') || id.contains('\\') || id.contains(".."))
        return false;
    return true;
}

// ── saveSession ───────────────────────────────────────────────────────────────
SaveResult SessionRepository::saveSession(const SessionSnapshot &snapshot)
{
    SaveResult result;

    QDir dir;
    if (!dir.mkpath(libraryRoot())) {
        result.error = "cannot create library root " + libraryRoot();
        qWarning() << "SessionRepository:" << result.error;
        return result;
    }

    // Built entirely under a ".partial" directory and only renamed to its
    // final <id> name once every required file/metadata write has actually
    // succeeded — so a save that fails partway (disk full, permissions, a
    // copy that fails mid-transfer) can never leave a session that *looks*
    // complete to loadAll()/restoreSession() but is silently missing a file
    // or has stale metadata.
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    const QString partialDir = libraryRoot() + "/" + id + ".partial";
    const QString finalDir   = libraryRoot() + "/" + id;

    auto abort = [&](const QString &reason) {
        qWarning() << "SessionRepository: aborting save," << reason;
        QDir(partialDir).removeRecursively();
        result.error = reason;
    };

    if (!dir.mkpath(partialDir)) {
        result.error = "cannot create session dir " + partialDir;
        qWarning() << "SessionRepository:" << result.error;
        return result;
    }

    const bool hasWebcam      = QFile::exists(webcamRecorded)
                             && QFileInfo(webcamRecorded).size() > 0;
    const bool hasAudio       = QFile::exists(audioRecorded)
                             && QFileInfo(audioRecorded).size() > 0;
    const bool hasPlaybackWav = QFile::exists(extractedTmpPlayback)
                             && QFileInfo(extractedTmpPlayback).size() > 0;

    // Webcam is genuinely optional (audio-only performances are a supported
    // mode throughout the app), but audio and playback are the two
    // ingredients restoreSession() actually requires to produce something
    // restorable — refusing to save without them here keeps the two halves
    // of this contract consistent instead of creating a session that looks
    // saved but that restoreSession() can only ever fail to restore.
    if (!hasAudio) {
        abort("recorded vocal audio is missing — cannot save an incomplete session");
        return result;
    }
    if (!hasPlaybackWav) {
        abort("playback audio is missing — cannot save an incomplete session");
        return result;
    }

    // Each of these is only attempted if its source actually exists (that's
    // a legitimate "this session has no webcam" case, not a failure) — but
    // once attempted, it must succeed, or the whole save is rolled back.
    if (hasWebcam && !copyFile(webcamRecorded, partialDir + "/webcam.mkv")) {
        abort("webcam.mkv copy failed");
        return result;
    }
    if (hasAudio && !copyFile(audioRecorded, partialDir + "/audio.wav")) {
        abort("audio.wav copy failed");
        return result;
    }
    if (hasPlaybackWav && !copyFile(extractedTmpPlayback, partialDir + "/playback.wav")) {
        abort("playback.wav copy failed");
        return result;
    }
    // tuned.wav is intentionally NOT saved: it is always re-generated by
    // PreviewDialog from audio.wav with the user's current enhancement settings.

    // offsets.json — store qint64 as strings to avoid double precision loss.
    // QSaveFile writes to a temp file next to the target and only replaces it
    // on commit(), so a crash/power-loss mid-write can't corrupt/truncate it.
    {
        QJsonObject off;
        off["audioOffset"]  = QString::number(snapshot.audioOffset);
        off["videoOffset"]  = QString::number(snapshot.videoOffset);
        off["sysOffset"]    = QString::number(snapshot.sysOffset);
        off["playbackFile"] = snapshot.currentVideoFile;
        off["playbackName"] = snapshot.currentVideoName;
        QSaveFile f(partialDir + "/offsets.json");
        if (!f.open(QIODevice::WriteOnly)
            || f.write(QJsonDocument(off).toJson()) < 0
            || !f.commit()) {
            abort("cannot write offsets.json");
            return result;
        }
    }

    // session.json — display metadata
    QString label;
    {
        label = snapshot.currentVideoName
              + QString::fromUtf8(" \xe2\x80\x94 ")   // em dash
              + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm");
        QJsonObject obj;
        obj["id"]          = id;
        obj["label"]       = label;
        obj["savedAt"]     = QDateTime::currentDateTime().toString(Qt::ISODate);
        obj["playbackFile"]= snapshot.currentVideoFile;
        obj["playbackName"]= snapshot.currentVideoName;
        obj["hasWebcam"]   = hasWebcam;
        obj["hasAudio"]    = hasAudio;

        QSaveFile f(partialDir + "/session.json");
        if (!f.open(QIODevice::WriteOnly)
            || f.write(QJsonDocument(obj).toJson()) < 0
            || !f.commit()) {
            abort("cannot write session.json");
            return result;
        }
    }

    if (!dir.rename(partialDir, finalDir)) {
        abort("cannot finalize session directory (rename)");
        return result;
    }

    qDebug() << "SessionRepository: saved session" << id << "for" << snapshot.currentVideoName;
    result.ok = true;
    result.sessionId = id;
    return result;
}

// ── readMetadata ──────────────────────────────────────────────────────────────
SessionEntry SessionRepository::readMetadata(const QString &sessionDir)
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
bool SessionRepository::writeMetadata(const SessionEntry &entry)
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

    // QSaveFile writes to a temp file next to the target and only replaces
    // it on commit(), so a disk-full or interrupted write can't truncate
    // session.json — the previous QFile-based write ignored write()'s
    // return value and reported success even on a partial write.
    const QByteArray json = QJsonDocument(obj).toJson();
    QSaveFile saveFile(metaPath(entry.sessionDir));
    if (!saveFile.open(QIODevice::WriteOnly)) {
        qWarning() << "SessionRepository::writeMetadata: cannot open for writing:" << saveFile.fileName();
        return false;
    }
    if (saveFile.write(json) != json.size() || !saveFile.commit()) {
        qWarning() << "SessionRepository::writeMetadata: failed to write/commit:" << saveFile.fileName();
        return false;
    }
    return true;
}

// ── loadAll ───────────────────────────────────────────────────────────────────
QList<SessionEntry> SessionRepository::loadAll()
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
OperationResult SessionRepository::deleteSession(const QString &id)
{
    OperationResult result;
    if (!isValidSessionId(id)) {
        result.error = "malformed session id";
        qWarning() << "SessionRepository::deleteSession: rejecting" << result.error << ":" << id;
        return result;
    }
    const QString sessionDir = libraryRoot() + "/" + id;
    QDir dir(sessionDir);
    if (!dir.exists()) {
        result.error = "session not found";
        qWarning() << "SessionRepository::deleteSession:" << result.error << ":" << sessionDir;
        return result;
    }
    if (!dir.removeRecursively()) {
        result.error = "could not remove session directory";
        qWarning() << "SessionRepository::deleteSession:" << result.error << ":" << sessionDir;
        return result;
    }
    result.ok = true;
    return result;
}

// ── renameSession ─────────────────────────────────────────────────────────────
OperationResult SessionRepository::renameSession(const QString &id, const QString &newLabel)
{
    OperationResult result;
    if (!isValidSessionId(id)) {
        result.error = "malformed session id";
        qWarning() << "SessionRepository::renameSession: rejecting" << result.error << ":" << id;
        return result;
    }
    const QString sessionDir = libraryRoot() + "/" + id;
    SessionEntry e = readMetadata(sessionDir);
    if (e.id.isEmpty()) {
        result.error = "session not found";
        qWarning() << "SessionRepository::renameSession:" << result.error << ":" << id;
        return result;
    }
    e.label = newLabel;
    if (!writeMetadata(e)) {
        result.error = "could not write session metadata";
        return result;
    }
    result.ok = true;
    return result;
}

// ── loadEntry ─────────────────────────────────────────────────────────────────
SessionEntry SessionRepository::loadEntry(const QString &id)
{
    if (!isValidSessionId(id)) {
        qWarning() << "SessionRepository::loadEntry: rejecting malformed id:" << id;
        return {};
    }
    return readMetadata(libraryRoot() + "/" + id);
}

// ── restoreSession ────────────────────────────────────────────────────────────
RestoreResult SessionRepository::restoreSession(const QString &id)
{
    RestoreResult result;

    if (!isValidSessionId(id)) {
        result.error = "malformed session id";
        qWarning() << "SessionRepository::restoreSession: rejecting" << result.error << ":" << id;
        return result;
    }
    const QString sessionDir = libraryRoot() + "/" + id;
    if (!QDir(sessionDir).exists()) {
        result.error = "session folder not found";
        qWarning() << "SessionRepository::restoreSession:" << result.error << ":" << sessionDir;
        return result;
    }

    // Neutral defaults: if offsets.json turns out to be missing/unreadable
    // below, restore must not leave these at whatever the caller happened to
    // have in memory from a previous recording/restore.
    SessionSnapshot &snapshot = result.snapshot;
    snapshot.audioOffset = 0;
    snapshot.videoOffset = 0;
    snapshot.sysOffset   = 0;
    snapshot.currentVideoFile.clear();
    snapshot.currentVideoName.clear();

    // Read offsets.json
    {
        QFile f(sessionDir + "/offsets.json");
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "SessionRepository::restoreSession: cannot open offsets.json";
            // Non-fatal: continue with the neutral defaults set above.
        } else {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                qWarning() << "SessionRepository::restoreSession: offsets.json is invalid:"
                           << parseError.errorString();
                result.warnings << "Offsets metadata is invalid; offsets were reset to zero "
                                   "and audio/video sync may need manual adjustment.";
                // Neutral defaults set above are left in place.
            } else {
                const QJsonObject off = doc.object();
                snapshot.audioOffset      = off["audioOffset"].toString().toLongLong();
                snapshot.videoOffset      = off["videoOffset"].toString().toLongLong();
                snapshot.sysOffset        = off["sysOffset"].toString().toLongLong();
                snapshot.currentVideoFile = off["playbackFile"].toString();
                snapshot.currentVideoName = off["playbackName"].toString();
            }
        }
    }

    // Resolved independently of whether offsets.json above parsed
    // successfully — a session with a perfectly good local playback.wav
    // must not lose it just because its sidecar offsets metadata was
    // missing or corrupt. Validated with parseWavPcm() rather than just
    // existence/size, same reasoning as the audio.wav check below: a
    // truncated file can still have nonzero size.
    const QString localPlaybackPath = sessionDir + "/playback.wav";
    bool localPlaybackOk = false;
    if (QFile::exists(localPlaybackPath)) {
        QFile pf(localPlaybackPath);
        localPlaybackOk = pf.open(QIODevice::ReadOnly) && parseWavPcm(pf.readAll()).isValid();
        if (!localPlaybackOk)
            qWarning() << "SessionRepository::restoreSession: playback.wav exists but is not "
                          "a valid WAV file:" << sessionDir;
    }
    // The render only needs audio from the playback file; always prefer the
    // guaranteed local copy saved inside the session folder so re-renders
    // work regardless of whether the original file still exists.
    if (localPlaybackOk)
        snapshot.currentVideoFile = localPlaybackPath;

    // Validate the session's actual file content before trusting it, instead
    // of just checking which files happen to exist. A session whose
    // session.json claims hasAudio/hasWebcam but whose corresponding file is
    // now missing, empty, or otherwise corrupted (disk error, manual
    // tampering, an interrupted copy) used to be reported as a successful
    // restore regardless — silently losing vocals entirely, or downgrading a
    // webcam session to audio-only with no indication anything was wrong.
    const SessionEntry meta = readMetadata(sessionDir);

    const QString audioPath = sessionDir + "/audio.wav";
    bool audioOk = QFile::exists(audioPath) && QFileInfo(audioPath).size() > 0;
    if (audioOk) {
        QFile af(audioPath);
        audioOk = af.open(QIODevice::ReadOnly) && parseWavPcm(af.readAll()).isValid();
    }
    if (!audioOk) {
        result.error = meta.hasAudio
            ? "session metadata claims audio was recorded, but audio.wav is "
              "missing, empty, or not a valid WAV file — the session cannot be restored"
            : "session has no usable audio.wav — the session cannot be restored";
        qWarning() << "SessionRepository::restoreSession:" << result.error << ":" << sessionDir;
        return result;
    }

    // Ground-truth against the session folder itself, not any caller state:
    // this is what tells restoreAndRender() apart from "is a camera
    // currently plugged into this machine" (MainWindow::hasCamera), which
    // has nothing to do with what a given past session actually recorded.
    // Existence/size alone doesn't catch a truncated recording that still
    // has some bytes but no actual video stream in it.
    const QString webcamPath = sessionDir + "/webcam.mkv";
    result.hasWebcam = QFile::exists(webcamPath) && QFileInfo(webcamPath).size() > 0
                     && mediaHasVideoStream(webcamPath);
    if (meta.hasWebcam && !result.hasWebcam) {
        result.warnings << "This session was recorded with a webcam, but its video file is "
                            "missing, empty, or unreadable — restoring as audio-only.";
        qWarning() << "SessionRepository::restoreSession: webcam.mkv missing/invalid despite "
                      "hasWebcam=true in metadata:" << sessionDir;
    }

    // The render's only source of the karaoke track itself — without it
    // there is nothing to mix the restored vocal against, so this is fatal
    // rather than a downgrade, same as the audio.wav check above.
    if (!QFile::exists(snapshot.currentVideoFile)) {
        result.error = "playback source '" + snapshot.currentVideoFile
                      + "' could not be found — the session cannot be restored";
        qWarning() << "SessionRepository::restoreSession:" << result.error;
        return result;
    }
    // A session saved before playback.wav became mandatory (see
    // saveSession()) can reach here with a valid *external* playback source
    // but no local copy — flag it so the caller can tell the user why the
    // live preview's backing-track monitor may end up silent (it needs a
    // real WAV; AudioAmplifier already degrades gracefully if what lands in
    // the workspace below isn't one).
    if (!localPlaybackOk) {
        result.warnings << "This session predates local playback caching; its preview backing "
                            "track may be unavailable if the original file isn't a WAV.";
    }

    // Copy artefacts into a throwaway directory private to this restore,
    // instead of the shared /tmp WakkaQt_tmp_* paths every other restore
    // and every live recording also uses. A brand-new directory has no
    // pre-existing file to remove/swap out, so a failure partway through
    // just means deleting the (still-private) directory and reporting an
    // error — there is nothing for a later step to roll back, and nothing
    // for a concurrent/subsequent restore or recording to collide with.
    const QString workspaceDir = QDir::temp().filePath(
        "WakkaQt_restore_" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(workspaceDir)) {
        result.error = "failed to create restore workspace";
        qWarning() << "SessionRepository::restoreSession:" << result.error << ":" << workspaceDir;
        return result;
    }

    struct RestoreItem { const char *srcName; QString *outPath; };
    const RestoreItem items[] = {
        { "webcam.mkv", &result.webcamPath },
        { "audio.wav",  &result.audioPath },
    };
    constexpr int kCount = int(sizeof(items) / sizeof(items[0]));

    for (int i = 0; i < kCount; ++i) {
        const QString src = sessionDir + "/" + items[i].srcName;
        if (!QFile::exists(src)) {
            qDebug() << "SessionRepository: optional file not in session:" << items[i].srcName;
            continue;
        }
        const QString dst = workspaceDir + "/" + items[i].srcName;
        if (!QFile::copy(src, dst)) {
            result.error = QString("failed to stage %1").arg(items[i].srcName);
            qWarning() << "SessionRepository::restoreSession:" << result.error << "->" << dst;
            QDir(workspaceDir).removeRecursively();
            return result;
        }
        *items[i].outPath = dst;
        qDebug() << "SessionRepository: restored" << items[i].srcName << "->" << dst;
    }

    // playback.wav: prefer the session's own validated local copy; fall back
    // to snapshot.currentVideoFile (already confirmed to exist above) for
    // older sessions saved before playback.wav became mandatory. Without
    // this fallback, such a session could report a successful restore with
    // result.playbackPath left empty even though a valid playback source
    // was found and used for snapshot.currentVideoFile.
    {
        const QString playbackSrc = localPlaybackOk ? localPlaybackPath : snapshot.currentVideoFile;
        const QString dst = workspaceDir + "/playback.wav";
        if (!QFile::copy(playbackSrc, dst)) {
            result.error = "failed to stage playback audio";
            qWarning() << "SessionRepository::restoreSession:" << result.error << "->" << dst;
            QDir(workspaceDir).removeRecursively();
            return result;
        }
        result.playbackPath = dst;
        qDebug() << "SessionRepository: restored playback ->" << dst;
    }
    // tuned.wav is not saved/restored — PreviewDialog re-generates it from audio.wav

    result.workspaceDir = workspaceDir;

    qDebug() << "SessionRepository: restore complete for session" << id
             << " | hasWebcam:" << result.hasWebcam
             << " | workspace:" << workspaceDir
             << " | video:" << snapshot.currentVideoName
             << " | audioOffset:" << snapshot.audioOffset
             << " | videoOffset:" << snapshot.videoOffset
             << " | sysOffset:"   << snapshot.sysOffset;
    result.ok = true;
    return result;
}
