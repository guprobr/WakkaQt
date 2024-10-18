#include "audiovisualizerwidget.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QRandomGenerator>
#include <QDebug>

AudioVisualizerWidget::AudioVisualizerWidget(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::Shape::HLine);

    //QPalette palette = this->palette();
    //bgColor = palette.color(QPalette::Alternate);

    // change the brush color every six seconds
    QTimer *colorTimer = new QTimer(this);
    connect(colorTimer, &QTimer::timeout, this, [this]() {
        // Generate random RGB values :)
        int red = QRandomGenerator::global()->bounded(256);
        int green = QRandomGenerator::global()->bounded(256);
        int blue = QRandomGenerator::global()->bounded(256);

        // Set a random brush color
        m_brush = QBrush(QColor(red, green, blue));

        update();
    });

    colorTimer->start(3333);
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AudioVisualizerWidget::updatePainter);
    timer->start(11);


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
    QFrame::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    //painter.fillRect(rect(), bgColor);

    // Ensure that the brush is set for filling
    painter.setBrush(m_brush);

    QPen pen(Qt::yellow); // Color of the rectangle border
    pen.setStyle(Qt::PenStyle::DotLine);
    painter.setPen(pen);

    // Get widget dimensions
    int widgetWidth = this->width();
    int widgetHeight = this->height();

    // Check if dimensions are valid
    if (widgetWidth <= 0 || widgetHeight <= 0) {
        return; // Exit if invalid widget size
    }

    // Get the number of bytes per sample
    int bytesPerSample = m_format.bytesPerSample();

    // Check for valid audio data and bytes per sample
    if (m_visualizationData.isEmpty() || bytesPerSample <= 0) {
        return; // Exit if there's no audio data or invalid format
    }

    // Calculate number of samples
    int numSamples = m_visualizationData.size() / bytesPerSample;

    // Check if there are samples to visualize
    if (numSamples <= 0) {
        return; // Exit if no samples
    }

    // Determine step size for visualization
    int step = qMax(1, numSamples / widgetWidth);
    int barWidth = qMax(1, widgetWidth / numSamples);

    // Draw filled rectangles (bars) for each audio sample
    for (int i = 0; i < numSamples; i += step) {
        int sampleValue = 0;

        // Read sample value based on the format
        switch (m_format.sampleFormat()) {
            case QAudioFormat::Int16: {
                qint16 sample = *reinterpret_cast<const qint16*>(m_visualizationData.constData() + i * bytesPerSample);
                sampleValue = static_cast<int>(sample);
                break;
            }
            case QAudioFormat::UInt8: {
                quint8 sample = *reinterpret_cast<const quint8*>(m_visualizationData.constData() + i * bytesPerSample);
                sampleValue = static_cast<int>(sample) - 128; // Center 8-bit sample
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

        // Normalize sample value
        int normalizationFactor = (1 << (8 * bytesPerSample - 1));

        if (normalizationFactor == 0) {
            continue; // Skip invalid normalization factor
        }

        // Calculate height of the bar based on normalized value
        int barHeight = (sampleValue * widgetHeight / normalizationFactor) / 2;

        // Draw a filled rectangle representing the sample
        int x = (i / step) * barWidth;
        int y = (widgetHeight / 2) - barHeight;
        painter.drawRect(x, y, barWidth, barHeight * 2); // Draw rectangle symmetrically
    }
}



void AudioVisualizerWidget::updatePainter() {
    // Trigger a repaint to update the visualization
    update();
}