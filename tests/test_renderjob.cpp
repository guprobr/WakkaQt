#include "renderjob.h"
#include "atomicfilecommit.h"

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QThread>

// Exercises RenderJob's own orchestration — atomic commit on success,
// partial-file cleanup on failure/cancellation, and (critically) that a
// cancelled or failed render never touches a pre-existing file at
// outputPath — via RenderJob::setEngineForTesting(), a stand-in for
// FFmpegNative::renderVideo() that writes deterministic bytes instead of
// running a real (multi-second, FFmpeg-dependent) render. See renderjob.h
// for why this seam costs nothing in production.
class TestRenderJob : public QObject
{
    Q_OBJECT

private:
    QScopedPointer<QTemporaryDir> m_dir;

    static RenderJob::Params makeParams(const QString &outputPath)
    {
        RenderJob::Params params;
        params.outputPath = outputPath;
        return params;
    }

    static bool readFile(const QString &path, QByteArray *out)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        *out = f.readAll();
        return true;
    }

    static bool writeFile(const QString &path, const QByteArray &content)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        return f.write(content) == content.size();
    }

private slots:
    void init() { m_dir.reset(new QTemporaryDir); QVERIFY(m_dir->isValid()); }
    void cleanup() { m_dir.reset(); }

    void successfulRender_commitsEngineOutputToOutputPath()
    {
        RenderJob job;
        job.setEngineForTesting([](const RenderJob::Params &, const QString &partialOutputPath,
                                    const std::function<void(double)> &progressCb,
                                    const std::atomic<bool> *) {
            progressCb(1.0);
            QFile f(partialOutputPath);
            if (!f.open(QIODevice::WriteOnly)) return false;
            return f.write("rendered content") > 0;
        });

        const QString outputPath = m_dir->filePath("out.mp4");
        QSignalSpy finishedSpy(&job, &RenderJob::finished);
        job.start(makeParams(outputPath));
        QVERIFY(finishedSpy.wait(5000));

        QCOMPARE(finishedSpy.count(), 1);
        const QList<QVariant> args = finishedSpy.takeFirst();
        QVERIFY2(args.at(0).toBool(), "expected success=true");
        QVERIFY(!args.at(1).toBool()); // cancelled=false

        QByteArray content;
        QVERIFY(readFile(outputPath, &content));
        QCOMPARE(content, QByteArray("rendered content"));
        QVERIFY(!QFile::exists(sidecarPathFor(outputPath, "partial")));
    }

    void failedRender_emitsFailureAndLeavesNoOutput()
    {
        RenderJob job;
        job.setEngineForTesting([](const RenderJob::Params &, const QString &,
                                    const std::function<void(double)> &,
                                    const std::atomic<bool> *) {
            return false; // simulates FFmpeg exiting with an error
        });

        const QString outputPath = m_dir->filePath("out.mp4");
        QSignalSpy finishedSpy(&job, &RenderJob::finished);
        job.start(makeParams(outputPath));
        QVERIFY(finishedSpy.wait(5000));

        const QList<QVariant> args = finishedSpy.takeFirst();
        QVERIFY(!args.at(0).toBool());  // success=false
        QVERIFY(!args.at(1).toBool());  // cancelled=false
        QVERIFY(!args.at(2).toString().isEmpty());
        QVERIFY(!QFile::exists(outputPath));
        QVERIFY(!QFile::exists(sidecarPathFor(outputPath, "partial")));
    }

    // Regression coverage for the bug fixed in mainwindowRenderMgr.cpp: a
    // cancelled render must never touch a pre-existing valid file at
    // outputPath. RenderJob itself already got this right (it only ever
    // commits onto outputPath on success) — this pins that contract down at
    // the job level, independent of whatever the UI layer does with the
    // cancelled signal.
    void cancelledRender_leavesPreexistingOutputFileUntouched()
    {
        RenderJob job;
        job.setEngineForTesting([](const RenderJob::Params &, const QString &partialOutputPath,
                                    const std::function<void(double)> &,
                                    const std::atomic<bool> *cancelled) {
            QFile f(partialOutputPath);
            (void)f.open(QIODevice::WriteOnly);
            f.write("partial garbage"); // simulates a part-written file
            for (int waited = 0; waited < 2000; waited += 10) {
                if (cancelled && cancelled->load())
                    return false;
                QThread::msleep(10);
            }
            return true; // only reached if cancel() never arrived (test would then fail below)
        });

        const QString outputPath = m_dir->filePath("out.mp4");
        QVERIFY(writeFile(outputPath, "precious pre-existing render"));

        QSignalSpy finishedSpy(&job, &RenderJob::finished);
        job.start(makeParams(outputPath));
        job.cancel();
        QVERIFY(finishedSpy.wait(5000));

        const QList<QVariant> args = finishedSpy.takeFirst();
        QVERIFY(!args.at(0).toBool()); // success=false
        QVERIFY(args.at(1).toBool());  // cancelled=true

        QByteArray content;
        QVERIFY(readFile(outputPath, &content));
        QCOMPARE(content, QByteArray("precious pre-existing render"));
        QVERIFY(!QFile::exists(sidecarPathFor(outputPath, "partial")));
    }
};

QTEST_MAIN(TestRenderJob)
#include "test_renderjob.moc"
