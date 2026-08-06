#include "vocalseparationjob.h"
#include "atomicfilecommit.h"

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QThread>

// Exercises VocalSeparationJob's own orchestration — workspace lifecycle
// (created before separate(), discarded on success/failed-separation, but
// deliberately PRESERVED on export failure so the user never has to re-run
// the expensive separation), atomic commit on export, and cancellation —
// via setSeparateEngineForTesting()/setExportEngineForTesting(), stand-ins
// for VocalSeparator::separate()/FFmpegNative's mux-or-transcode call. See
// vocalseparationjob.h for why these seams cost nothing in production.
class TestVocalSeparationJob : public QObject
{
    Q_OBJECT

private:
    QScopedPointer<QTemporaryDir> m_dir;

    static bool writeFile(const QString &path, const QByteArray &content)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        return f.write(content) == content.size();
    }

    static bool readFile(const QString &path, QByteArray *out)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        *out = f.readAll();
        return true;
    }

    // Runs separate() with an engine that writes `content` into
    // workspaceDir/instrumental.wav and succeeds. Returns the resulting
    // tempWavPath (from the separated() signal) — used as exportResult()'s
    // input by the export-focused tests below.
    static QString separateSuccessfully(VocalSeparationJob &job, const QByteArray &content)
    {
        job.setSeparateEngineForTesting(
            [content](const QString &, const QString &workspaceDir,
                      const std::function<void(int)> &progressFn, QString &,
                      const std::atomic<bool> *) -> QString {
                progressFn(100);
                const QString path = workspaceDir + "/instrumental.wav";
                QFile f(path);
                if (!f.open(QIODevice::WriteOnly)) return QString();
                f.write(content);
                return path;
            });
        QSignalSpy separatedSpy(&job, &VocalSeparationJob::separated);
        job.separate("input.mp4");
        if (!separatedSpy.wait(5000) || separatedSpy.isEmpty())
            return QString();
        return separatedSpy.takeFirst().at(0).toString();
    }

