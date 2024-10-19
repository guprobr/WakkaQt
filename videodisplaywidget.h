#ifndef VIDEODISPLAYWIDGET_H
#define VIDEODISPLAYWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QImage>
#include <QMouseEvent>

class VideoDisplayWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoDisplayWidget(QWidget *parent = nullptr) : QWidget(parent) {}

    void setImage(const QImage &image) {
        // clear the previous image to free memory
        if (!m_image.isNull()) {
            m_image = QImage(); 
        }
        
        if (m_image != image) { // Only update if the image is different
            m_image = image; 
            if (!repaintScheduled) {
                repaintScheduled = true;
                QTimer::singleShot(33, this, [this]() {
                    update(); 
                    repaintScheduled = false;
                });
            }
        }
    }

signals:
    void clicked();

protected:
    
    // the mousePressEvent for 'clicked' 
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked();  
        }
    }

    void paintEvent(QPaintEvent *event) override {
        QPainter painter(this);
        if (!m_image.isNull()) {
            // Scale the image to fit the widget's size
            painter.drawImage(this->rect(), m_image);
        }
    }

private:
    QImage m_image;

    bool repaintScheduled = false;
};

#endif
