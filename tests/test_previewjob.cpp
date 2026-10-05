#include "previewjob.h"
#include "complexes.h"
#include "playbackbleedfilter.h"
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

class TestPreviewJob : public QObject {
    Q_OBJECT
    static QAudioFormat format(int rate = 44100, int channels = 2) {
        QAudioFormat f;
        f.setSampleRate(rate);
        f.setChannelCount(channels);
        f.setSampleFormat(QAudioFormat::Int16);
        return f;
    }
    static QByteArray tone(int amplitude, int rate = 44100, int channels = 2) {
        QByteArray pcm(rate * channels * 2, 0);
        for (int i = 0; i < rate; ++i)
            for (int c = 0; c < channels; ++c)
                qToLittleEndian<qint16>(qint16(amplitude * std::sin(2 * 3.141592653589793 * 440 * i / rate)),
                    pcm.data() + (i * channels + c) * 2);
        return pcm;
    }
    static bool write(const QString &path, const QByteArray &pcm, int rate = 44100, int channels = 2,
                      QAudioFormat::SampleFormat sampleFormat = QAudioFormat::Int16) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        auto f = format(rate, channels);
        f.setSampleFormat(sampleFormat);
        if (!writeWavHeader(file, f, pcm.size(), pcm)) return false;
        return file.error() == QFile::NoError;
    }
