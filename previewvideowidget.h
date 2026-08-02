#ifndef PREVIEWVIDEOWIDGET_H
#define PREVIEWVIDEOWIDGET_H

#include <QWidget>
#include <QImage>

// Paints QImage frames handed to it directly, letterboxed to preserve aspect
// ratio — used instead of QVideoWidget so video-effect filtering can happen
// in between frame decode and display (see PreviewDialog::onVideoFrame).
class PreviewVideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewVideoWidget(QWidget *parent = nullptr);

    void setImage(const QImage &image);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
};

#endif // PREVIEWVIDEOWIDGET_H
