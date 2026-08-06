#include "sessionrepository.h"
#include "complexes.h"

#include <QTest>
#include <QTemporaryDir>
#include <QScopedPointer>
#include <QFile>
#include <QDir>
#include <QAudioFormat>

// Exercises SessionRepository's save/restore/delete contract against a
// throwaway library root (see the WAKKAQT_LIBRARY_ROOT_OVERRIDE hook added
// to libraryRoot() specifically so this suite never touches a real user's
// ~/.WakkaQt/library) and the fixed /tmp source globals declared in
// complexes.h, repointed at temp files per test instead of the app's real
// recording paths.
class TestSessionRepository : public QObject
{
    Q_OBJECT

private:
    QScopedPointer<QTemporaryDir> m_libraryDir;
    QScopedPointer<QTemporaryDir> m_sourceDir;

    static bool writeValidWav(const QString &path, int frames = 8)
    {
        QAudioFormat fmt;
        fmt.setChannelCount(2);
        fmt.setSampleRate(44100);
        fmt.setSampleFormat(QAudioFormat::Int16);
        const QByteArray pcm(frames * 2 * 2, '\x5A'); // frames * channels * bytesPerSample
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        writeWavHeader(f, fmt, pcm.size(), pcm);
        return true;
    }

private slots:
    void init()
    {
        m_libraryDir.reset(new QTemporaryDir);
        m_sourceDir.reset(new QTemporaryDir);
        QVERIFY(m_libraryDir->isValid());
        QVERIFY(m_sourceDir->isValid());
        qputenv("WAKKAQT_LIBRARY_ROOT_OVERRIDE", m_libraryDir->path().toUtf8());

        webcamRecorded       = m_sourceDir->filePath("webcam.mkv");
        audioRecorded        = m_sourceDir->filePath("audio.wav");
        extractedTmpPlayback = m_sourceDir->filePath("playback.wav");
    }

    void cleanup()
    {
        qunsetenv("WAKKAQT_LIBRARY_ROOT_OVERRIDE");
        resetRecordingTempPaths();
        m_libraryDir.reset();
        m_sourceDir.reset();
    }

    void libraryRoot_honorsTestOverride()
    {
        QCOMPARE(SessionRepository::libraryRoot(), m_libraryDir->path());
    }

    void saveSession_missingAudio_isRejected()
    {
        QVERIFY(writeValidWav(extractedTmpPlayback));
        // audioRecorded deliberately left pointing at a nonexistent file.

        SessionRepository repo;
        SessionSnapshot snap;
        snap.currentVideoName = "Test Song";
        const SaveResult result = repo.saveSession(snap);

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("audio", Qt::CaseInsensitive));
        // Nothing should be left behind in the library on a rejected save.
        QVERIFY(QDir(m_libraryDir->path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    void saveSession_missingPlayback_isRejected()
    {
        QVERIFY(writeValidWav(audioRecorded));
        // extractedTmpPlayback deliberately left pointing at a nonexistent file.

        SessionRepository repo;
        SessionSnapshot snap;
        const SaveResult result = repo.saveSession(snap);

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("playback", Qt::CaseInsensitive));
    }

    void saveAndRestore_audioOnlySession_roundTrips()
    {
        QVERIFY(writeValidWav(audioRecorded));
        QVERIFY(writeValidWav(extractedTmpPlayback));

        SessionRepository repo;
        SessionSnapshot snap;
        snap.currentVideoName = "Audio Only Song";
        snap.currentVideoFile = "/some/original/path.mp3"; // original no longer needs to exist
        snap.audioOffset = 123;
        const SaveResult saved = repo.saveSession(snap);
        QVERIFY2(saved.ok, qPrintable(saved.error));
        QVERIFY(!saved.sessionId.isEmpty());

        const RestoreResult restored = repo.restoreSession(saved.sessionId);
        QVERIFY2(restored.ok, qPrintable(restored.error));
        QVERIFY(!restored.hasWebcam);
        QVERIFY(restored.warnings.isEmpty());
        QVERIFY(!restored.audioPath.isEmpty());
        QVERIFY(!restored.playbackPath.isEmpty());
        QVERIFY(restored.webcamPath.isEmpty());
        QCOMPARE(restored.snapshot.audioOffset, qint64(123));

        // Restored files must be real, parseable WAV — not just present.
        QFile af(restored.audioPath);
        QVERIFY(af.open(QIODevice::ReadOnly));
        QVERIFY(parseWavPcm(af.readAll()).isValid());

        QDir(restored.workspaceDir).removeRecursively();
    }

    void restoreSession_corruptAudioWav_isRejected()
    {
        QVERIFY(writeValidWav(audioRecorded));
        QVERIFY(writeValidWav(extractedTmpPlayback));
        SessionRepository repo;
        SessionSnapshot snap;
        const SaveResult saved = repo.saveSession(snap);
        QVERIFY2(saved.ok, qPrintable(saved.error));

        // Simulate a truncated/corrupt on-disk file post-save: nonzero size,
        // but not a valid WAV — exactly the case the v2.9.5 parseWavPcm()
        // validation (as opposed to a bare exists()+size() check) exists to catch.
        const QString audioPath = m_libraryDir->filePath(saved.sessionId + "/audio.wav");
        QFile f(audioPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("not a real wav file, just garbage bytes");
        f.close();

        const RestoreResult restored = repo.restoreSession(saved.sessionId);
        QVERIFY(!restored.ok);
        QVERIFY(restored.error.contains("audio", Qt::CaseInsensitive));
    }

    void deleteSession_pathTraversalId_isRejected()
    {
        // A sentinel outside the library root that a path-traversal id
        // could reach if isValidSessionId() didn't reject it.
        QTemporaryDir outsideDir;
        QVERIFY(outsideDir.isValid());
        const QString sentinel = outsideDir.filePath("do_not_delete_me");
        QFile sf(sentinel);
        QVERIFY(sf.open(QIODevice::WriteOnly));
        sf.write("sentinel");
        sf.close();

        const QString relative =
            QDir(m_libraryDir->path()).relativeFilePath(outsideDir.path()) + "/do_not_delete_me";

        SessionRepository repo;
        const OperationResult result = repo.deleteSession(relative);

        QVERIFY(!result.ok);
        QVERIFY(QFile::exists(sentinel));
    }

    void renameSession_pathTraversalId_isRejected()
    {
        SessionRepository repo;
        const OperationResult result = repo.renameSession("../../etc/passwd", "pwned");
        QVERIFY(!result.ok);
    }
};

QTEST_MAIN(TestSessionRepository)
#include "test_sessionrepository.moc"
