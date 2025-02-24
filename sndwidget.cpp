#include "sndwidget.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QPaintEvent>

#include <cstring> // for std::memcpy
#include <algorithm> // for std::clamp

SndWidget::SndWidget(QWidget *parent) 
    : QWidget(parent), 
      timer(new QTimer(this)),
      audioSource(nullptr),
      audioBuffer(new QBuffer(this)) {

    // Set up the default audio format
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    connect(timer, &QTimer::timeout, this, &SndWidget::updateWaveform);
    timer->start(111); // Update every 111 milliseconds
}

SndWidget::~SndWidget() {
    if (audioSource) {
        audioSource->stop();
    }
}

void SndWidget::setInputDevice(const QAudioDevice &device) {

    if (audioSource) {
        audioSource->stop();
    }

    // Initialize the audio source with the new device
    audioSource = new QAudioSource(device, format, this);
    audioBuffer->open(QIODevice::ReadWrite);

    connect(audioSource, &QAudioSource::stateChanged, this, &SndWidget::onAudioStateChanged);
    audioSource->start(audioBuffer);
}

void SndWidget::processBuffer() {
    QMutexLocker locker(&bufferMutex);
    
    // Retrieve data from the buffer
    QByteArray data = audioBuffer->data();
    if (data.isEmpty()) return;

    // Process the data
    const qint16 *samples = reinterpret_cast<const qint16 *>(data.constData());
    int numSamples = data.size() / sizeof(qint16);

    // Clear the previous data
    audioData.clear();
    audioData.reserve(numSamples);  // Reserve memory to avoid reallocations
    
    // Append the new data
    for (int i = 0; i < numSamples; ++i) {
        audioData.append(samples[i]);
    }

    // Clear the buffer
    audioBuffer->buffer().clear();
    audioBuffer->seek(0);  // Reset the buffer to the beginning
}


void SndWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    painter.setPen(Qt::green);

    QMutexLocker locker(&bufferMutex);
    int width = this->width();
    int height = this->height();
    int middle = height / 2;
    int numSamples = audioData.size();

    if (numSamples == 0) return;

    // Sensitivity factor to amplify or attenuate the waveform
    float sensitivity = 2.5f; // Increase or decrease this value (e.g., 1.0f for no change)

    // Draw the waveform
    for (int i = 0; i < width; ++i) {
        int sampleIndex = (i * numSamples) / width;
        if (sampleIndex >= numSamples) break; // Avoid out-of-bounds access
        int sampleValue = audioData[sampleIndex];

        // Scale the sample to fit in the widget's height
        int y = middle - (std::clamp(sampleValue * sensitivity, -32768.0f, 32767.0f) * middle / 32768);
        painter.drawLine(i, middle, i, y);
    }
}

void SndWidget::updateWaveform() {
    processBuffer(); // Process the buffer to update audioData
    update(); // Trigger a repaint
}

void SndWidget::onAudioStateChanged(QAudio::State state) {
    if (state == QAudio::IdleState) {
        audioSource->stop();
        audioBuffer->close();
    }
}
