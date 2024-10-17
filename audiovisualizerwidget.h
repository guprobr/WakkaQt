#ifndef AUDIOVISUALIZERWIDGET_H
#define AUDIOVISUALIZERWIDGET_H

#include <QWidget>
#include <QFrame>
#include <QAudioFormat>
#include <QByteArray>
#include <QTimer>

class AudioVisualizerWidget : public QFrame
{
    Q_OBJECT
public:
    explicit AudioVisualizerWidget(QWidget *parent = nullptr);
    ~AudioVisualizerWidget();
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QByteArray m_visualizationData;
    QAudioFormat m_format;
    QBrush m_brush;
    QColor bgColor;

public slots:
    void updateVisualization(const QByteArray &audioData, const QAudioFormat &format);

private slots:
    void updatePainter();

};

#endif // AUDIOVISUALIZERWIDGET_H
