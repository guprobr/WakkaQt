#include "vocalenhancer.h"

#include <QTest>
#include <QAudioFormat>
#include <QScopedPointer>

// VocalEnhancer's DSP internals (pitch detection, LPC, phase vocoder) are
// private and only reachable through enhance(), whose correctness depends on
// real audio content — not a good fit for a fast, deterministic unit test.
// What *is* cheap and deterministic, and just as important to pin down, is
// every user-tweakable knob's clamping: enhance()'s hot loops trust these
// values are already in-range (see e.g. maxCorrectionCents/maxRatio math in
// correctPitchChunk()), so a knob that silently stopped clamping would send
// out-of-range values straight into that math instead of failing loudly here.
class TestVocalEnhancer : public QObject
{
    Q_OBJECT

private:
    QScopedPointer<VocalEnhancer> m_enh;

private slots:
    // FFTW plan creation (FFTW_MEASURE) happens once in the constructor —
    // shared across all test functions in this class instead of paying that
    // cost per test slot. None of the tests below mutate anything the
    // others depend on (each sets its own knob and reads it straight back).
    void initTestCase()
    {
        QAudioFormat fmt;
        fmt.setSampleRate(44100);
        fmt.setChannelCount(2);
        fmt.setSampleFormat(QAudioFormat::Int16);
        m_enh.reset(new VocalEnhancer(fmt));
    }

    void pitchCorrectionAmount_isClampedTo0_1()
    {
        m_enh->setPitchCorrectionAmount(0.5);
        QCOMPARE(m_enh->getPitchCorrectionAmount(), 0.5);
        m_enh->setPitchCorrectionAmount(-1.0);
        QCOMPARE(m_enh->getPitchCorrectionAmount(), 0.0);
        m_enh->setPitchCorrectionAmount(2.0);
        QCOMPARE(m_enh->getPitchCorrectionAmount(), 1.0);
    }

    void noiseReductionAmount_isClampedTo0_1()
    {
        m_enh->setNoiseReductionAmount(0.7);
        QCOMPARE(m_enh->getNoiseReductionAmount(), 0.7);
        m_enh->setNoiseReductionAmount(-5.0);
        QCOMPARE(m_enh->getNoiseReductionAmount(), 0.0);
        m_enh->setNoiseReductionAmount(5.0);
        QCOMPARE(m_enh->getNoiseReductionAmount(), 1.0);
    }

    void retuneSpeed_isClampedTo0_5000ms()
    {
        m_enh->setRetuneSpeed(150.0);
        QCOMPARE(m_enh->getRetuneSpeed(), 150.0);
        m_enh->setRetuneSpeed(-10.0);
        QCOMPARE(m_enh->getRetuneSpeed(), 0.0);
        m_enh->setRetuneSpeed(9999.0);
        QCOMPARE(m_enh->getRetuneSpeed(), 5000.0);
    }

    void reverbParams_areClampedTo0_1()
    {
        m_enh->setReverbRoomSize(1.5);
        QCOMPARE(m_enh->getReverbRoomSize(), 1.0);
        m_enh->setReverbRoomSize(-0.5);
        QCOMPARE(m_enh->getReverbRoomSize(), 0.0);

        m_enh->setReverbDecay(1.5);
        QCOMPARE(m_enh->getReverbDecay(), 1.0);

        m_enh->setReverbMix(0.42);
        QCOMPARE(m_enh->getReverbMix(), 0.42);
    }

    void setScale_keyNoteIsClampedTo0_11()
    {
        m_enh->setScale(-5, {0, 2, 4, 5, 7, 9, 11});
        QCOMPARE(m_enh->getKeyNote(), 0);
        m_enh->setScale(15, {0, 2, 4, 5, 7, 9, 11});
        QCOMPARE(m_enh->getKeyNote(), 11);
        m_enh->setScale(7, {0, 2, 4, 5, 7, 9, 11});
        QCOMPARE(m_enh->getKeyNote(), 7);
    }

    void setScalePreset_isCaseInsensitive()
    {
        m_enh->setScalePreset("MAJOR", 2);
        QCOMPARE(m_enh->getScaleName(), QString("Major"));
        QCOMPARE(m_enh->getKeyNote(), 2);

        m_enh->setScalePreset("bLuEs", 0);
        QCOMPARE(m_enh->getScaleName(), QString("Blues"));
    }

    void setScalePreset_unknownNameFallsBackToChromatic()
    {
        m_enh->setScalePreset("not_a_real_scale", 3);
        QCOMPARE(m_enh->getScaleName(), QString("Chromatic"));
    }
};

QTEST_MAIN(TestVocalEnhancer)
#include "test_vocalenhancer.moc"
