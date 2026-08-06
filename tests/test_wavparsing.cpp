#include "complexes.h"

#include <QTest>
#include <QTemporaryFile>
#include <QAudioFormat>

// parseWavPcm() walks real RIFF chunk structure (see complexes.cpp) instead
// of assuming a fixed 44-byte header — these tests exist to pin that
// behavior down, since a regression here would silently feed header bytes
// into the audio pipeline as if they were PCM samples.
class TestWavParsing : public QObject
{
    Q_OBJECT

private:
    static QByteArray makeMinimalWav(quint16 channels, quint32 sampleRate,
                                      quint16 bitsPerSample, const QByteArray &pcmData)
    {
        QAudioFormat fmt;
        fmt.setChannelCount(channels);
        fmt.setSampleRate(int(sampleRate));
        fmt.setSampleFormat(bitsPerSample == 16 ? QAudioFormat::Int16 : QAudioFormat::UInt8);

        QTemporaryFile tmp;
        if (!tmp.open())
            return QByteArray();
        writeWavHeader(tmp, fmt, pcmData.size(), pcmData);
        tmp.seek(0);
        return tmp.readAll();
    }

private slots:
    void roundTrip_stereo16bit()
    {
        const QByteArray pcm(64, '\x11'); // 16 stereo int16 frames
        const QByteArray wav = makeMinimalWav(2, 44100, 16, pcm);
        QVERIFY(!wav.isEmpty());

        const PcmBuffer result = parseWavPcm(wav);

        QVERIFY(result.isValid());
        QCOMPARE(result.samples, pcm);
        QCOMPARE(result.format.channelCount(), 2);
        QCOMPARE(result.format.sampleRate(), 44100);
        QCOMPARE(result.format.sampleFormat(), QAudioFormat::Int16);
    }

    void roundTrip_mono8bit()
    {
        const QByteArray pcm(32, '\x22');
        const QByteArray wav = makeMinimalWav(1, 22050, 8, pcm);

        const PcmBuffer result = parseWavPcm(wav);

        QVERIFY(result.isValid());
        QCOMPARE(result.samples, pcm);
        QCOMPARE(result.format.channelCount(), 1);
        QCOMPARE(result.format.sampleRate(), 22050);
    }

    // A LIST/INFO chunk (or anything else) between "fmt " and "data" is
    // legal RIFF and common from real encoders — the parser must skip over
    // it via chunkSize, not assume "data" always follows "fmt " directly.
    void extraChunkBeforeData_isSkipped()
    {
        QByteArray wav = makeMinimalWav(2, 44100, 16, QByteArray(16, '\x33'));
        QVERIFY(!wav.isEmpty());

        // Splice a small even-sized "JUNK" chunk in right after the 36-byte
        // 'fmt ' block (12 RIFF header + 8 'fmt ' chunk header + 16 fmt data
        // = 36), before "data" begins.
        QByteArray junkChunk;
        junkChunk.append("JUNK");
        const quint32 junkSize = 4;
        junkChunk.append(reinterpret_cast<const char *>(&junkSize), 4);
        junkChunk.append("padd");

        QByteArray spliced = wav.left(36) + junkChunk + wav.mid(36);
        // Fix up the RIFF chunk size (bytes 4..7) for the inserted bytes.
        qint32 riffSize = 0;
        memcpy(&riffSize, spliced.constData() + 4, 4);
        riffSize += junkChunk.size();
        memcpy(spliced.data() + 4, &riffSize, 4);

        const PcmBuffer result = parseWavPcm(spliced);

        QVERIFY(result.isValid());
        QCOMPARE(result.samples, QByteArray(16, '\x33'));
    }

    void notRiff_isRejected()
    {
        QVERIFY(!parseWavPcm(QByteArray("not a wav file at all")).isValid());
    }

    void truncatedBuffer_isRejected()
    {
        const QByteArray wav = makeMinimalWav(2, 44100, 16, QByteArray(16, '\x44'));
        QVERIFY(!parseWavPcm(wav.left(20)).isValid());
    }

    void dataBeforeFmt_isRejected()
    {
        QByteArray wav;
        wav.append("RIFF\x24\x00\x00\x00WAVE", 12);
        wav.append("data\x04\x00\x00\x00", 8);
        wav.append(QByteArray(4, '\x00'));
        QVERIFY(!parseWavPcm(wav).isValid());
    }

    void emptyBuffer_isRejected()
    {
        QVERIFY(!parseWavPcm(QByteArray()).isValid());
    }
};

QTEST_MAIN(TestWavParsing)
#include "test_wavparsing.moc"
