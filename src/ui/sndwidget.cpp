#include "sndwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMediaDevices>

#include <algorithm>

SndWidget::SndWidget(QWidget *parent)
    : QWidget(parent),
      timer(new QTimer(this))
{
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    connect(timer, &QTimer::timeout, this, &SndWidget::updateWaveform);
    timer->start(100);
}

SndWidget::~SndWidget()
{
    if (audioSource)
        audioSource->stop();
}

void SndWidget::setInputDevice(const QAudioDevice &device)
{
    // Tear down any existing source cleanly
    if (audioSource) {
        disconnect(audioSource, nullptr, this, nullptr);
        audioSource->stop();
        delete audioSource;
        audioSource   = nullptr;
        m_pullDevice  = nullptr;
    }

    m_device = device;
    audioSource = new QAudioSource(device, format, this);

    connect(audioSource, &QAudioSource::stateChanged,
            this, &SndWidget::onAudioStateChanged,
            Qt::UniqueConnection);

    // Pull mode: QAudioSource owns an internal ring buffer; we read from
    // the returned QIODevice on every timer tick.  No shared QBuffer, no
    // concurrent-write data race.
    m_pullDevice = audioSource->start();
}

void SndWidget::processBuffer()
{
    if (!m_pullDevice || !audioSource)
        return;

    // Read whatever has accumulated since the last tick
    const qint64 available = m_pullDevice->bytesAvailable();
    if (available <= 0)
        return;

    QByteArray data = m_pullDevice->read(available);
    if (data.isEmpty())
        return;

    const int numSamples = data.size() / int(sizeof(qint16));
    const qint16 *samples = reinterpret_cast<const qint16 *>(data.constData());

    QVector<qint16> chunk(numSamples);
    for (int i = 0; i < numSamples; ++i)
        chunk[i] = samples[i];

    {
        QMutexLocker locker(&bufferMutex);
        audioData = chunk;
    }

    emit audioChunkReady(chunk);
}

void SndWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    painter.setPen(Qt::green);

    QMutexLocker locker(&bufferMutex);
    const int numSamples = audioData.size();
    if (numSamples == 0)
        return;

    const int width  = this->width();
    const int height = this->height();
    const int middle = height / 2;
    constexpr float kSensitivity = 3.5f;

    for (int i = 0; i < width; ++i) {
        const int idx = (i * numSamples) / width;
        if (idx >= numSamples) break;
        const float v = std::clamp(audioData[idx] * kSensitivity, -32768.0f, 32767.0f);
        const int y   = middle - int(v * middle / 32768.0f);
        painter.drawLine(i, middle, i, y);
    }
}

void SndWidget::updateWaveform()
{
    processBuffer();
    update();
}

void SndWidget::onAudioStateChanged(QAudio::State state)
{
    // On Windows (WASAPI) IdleState fires whenever the capture buffer
    // momentarily drains — that is normal and must not stop the source.
    // Only react to a hard stop caused by an actual device error.
    if (state != QAudio::StoppedState
        || !audioSource
        || audioSource->error() == QAudio::NoError)
        return;

    // Only worth restarting if the device is actually still there — e.g. a
    // transient underrun. If it's gone (unplugged), retrying in a loop
    // against a dead device achieves nothing; leave it stopped and let
    // MainWindow's device-hotplug handling notice the mic is gone and
    // eventually call setInputDevice() again once the user picks a new one.
    const auto inputs = QMediaDevices::audioInputs();
    const bool stillPresent = std::any_of(inputs.begin(), inputs.end(),
        [this](const QAudioDevice &d) { return d.id() == m_device.id(); });

    if (stillPresent)
        m_pullDevice = audioSource->start();
    else
        m_pullDevice = nullptr;
}