private slots:
    void convertsReferenceToVocalFormat_data() {
        QTest::addColumn<int>("microphoneRate");
        QTest::addColumn<int>("playbackRate");
        QTest::addColumn<int>("playbackChannels");
        QTest::addColumn<int>("microphoneChannels");
        QTest::addColumn<bool>("microphoneInt32");
        QTest::newRow("48 kHz mic, 44.1 kHz mono backing") << 48000 << 44100 << 1 << 2 << false;
        QTest::newRow("44.1 kHz mic, 48 kHz stereo backing") << 44100 << 48000 << 2 << 2 << false;
        QTest::newRow("96 kHz mic, 44.1 kHz stereo backing") << 96000 << 44100 << 2 << 2 << false;
        QTest::newRow("192 kHz Int32 stereo mic") << 192000 << 44100 << 2 << 2 << true;
        QTest::newRow("192 kHz Int32 mono mic") << 192000 << 44100 << 2 << 1 << true;
    }
    void convertsReferenceToVocalFormat() {
        QFETCH(int, microphoneRate);
        QFETCH(int, playbackRate);
        QFETCH(int, playbackChannels);
        QFETCH(int, microphoneChannels);
        QFETCH(bool, microphoneInt32);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray microphone = tone(3000, microphoneRate, microphoneChannels);
        if (microphoneInt32) {
            QByteArray wide(microphone.size() * 2, 0);
            for (qsizetype i = 0; i < microphone.size() / 2; ++i)
                qToLittleEndian<qint32>(qint32(qFromLittleEndian<qint16>(microphone.constData() + i * 2)) * 65536,
                                       wide.data() + i * 4);
            microphone = wide;
        }
        const QByteArray playback = tone(12000, playbackRate, playbackChannels);
        const QString micPath = dir.filePath("microphone.wav"), refPath = dir.filePath("backing.wav");
        QVERIFY(write(micPath, microphone, microphoneRate, microphoneChannels,
                      microphoneInt32 ? QAudioFormat::Int32 : QAudioFormat::Int16));
        QVERIFY(write(refPath, playback, playbackRate, playbackChannels));
        PreviewJob job;
        QSignalSpy extracted(&job, &PreviewJob::extracted), failed(&job, &PreviewJob::extractionFailed);
        PreviewJob::ExtractParams params;
        params.sourceFile = micPath;
        params.playbackFile = refPath;
        params.destTempFile = dir.filePath("scratch.wav");
        job.extract(params);
        QVERIFY(extracted.wait(10000));
        QCOMPARE(failed.size(), 0);
        const QAudioFormat decodedFormat = qvariant_cast<QAudioFormat>(extracted[0][1]);
#ifdef WAKKAQT_FFMPEG_NATIVE
        QCOMPARE(decodedFormat, format(microphoneRate > 96000 ? 48000 : microphoneRate));
        if (!microphoneInt32 && microphoneChannels == 2 && microphoneRate <= 96000)
            QCOMPARE(extracted[0][0].toByteArray(), microphone); // preserve ordinary native-rate vocals
#else
        QCOMPARE(decodedFormat, format()); // CLI extraction's existing common rate
#endif
        const QByteArray converted = extracted[0][2].toByteArray();
        const QByteArray decodedMic = extracted[0][0].toByteArray();
        QVERIFY(!decodedMic.isEmpty());
        QVERIFY(qAbs(decodedMic.size() / decodedFormat.bytesPerFrame() - decodedFormat.sampleRate()) <= 1);
        QVERIFY(!converted.isEmpty());
        QCOMPARE(converted.size() % decodedFormat.bytesPerFrame(), 0);
        // The reference keeps its one-second duration after resampling; it
        // must not simply relabel the old PCM bytes as the microphone rate.
        const qint64 frames = converted.size() / decodedFormat.bytesPerFrame();
        QVERIFY(qAbs(frames - decodedFormat.sampleRate()) <= 1);
        double leftEnergy = 0, rightEnergy = 0;
        for (qint64 i = 64; i < frames - 64; ++i) {
            const qint16 left = qFromLittleEndian<qint16>(converted.constData() + i * 4);
            const qint16 right = qFromLittleEndian<qint16>(converted.constData() + i * 4 + 2);
            leftEnergy += double(left) * left;
            rightEnergy += double(right) * right;
            QCOMPARE(left, right);
        }
        QVERIFY(leftEnergy > 0);
        QCOMPARE(leftEnergy, rightEnergy);
        // Reproduce the reported 192 kHz/32-bit device format through the
        // complete extraction -> cleanup path, not just WAV metadata checks.
        if (microphoneInt32 && PlaybackBleedFilter::isAvailable()) {
            QSignalSpy enhanced(&job, &PreviewJob::enhanced), cleanupFailed(&job, &PreviewJob::enhancementFailed);
            PreviewJob::EnhanceParams cleanup;
            cleanup.removePlaybackBleed = true;
            cleanup.playbackPcm = converted;
            cleanup.pitchCorrectionAmount = 0;
            cleanup.noiseReductionAmount = 0;
            cleanup.masterAudio = false;
            QVERIFY(job.enhance(decodedMic, decodedFormat, cleanup));
            QVERIFY(enhanced.wait(10000));
            QCOMPARE(cleanupFailed.size(), 0);
            QCOMPARE(enhanced[0][0].toByteArray().size(), decodedMic.size());
        }
        // Input files are never resampled in place.
        QFile rawMic(micPath), rawReference(refPath);
        QVERIFY(rawMic.open(QIODevice::ReadOnly));
        QVERIFY(rawReference.open(QIODevice::ReadOnly));
        QCOMPARE(parseWavPcm(rawMic.readAll()).samples, microphone);
        QCOMPARE(parseWavPcm(rawReference.readAll()).samples, playback);
    }
    void extractsRawVocalAndSeparateReference() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto mic = tone(3000), reference = tone(12000);
        const auto micPath = dir.filePath("mic.wav"), refPath = dir.filePath("backing.wav");
        QVERIFY(write(micPath, mic));
        QVERIFY(write(refPath, reference));
        PreviewJob job;
        QSignalSpy extracted(&job, &PreviewJob::extracted), failed(&job, &PreviewJob::extractionFailed);
        PreviewJob::ExtractParams params;
        params.sourceFile = micPath;
        params.playbackFile = refPath;
        params.destTempFile = dir.filePath("scratch.wav");
        job.extract(params);
        QVERIFY(extracted.wait(10000));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(extracted[0][0].toByteArray(), mic); // no gain/denoise/mastering before AEC
        QCOMPARE(qvariant_cast<QAudioFormat>(extracted[0][1]), format());
        QCOMPARE(extracted[0][2].toByteArray(), reference);
        QVERIFY(!QFile::exists(params.destTempFile));
        QFile original(micPath);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(parseWavPcm(original.readAll()).samples, mic);
    }
    void failedReferenceDoesNotReportSuccess() {
        QTemporaryDir dir;
        const QString micPath = dir.filePath("mic.wav");
        QVERIFY(write(micPath, tone(3000)));
        PreviewJob job;
        QSignalSpy extracted(&job, &PreviewJob::extracted), failed(&job, &PreviewJob::extractionFailed);
        PreviewJob::ExtractParams params;
        params.sourceFile = micPath;
        params.playbackFile = dir.filePath("missing.wav");
        params.destTempFile = dir.filePath("scratch.wav");
        job.extract(params);
        QVERIFY(failed.wait(10000));
        QCOMPARE(extracted.size(), 0);
        QVERIFY(!failed[0][0].toString().isEmpty());
        QCOMPARE(failed[0][1].toBool(), false);
        QVERIFY(!QFile::exists(params.destTempFile));
    }
    void cancelledExtractionCannotPublishAudio() {
        QTemporaryDir dir;
        const QString micPath = dir.filePath("mic.wav");
        QVERIFY(write(micPath, tone(3000)));
        PreviewJob job;
        QSignalSpy extracted(&job, &PreviewJob::extracted), failed(&job, &PreviewJob::extractionFailed);
        PreviewJob::ExtractParams params;
        params.sourceFile = micPath;
        params.destTempFile = dir.filePath("scratch.wav");
        job.extract(params);
        job.waitForIdle();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
        QCOMPARE(extracted.size(), 0);
        QCOMPARE(failed[0][1].toBool(), true);
        QVERIFY(failed[0][0].toString().isEmpty());
    }

    void replacementExtractionOnlyPublishesNewTake() {
        QTemporaryDir dir;
        const QString firstPath = dir.filePath("first.wav"), secondPath = dir.filePath("second.wav");
        const QByteArray first = tone(3000), second = tone(7000);
        QVERIFY(write(firstPath, first));
        QVERIFY(write(secondPath, second));
        PreviewJob job;
        QSignalSpy extracted(&job, &PreviewJob::extracted), failed(&job, &PreviewJob::extractionFailed);
        PreviewJob::ExtractParams params;
        params.sourceFile = firstPath;
        params.destTempFile = dir.filePath("scratch.wav");
        job.extract(params);
        params.sourceFile = secondPath;
        job.extract(params);
        QVERIFY(extracted.wait(10000));
        QCOMPARE(extracted.size(), 1);
        QCOMPARE(extracted[0][0].toByteArray(), second);
        // A replaced run may report cancellation, but never a decode error.
        for (const auto &failure : failed) QVERIFY(failure[1].toBool());
    }
    void missingReferenceForEnabledCleanupFailsClearly() {
        PreviewJob job;
        QSignalSpy failed(&job, &PreviewJob::enhancementFailed), enhanced(&job, &PreviewJob::enhanced);
        PreviewJob::EnhanceParams params;
        params.removePlaybackBleed = true;
        QVERIFY(job.enhance(tone(3000), format(), params));
        QVERIFY(enhanced.wait(10000));
        QCOMPARE(failed.size(), 1);
        QVERIFY(enhanced[0][0].toByteArray().isEmpty());
    }
    void cancelledEnhancementCannotPublishProcessedAudio() {
        PreviewJob job;
        QSignalSpy enhanced(&job, &PreviewJob::enhanced);
        PreviewJob::EnhanceParams params;
        params.pitchCorrectionAmount = 0;
        params.noiseReductionAmount = 0;
        QVERIFY(job.enhance(tone(3000), format(), params));
        job.waitForIdle();
        // The finished callback is still queued: a second run must wait for
        // the first result instead of changing the first run's cancel flag.
        QVERIFY(job.isEnhancing());
        QVERIFY(!job.enhance(tone(7000), format(), params));
        QTRY_COMPARE_WITH_TIMEOUT(enhanced.size(), 1, 10000);
        QVERIFY(enhanced[0][0].toByteArray().isEmpty());
        QVERIFY(!job.isEnhancing());
    }
    void masteringRunsAfterEnhancement() {
        PreviewJob job;
        QSignalSpy failed(&job, &PreviewJob::enhancementFailed), enhanced(&job, &PreviewJob::enhanced);
        PreviewJob::EnhanceParams params;
        params.pitchCorrectionAmount = 0;
        params.noiseReductionAmount = 0;
        QVERIFY(job.enhance(tone(3000), format(), params));
        QVERIFY(enhanced.wait(10000));
        QCOMPARE(failed.size(), 0);
        QVERIFY(!enhanced[0][0].toByteArray().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestPreviewJob)
#include "test_previewjob.moc"
