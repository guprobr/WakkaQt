#include "pitchmonitorwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

static constexpr double kPi = 3.14159265358979323846;

PitchMonitorWidget::PitchMonitorWidget(int sampleRate, QWidget *parent)
    : QWidget(parent), m_sampleRate(sampleRate)
{
    setMinimumSize(320, 80);
    setMaximumHeight(90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip("Real-time pitch monitor — shows the note you are currently singing");
}

void PitchMonitorWidget::reset()
{
    QMutexLocker lk(&m_mutex);
    m_accumulator.clear();
    m_detectedHz    = 0.0;
    m_note          = {"", 0, 0.0, false};
    m_smoothedCents = 0.0;
    update();
}

// ── Audio processing ────────────────────────────────────────────────────────

void PitchMonitorWidget::onAudioChunk(const QVector<qint16> &samples)
{
    // Append and normalize to [-1, 1]
    {
        QMutexLocker lk(&m_mutex);
        for (qint16 s : samples)
            m_accumulator.append(s / 32768.0);

        // Keep at most 4096 samples in the accumulator
        constexpr int kMaxAcc = 4096;
        if (m_accumulator.size() > kMaxAcc)
            m_accumulator = m_accumulator.mid(m_accumulator.size() - kMaxAcc);
    }

    // Run detection when we have >= 2048 samples
    constexpr int kDetWin = 2048;
    QVector<double> buf;
    {
        QMutexLocker lk(&m_mutex);
        if (m_accumulator.size() < kDetWin) return;
        buf = m_accumulator;
    }

    const double hz = detectPitchYIN(buf);

    {
        QMutexLocker lk(&m_mutex);
        m_detectedHz = hz;
        m_note = hzToNote(hz);
        // EMA smooth the deviation bar (α = 0.3 → responsive but not jittery)
        if (m_note.valid)
            m_smoothedCents = 0.7 * m_smoothedCents + 0.3 * m_note.cents;
        else
            m_smoothedCents *= 0.85; // decay toward zero when silent
    }

    // Schedule a repaint (thread-safe via queued connection to main thread)
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

// ── YIN pitch detection ─────────────────────────────────────────────────────

double PitchMonitorWidget::detectPitchYIN(const QVector<double> &data) const
{
    const int W = std::min<int>(data.size(), 2048);
    if (W < 512) return 0.0;

    const int minTau = std::max(2, int(double(m_sampleRate) / 1100.0));
    const int maxTau = std::min(W / 2, int(double(m_sampleRate) / 60.0));
    if (minTau >= maxTau) return 0.0;

    // Find highest-RMS block of W samples
    int bestStart = 0;
    double bestRms = 0.0;
    for (int i = 0; i + W <= int(data.size()); i += W / 4) {
        double rms = 0.0;
        for (int j = 0; j < W; ++j) rms += data[i+j] * data[i+j];
        rms = std::sqrt(rms / W);
        if (rms > bestRms) { bestRms = rms; bestStart = i; }
    }
    if (bestRms < 5e-4) return 0.0; // silence

    const int half = W / 2;

    // Step 1: Difference function
    QVector<double> d(maxTau + 1, 0.0);
    for (int tau = 1; tau <= maxTau; ++tau) {
        double sum = 0.0;
        for (int j = 0; j < half; ++j) {
            const double diff = data[bestStart + j] - data[bestStart + j + tau];
            sum += diff * diff;
        }
        d[tau] = sum;
    }

    // Step 2: CMND
    QVector<double> cmnd(maxTau + 1, 1.0);
    double cumSum = 0.0;
    for (int tau = 1; tau <= maxTau; ++tau) {
        cumSum += d[tau];
        cmnd[tau] = (cumSum > 1e-12) ? d[tau] * tau / cumSum : 1.0;
    }

    // Step 3: First tau below threshold
    constexpr double kThresh = 0.12;
    int bestTau = -1;
    for (int tau = minTau; tau < maxTau; ++tau) {
        if (cmnd[tau] < kThresh) {
            while (tau + 1 <= maxTau && cmnd[tau+1] <= cmnd[tau]) ++tau;
            bestTau = tau;
            break;
        }
    }
    if (bestTau < 1) {
        int minIdx = minTau;
        for (int tau = minTau+1; tau <= maxTau; ++tau)
            if (cmnd[tau] < cmnd[minIdx]) minIdx = tau;
        if (cmnd[minIdx] > 0.50) return 0.0;
        bestTau = minIdx;
    }

    // Step 4: Parabolic interpolation
    double refined = double(bestTau);
    if (bestTau > minTau && bestTau < maxTau) {
        const double y0 = cmnd[bestTau-1], y1 = cmnd[bestTau], y2 = cmnd[bestTau+1];
        const double den = y0 - 2.0*y1 + y2;
        if (std::abs(den) > 1e-12)
            refined = bestTau + 0.5*(y0 - y2) / den;
    }

    const double freq = double(m_sampleRate) / std::max(refined, 1.0);
    return (freq > 50.0 && freq < 1200.0) ? freq : 0.0;
}

// ── Note name conversion ────────────────────────────────────────────────────

PitchMonitorWidget::NoteInfo PitchMonitorWidget::hzToNote(double hz)
{
    if (hz <= 0.0) return {"", 0, 0.0, false};

    static const char* kNames[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };

    // A4 = 69 MIDI = 440 Hz
    const double midi  = 69.0 + 12.0 * std::log2(hz / 440.0);
    const int    midiI = int(std::round(midi));
    const double cents = (midi - midiI) * 100.0;

    const int noteIdx = ((midiI % 12) + 12) % 12;
    const int octave  = midiI / 12 - 1;

    return { kNames[noteIdx], octave, cents, true };
}

// ── Painting ────────────────────────────────────────────────────────────────

void PitchMonitorWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(18, 18, 18));

    NoteInfo note;
    double smoothedCents;
    {
        QMutexLocker lk(&m_mutex);
        note          = m_note;
        smoothedCents = m_smoothedCents;
    }

    const int W = width();
    const int H = height();

    // ── Note name (large, centred) ────────────────────────────────────────
    const QString noteStr = note.valid
        ? QString("%1%2").arg(note.name).arg(note.octave)
        : "—";

    QFont noteFont("Monospace", 28, QFont::Bold);
    p.setFont(noteFont);

    // Colour: green ±15 c, yellow ±30 c, red beyond
    QColor noteColor = Qt::darkGray;
    if (note.valid) {
        const double absCents = std::abs(smoothedCents);
        if (absCents <= 15.0)       noteColor = QColor(60, 220, 60);
        else if (absCents <= 30.0)  noteColor = QColor(220, 200, 40);
        else                        noteColor = QColor(220, 60, 60);
    }
    p.setPen(noteColor);

    QFontMetrics fm(noteFont);
    const int noteW = fm.horizontalAdvance(noteStr);
    p.drawText((W / 3 - noteW) / 2, H / 2 + fm.ascent() / 2 - 4, noteStr);

    // ── Cents deviation bar ───────────────────────────────────────────────
    const int barX = W / 3;
    const int barW = W - barX - 10;
    const int barH = 18;
    const int barY = (H - barH) / 2;

    // Track outline
    p.setPen(QColor(60, 60, 60));
    p.setBrush(QColor(30, 30, 30));
    p.drawRoundedRect(barX, barY, barW, barH, 4, 4);

    // Centre tick
    p.setPen(QColor(100, 100, 100));
    p.drawLine(barX + barW/2, barY + 2, barX + barW/2, barY + barH - 2);

    // Filled region
    if (note.valid) {
        const double fraction = std::clamp(smoothedCents / 50.0, -1.0, 1.0);
        const int centre = barX + barW / 2;
        const int fill   = int(fraction * (barW / 2));
        const int rx = (fill >= 0) ? centre : centre + fill;
        const int rw = std::abs(fill);
        if (rw > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(noteColor);
            p.drawRoundedRect(rx, barY + 3, rw, barH - 6, 3, 3);
        }
    }

    // Cents label
    QFont smallFont("Monospace", 9);
    p.setFont(smallFont);
    p.setPen(QColor(160, 160, 160));
    const QString centsStr = note.valid
        ? QString("%1%2¢").arg(smoothedCents >= 0 ? "+" : "").arg(int(smoothedCents))
        : "";
    p.drawText(barX + barW + 2, barY + barH - 3, centsStr);

    // Labels
    p.setPen(QColor(80, 80, 80));
    QFont tinyFont("Monospace", 7);
    p.setFont(tinyFont);
    p.drawText(barX + 2,       barY + barH + 11, "-50¢");
    p.drawText(barX + barW/2 - 6, barY + barH + 11, "0");
    p.drawText(barX + barW - 20,  barY + barH + 11, "+50¢");
}
