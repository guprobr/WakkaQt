// previewwebcam.cpp
#include "previewwebcam.h"
#include <QVBoxLayout>

PreviewWebcam::PreviewWebcam(QWidget *parent) 
    : QDialog(parent), videoWidget(new QVideoWidget(this)) {
    setWindowTitle("Webcam Preview");
    setMinimumSize(320, 200);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(videoWidget);
    setLayout(layout);
}
