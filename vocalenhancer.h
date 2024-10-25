#ifndef VOCALENHANCER_H
#define VOCALENHANCER_H

#include <QAudioFormat>
#include <QVector>
#include <QByteArray>
#include <fftw3.h>
#include <complex>

class VocalEnhancer {
public:
    explicit VocalEnhancer(const QAudioFormat& format);
    QByteArray enhance(const QByteArray& input);

private:
    static const double A440; // Frequency of A4
    static const int MIDI_NOTES; // Total MIDI notes
    static const double NOTE_FREQUENCIES[128];

    int m_sampleSize;
    int m_sampleRate;
    int m_numSamples;
    QVector<std::complex<double>> m_fftIn;
    QVector<std::complex<double>> m_fftOut;
    fftw_plan m_fftPlan;
    fftw_plan m_ifftPlan;

    void phaseVocoder(QVector<double>& inputData, double pitchShiftRatio, int frameSize, int hopSize);
    void processPitchCorrection(QVector<double>& inputData);
    double detectPitch(const QVector<double>& inputData);
    QVector<double> pitchShiftPSOLA(const QVector<double>& inputData, double shiftRatio);
    QVector<double> timeStretch(const QVector<double>& segment, double shiftRatio);
    double findClosestNoteFrequency(double frequency);
    double calculateDuration(int sampleRate);

    void applyHanningWindow(QVector<double>& frame);
    double cubicInterpolate(double y0, double y1, double y2, double y3, double mu);
    void harmonicExciter(QVector<double>& inputData, double gain, double threshold);
    void compressDynamics(QVector<double>& inputData, double threshold, double ratio);

};

#endif // VOCALENHANCER_H
