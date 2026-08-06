#include "atomicfilecommit.h"

#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

// Covers the exact failure this helper was introduced to fix (see v2.9.5):
// a naive remove()-then-rename() sequence has a window where a failed
// rename leaves neither the old nor the new file at finalPath. Every test
// here writes real files under a QTemporaryDir and checks actual on-disk
// state after each call, not just the return value.
class TestAtomicFileCommit : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    static bool writeFile(const QString &path, const QByteArray &content)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        return f.write(content) == content.size();
    }

    static QByteArray readFile(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    }

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
    }

    void sidecarPathFor_preservesExtension()
    {
        const QString path = sidecarPathFor("/some/dir/song.mp4", "partial");
        QCOMPARE(path, QString("/some/dir/song.partial.mp4"));
    }

    void sidecarPathFor_noExtension()
    {
        const QString path = sidecarPathFor("/some/dir/song", "partial");
        QCOMPARE(path, QString("/some/dir/song.partial"));
    }

    void sidecarPathFor_dotsInDirectoryDontConfuseIt()
    {
        const QString path = sidecarPathFor("/some/dir.with.dots/song.wav", "backup");
        QCOMPARE(path, QString("/some/dir.with.dots/song.backup.wav"));
    }

    void commit_noExistingFinal_movesPartialIntoPlace()
    {
        const QString partial = m_dir.filePath("out.partial.wav");
        const QString final_  = m_dir.filePath("out.wav");
        QVERIFY(writeFile(partial, "new content"));

        const QString err = commitPartialOverFinal(partial, final_);

        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!QFile::exists(partial));
        QVERIFY(QFile::exists(final_));
        QCOMPARE(readFile(final_), QByteArray("new content"));
        // No leftover backup sidecar on the clean-create path.
        QVERIFY(!QFile::exists(sidecarPathFor(final_, "backup")));
    }

    void commit_existingFinal_isReplacedAndBackupCleaned()
    {
        const QString partial = m_dir.filePath("out.partial.wav");
        const QString final_  = m_dir.filePath("out.wav");
        QVERIFY(writeFile(final_, "old content"));
        QVERIFY(writeFile(partial, "new content"));

        const QString err = commitPartialOverFinal(partial, final_);

        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(readFile(final_), QByteArray("new content"));
        QVERIFY(!QFile::exists(partial));
        QVERIFY(!QFile::exists(sidecarPathFor(final_, "backup")));
    }

    // The scenario the helper exists for: the partial never actually landed
    // (crash, caller bug) but a valid file already sits at finalPath. The
    // old rename-then-rename code would have deleted it for nothing; this
    // must leave it untouched and say so.
    void commit_missingPartial_restoresExistingFinal()
    {
        const QString partial = m_dir.filePath("does_not_exist.partial.wav");
        const QString final_  = m_dir.filePath("out.wav");
        QVERIFY(writeFile(final_, "precious old content"));

        const QString err = commitPartialOverFinal(partial, final_);

        QVERIFY(!err.isEmpty());
        QVERIFY(QFile::exists(final_));
        QCOMPARE(readFile(final_), QByteArray("precious old content"));
        QVERIFY(!QFile::exists(sidecarPathFor(final_, "backup")));
    }

    void commit_leftoverBackupFromCrashedRun_isClearedFirst()
    {
        const QString partial = m_dir.filePath("out.partial.wav");
        const QString final_  = m_dir.filePath("out.wav");
        const QString backup  = sidecarPathFor(final_, "backup");
        QVERIFY(writeFile(backup, "stale backup from a previous crash"));
        QVERIFY(writeFile(final_, "current content"));
        QVERIFY(writeFile(partial, "new content"));

        const QString err = commitPartialOverFinal(partial, final_);

        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(readFile(final_), QByteArray("new content"));
        QVERIFY(!QFile::exists(backup));
    }

    // Regression test: an unremovable leftover backup must fail with a clear
    // error naming the actual blocker, not the misleading "could not move
    // existing file aside" message the old rename(finalPath, backupPath)
    // call would have produced (it would fail too, since backupPath still
    // exists, but for a reason that message doesn't mention). A directory
    // at backupPath is a portable way to make QFile::remove() fail deterministically
    // (it only removes regular files), without relying on permission quirks.
    void commit_unremovableLeftoverBackup_failsWithClearError()
    {
        const QString partial = m_dir.filePath("out.partial.wav");
        const QString final_  = m_dir.filePath("out.wav");
        const QString backup  = sidecarPathFor(final_, "backup");
        QVERIFY(QDir(m_dir.path()).mkpath(backup));
        QVERIFY(writeFile(final_, "current content"));
        QVERIFY(writeFile(partial, "new content"));

        const QString err = commitPartialOverFinal(partial, final_);

        QVERIFY(!err.isEmpty());
        QVERIFY2(err.contains("backup", Qt::CaseInsensitive), qPrintable(err));
        // Neither file should have been touched: the commit must bail out
        // before ever moving final_ aside.
        QCOMPARE(readFile(final_), QByteArray("current content"));
        QVERIFY(QFile::exists(partial));
    }
};

QTEST_MAIN(TestAtomicFileCommit)
#include "test_atomicfilecommit.moc"
