#ifndef VIDEODISPLAYWIDGET_H
#define VIDEODISPLAYWIDGET_H

#include "complexes.h"

#include <QWidget>
#include <QPainter>
#include <QImage>
#include <QMouseEvent>

class VideoDisplayWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoDisplayWidget(QWidget *parent = nullptr) : QWidget(parent) {}

    void setImage() {
        update(); // Trigger a repaint
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
        if (!sharedWebcamPreviewImage.isNull()) {
            // Scale the image to fit the widget's size
            painter.drawImage(this->rect(), sharedWebcamPreviewImage);
        }
    }

 
};

#endif
