#include "previewvideowidget.h"

#include <QPainter>

PreviewVideoWidget::PreviewVideoWidget(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
}

void PreviewVideoWidget::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void PreviewVideoWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_image.isNull())
        return;

    const QSize scaled = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - scaled.width()) / 2,
                               (height() - scaled.height()) / 2),
                        scaled);
    painter.drawImage(target, m_image);
}