private slots:
    void init() { m_dir.reset(new QTemporaryDir); QVERIFY(m_dir->isValid()); }
    void cleanup() { m_dir.reset(); }

    void separate_success_emitsSeparatedWithWorkingTempFile()
    {
        VocalSeparationJob job;
        const QString tempPath = separateSuccessfully(job, "fake instrumental");
        QVERIFY(!tempPath.isEmpty());
        QByteArray content;
        QVERIFY(readFile(tempPath, &content));
        QCOMPARE(content, QByteArray("fake instrumental"));
    }

    void separate_failure_emitsSeparationFailedAndDiscardsWorkspace()
    {
        VocalSeparationJob job;
        QString capturedWorkspaceDir;
        job.setSeparateEngineForTesting(
            [&capturedWorkspaceDir](const QString &, const QString &workspaceDir,
                                     const std::function<void(int)> &, QString &errorOut,
                                     const std::atomic<bool> *) -> QString {
                capturedWorkspaceDir = workspaceDir;
                errorOut = "fake separation failure";
                return QString();
            });

        QSignalSpy failedSpy(&job, &VocalSeparationJob::separationFailed);
        job.separate("input.mp4");
        QVERIFY(failedSpy.wait(5000));

        const QList<QVariant> args = failedSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QString("fake separation failure"));
        QVERIFY(!args.at(1).toBool()); // wasCancelled=false
        QVERIFY(!capturedWorkspaceDir.isEmpty());
        QVERIFY(!QDir(capturedWorkspaceDir).exists());
    }

    void separate_cancelled_emitsSeparationFailedWithCancelledFlag()
    {
        VocalSeparationJob job;
        job.setSeparateEngineForTesting(
            [](const QString &, const QString &, const std::function<void(int)> &,
               QString &errorOut, const std::atomic<bool> *cancelled) -> QString {
                for (int waited = 0; waited < 2000; waited += 10) {
                    if (cancelled && cancelled->load()) {
                        errorOut = "Cancelled";
                        return QString();
                    }
                    QThread::msleep(10);
                }
                return QString();
            });

        QSignalSpy failedSpy(&job, &VocalSeparationJob::separationFailed);
        job.separate("input.mp4");
        job.cancelSeparate();
        QVERIFY(failedSpy.wait(5000));

        const QList<QVariant> args = failedSpy.takeFirst();
        QVERIFY(args.at(1).toBool()); // wasCancelled=true
    }

    void export_success_commitsToSavePathAndDiscardsWorkspace()
    {
        VocalSeparationJob job;
        const QString tempPath = separateSuccessfully(job, "instrumental bytes");
        QVERIFY(!tempPath.isEmpty());
        const QString workspaceDir = QFileInfo(tempPath).absolutePath();

        job.setExportEngineForTesting(
            [](const QString &, const QString &, const QString &partialOutputPath, bool,
               const std::function<void(int)> &progressCb, const std::atomic<bool> *) {
                progressCb(100);
                QFile f(partialOutputPath);
                if (!f.open(QIODevice::WriteOnly)) return false;
                return f.write("exported content") > 0;
            });

        const QString savePath = m_dir->filePath("out.mp3");
        VocalSeparationJob::ExportParams params;
        params.tempWavPath = tempPath;
        params.inputFile = "input.mp4";
        params.savePath = savePath;
        params.saveAsVideo = false;

        QSignalSpy exportedSpy(&job, &VocalSeparationJob::exported);
        job.exportResult(params);
        QVERIFY(exportedSpy.wait(5000));

        QCOMPARE(exportedSpy.takeFirst().at(0).toString(), savePath);
        QByteArray content;
        QVERIFY(readFile(savePath, &content));
        QCOMPARE(content, QByteArray("exported content"));
        QVERIFY(!QFile::exists(sidecarPathFor(savePath, "partial")));
        // Success discards the (now-consumed) separation workspace.
        QVERIFY(!QDir(workspaceDir).exists());
    }

    // The whole point of not auto-discarding on export failure: the
    // separated instrumental (expensive to recompute) must still be sitting
    // in the workspace afterward so MainWindow's recovery UX (Try Again /
    // Save as WAV) has real data to act on.
    void export_failure_preservesWorkspace()
    {
        VocalSeparationJob job;
        const QString tempPath = separateSuccessfully(job, "instrumental bytes");
        QVERIFY(!tempPath.isEmpty());
        const QString workspaceDir = QFileInfo(tempPath).absolutePath();

        job.setExportEngineForTesting(
            [](const QString &, const QString &, const QString &, bool,
               const std::function<void(int)> &, const std::atomic<bool> *) {
                return false; // simulates a mux/encode failure
            });

        VocalSeparationJob::ExportParams params;
        params.tempWavPath = tempPath;
        params.inputFile = "input.mp4";
        params.savePath = m_dir->filePath("out.mp3");
        params.saveAsVideo = false;

        QSignalSpy failedSpy(&job, &VocalSeparationJob::exportFailed);
        job.exportResult(params);
        QVERIFY(failedSpy.wait(5000));

        QVERIFY(!failedSpy.takeFirst().at(0).toString().isEmpty());
        QVERIFY(!QFile::exists(params.savePath));
        // Not discarded — still recoverable, per discardWorkspace()'s contract.
        QVERIFY(QDir(workspaceDir).exists());
        QVERIFY(QFile::exists(tempPath));

        job.discardWorkspace(); // clean up after ourselves
    }

    void export_cancelled_emitsCancelledError()
    {
        VocalSeparationJob job;
        const QString tempPath = separateSuccessfully(job, "instrumental bytes");
        QVERIFY(!tempPath.isEmpty());

        job.setExportEngineForTesting(
            [](const QString &, const QString &, const QString &partialOutputPath, bool,
               const std::function<void(int)> &, const std::atomic<bool> *cancelled) {
                QFile partial(partialOutputPath);
                (void)partial.open(QIODevice::WriteOnly);
                for (int waited = 0; waited < 2000; waited += 10) {
                    if (cancelled && cancelled->load())
                        return false;
                    QThread::msleep(10);
                }
                return true;
            });

        VocalSeparationJob::ExportParams params;
        params.tempWavPath = tempPath;
        params.inputFile = "input.mp4";
        params.savePath = m_dir->filePath("out.mp3");
        params.saveAsVideo = false;

        QSignalSpy failedSpy(&job, &VocalSeparationJob::exportFailed);
        job.exportResult(params);
        job.cancelExport();
        QVERIFY(failedSpy.wait(5000));

        QCOMPARE(failedSpy.takeFirst().at(0).toString(), QString("Cancelled"));
        job.discardWorkspace();
    }
};

QTEST_MAIN(TestVocalSeparationJob)
#include "test_vocalseparationjob.moc"
