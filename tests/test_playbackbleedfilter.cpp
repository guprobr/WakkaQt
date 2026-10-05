#include "playbackbleedfilter.h"
#include <QtTest>
#include <QtEndian>
#include <cmath>
#include <random>

class TestPlaybackBleedFilter : public QObject {
    Q_OBJECT
    static QAudioFormat format(int channels = 1) {
        QAudioFormat f;
        f.setSampleRate(44100);
        f.setChannelCount(channels);
        f.setSampleFormat(QAudioFormat::Int16);
        return f;
    }
    static QByteArray encode(const QVector<double> &samples) {
        QByteArray bytes(samples.size() * 2, 0);
        for (qsizetype i = 0; i < samples.size(); ++i)
            qToLittleEndian<qint16>(qint16(std::clamp(samples[i], -1.0, 1.0) * 32767), bytes.data() + 2 * i);
        return bytes;
    }
    static double value(const QByteArray &pcm, qsizetype i) {
        return qFromLittleEndian<qint16>(pcm.constData() + 2 * i) / 32767.0;
    }
private slots:
    void invalidFormatAndCancellation() {
        auto f = format();
        f.setSampleFormat(QAudioFormat::Float);
        QVERIFY(PlaybackBleedFilter::process(QByteArray(100, 0), QByteArray(100, 0), f).error.contains("16-bit"));
        f = format();
        f.setSampleRate(192000);
        const auto unsupportedRate = PlaybackBleedFilter::process(QByteArray(100, 0), QByteArray(100, 0), f);
        QVERIFY(unsupportedRate.error.contains("192000 Hz"));
        QVERIFY(!unsupportedRate.error.contains("16-bit"));
        std::atomic<bool> cancelled{true};
        auto r = PlaybackBleedFilter::process(QByteArray(100, 0), QByteArray(100, 0), format(), 0, &cancelled);
        QVERIFY(r.samples.isEmpty());
        QVERIFY(r.error.isEmpty());
    }
    void silentOrUnrelatedReferencePreservesTake() {
        if (!PlaybackBleedFilter::isAvailable()) QSKIP("SpeexDSP runtime not installed");
        std::mt19937 rng(24);
        std::uniform_real_distribution<double> noise(-0.2, 0.2);
        QVector<double> vocal(44100), unrelated(44100);
        for (auto &s : vocal) s = noise(rng);
        for (auto &s : unrelated) s = noise(rng);
        const QByteArray mic = encode(vocal);
        for (const auto &ref : {QByteArray(mic.size(), 0), encode(unrelated)}) {
            const auto r = PlaybackBleedFilter::process(mic, ref, format());
            QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
            QVERIFY(!r.applied);
            QCOMPARE(r.samples, mic);
        }
    }
    void removesDelayedPlaybackAndPreservesSinger_data() {
        QTest::addColumn<int>("channels");
        QTest::addColumn<int>("delay");
        QTest::addColumn<int>("startFrame");
        QTest::addColumn<int>("voiceStart");
        QTest::newRow("mono delayed") << 1 << 5292 << 0 << 44100 * 2;
        QTest::newRow("mono capture gate") << 1 << -3087 << 0 << 44100 * 2;
        QTest::newRow("stereo room") << 2 << 2205 << 0 << 44100 * 2;
        QTest::newRow("snippet full reference") << 2 << 2205 << 44100 * 3 << 44100 * 2;
        QTest::newRow("singing from start") << 1 << 2205 << 0 << 0;
    }
    void removesDelayedPlaybackAndPreservesSinger() {
        if (!PlaybackBleedFilter::isAvailable()) QSKIP("SpeexDSP runtime not installed");
        QFETCH(int, channels);
        QFETCH(int, delay);
        QFETCH(int, startFrame);
        QFETCH(int, voiceStart);
        const int rate = 44100, frames = rate * 8 + 123;
        const int referenceFrames = frames + startFrame + rate;
        std::mt19937 rng(76);
        std::uniform_real_distribution<double> noise(-0.3, 0.3);
        QVector<double> reference(referenceFrames * channels), mic(frames * channels), singer(mic.size());
        for (auto &s : reference) s = noise(rng);
        auto ref = [&](int frame, int c) {
            return frame >= 0 && frame < referenceFrames ? reference[frame * channels + c] : 0.0;
        };
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < channels; ++c) {
                // Singer starts during playback, after an instrumental intro.
                const double voice = i > voiceStart ? 0.10 * std::sin(2 * 3.141592653589793 * (233 + c * 47) * i / rate) : 0;
                singer[i * channels + c] = voice;
                mic[i * channels + c] = voice + 0.5 * ref(startFrame + i - delay, c)
                    + 0.18 * ref(startFrame + i - delay - rate / 25, c);
                if (channels == 2) mic[i * channels + c] += 0.15 * ref(startFrame + i - delay - 331, 1 - c);
            }
        }
        int lastProgress = -1;
        const auto r = PlaybackBleedFilter::process(encode(mic), encode(reference), format(channels), startFrame,
            nullptr, [&](int p) { lastProgress = p; });
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.applied);
        QCOMPARE(r.delaySamples, delay);
        QCOMPARE(r.samples.size(), mic.size() * 2);
        QCOMPARE(lastProgress, 100);
        double before = 0, after = 0, voicePower = 0, voiceProjection = 0;
        for (int i = rate * 4 * channels; i < mic.size(); ++i) {
            const double out = value(r.samples, i);
            before += std::pow(mic[i] - singer[i], 2);
            after += std::pow(out - singer[i], 2);
            voicePower += singer[i] * singer[i];
            voiceProjection += out * singer[i];
        }
        const double reductionDb = 10 * std::log10(before / after);
        qInfo() << "Residual reduction:" << reductionDb << "dB; singer gain:" << voiceProjection / voicePower;
        QVERIFY2(reductionDb > 10, "Expected at least 10 dB reduction while the singer is active");
        QVERIFY(voiceProjection / voicePower > 0.85);
        QVERIFY(voiceProjection / voicePower < 1.15);
    }
    void cancellationDuringProcessing() {
        if (!PlaybackBleedFilter::isAvailable()) QSKIP("SpeexDSP runtime not installed");
        std::atomic<bool> cancelled{false};
        QVector<double> audio(44100 * 2);
        std::mt19937 rng(8);
        for (auto &s : audio) s = (int(rng() % 2000) - 1000) / 10000.0;
        const auto pcm = encode(audio);
        const auto r = PlaybackBleedFilter::process(pcm, pcm, format(), 0, &cancelled,
            [&](int p) { if (p > 10) cancelled.store(true); });
        QVERIFY(cancelled.load());
        QVERIFY(r.samples.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestPlaybackBleedFilter)
#include "test_playbackbleedfilter.moc"
