#ifndef VIDEODISPLAYWIDGET_H
#define VIDEODISPLAYWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>
#include <QMutex>
#include <QTimer>

class VideoDisplayWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoDisplayWidget(QWidget *parent = nullptr)
        : QWidget(parent), m_sharedImage(nullptr), m_mutex(nullptr) {
        // Set up a timer to refresh at a steady frame rate (~30 FPS)
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &VideoDisplayWidget::updateFrame);
        m_timer->start(33); // ~30 FPS
    }

    void setSharedImage(QImage *image, QMutex *mutex) {
        m_sharedImage = image;
        m_mutex = mutex;
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        }
    }

    void paintEvent(QPaintEvent *event) override {
        if (m_sharedImage && m_mutex) {
            QPainter painter(this);
            QMutexLocker locker(m_mutex); // Ensure thread-safe access to the shared image
            
            if (!m_sharedImage->isNull()) {
                // Scale the image to fit the widget’s size and draw it
                painter.drawImage(this->rect(), *m_sharedImage);
            }
        }
    }

private slots:
    void updateFrame() {
        // Trigger a repaint of the widget
        update();
    }

private:
    QImage *m_sharedImage; // Shared image pointer
    QMutex *m_mutex;       // Mutex for thread-safe image access
    QTimer *m_timer;       // Timer for refreshing the widget
};

#endif
