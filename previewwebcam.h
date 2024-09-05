// previewwebcam.h
#ifndef PREVIEWWEBCAM_H
#define PREVIEWWEBCAM_H

#include <QDialog>
#include <QVideoWidget>
#include <QCamera>
#include <QMediaCaptureSession>

class PreviewWebcam : public QDialog {
    Q_OBJECT

public:
    explicit PreviewWebcam(QWidget *parent = nullptr);
    QVideoWidget *videoWidget;

};

#endif // PREVIEWWEBCAM_H
