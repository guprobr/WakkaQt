#include "audiovisualizerwidget.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QDebug>

AudioVisualizerWidget::AudioVisualizerWidget(QWidget *parent)
    : QWidget(parent)
{
    
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AudioVisualizerWidget::updatePainter);
    timer->start(10);
}

AudioVisualizerWidget::~AudioVisualizerWidget()
{
    clear();
}

void AudioVisualizerWidget::updateVisualization(const QByteArray &audioData, const QAudioFormat &format)
{
    // Store audio data and format
    m_visualizationData = audioData;
    m_format = format;

}

void AudioVisualizerWidget::clear()
{
    // Clear the visualization data
    m_visualizationData.detach();
    m_visualizationData.clear();
    update(); // Request a repaint
}

void AudioVisualizerWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), Qt::black);

    QPen pen(Qt::darkYellow); // Color of the visualization
    pen.setStyle(Qt::PenStyle::DashLine);
    painter.setPen(pen);
    painter.setBrush(QBrush(Qt::darkGreen));

    // Calculate the width of each sample
    int width = this->width();
    int height = this->height();
    int bytesPerSample = m_format.bytesPerSample();
    
    // Check for valid bytes per sample
    if (bytesPerSample <= 0) {
        //qDebug() << "Invalid bytes per sample:" << bytesPerSample;
        return; // Exit if invalid
    }

    int numSamples = m_visualizationData.size() / bytesPerSample; // Calculate number of samples
    int step = qMax(1, numSamples / width); // Determine the step size for visualization

    // Draw the visualization
    for (int i = 0; i < numSamples; i += step) {
        int sampleValue = 0;

        // Read the sample value based on the sample format
        switch (m_format.sampleFormat()) {
            case QAudioFormat::Int16: {
                qint16 sample = *reinterpret_cast<const qint16*>(m_visualizationData.constData() + i * bytesPerSample);
                sampleValue = static_cast<int>(sample);
                break;
            }
            case QAudioFormat::UInt8: {
                quint8 sample = *reinterpret_cast<const quint8*>(m_visualizationData.constData() + i * bytesPerSample);
                sampleValue = static_cast<int>(sample) - 128; // Center the 8-bit sample
                break;
            }
            case QAudioFormat::Int32: {
                qint32 sample = *reinterpret_cast<const qint32*>(m_visualizationData.constData() + i * bytesPerSample);
                sampleValue = static_cast<int>(sample);
                break;
            }
            
            default:
                continue; // Skip if format is not handled
        }

        // Normalize sample value to height of the widget using bytesPerSample
        int normalizationFactor = (1 << (8 * bytesPerSample - 1));
        
        if (normalizationFactor == 0) {
            qDebug() << "Normalization factor is zero!";
            continue; // Skip if normalization factor is invalid
        }
        
        // Calculate the Y position based on the normalized value
        int y = height / 2 - (sampleValue * height / normalizationFactor);

        // Draw a vertical line for the sample
        painter.drawLine(i / step, height / 2, i / step, y);
    }
}

void AudioVisualizerWidget::updatePainter() {
    // Trigger a repaint to update the visualization
    update();
}