#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <gst/gst.h>

class PreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreviewDialog(QWidget *parent = nullptr);
    ~PreviewDialog();

    void setAudioFile(const QString &filePath);
    double getVolume() const;

private slots:
    void onOkButtonClicked();
    void onVolumeSliderChanged(int value);
    void onPlayButtonClicked();
    void onStopButtonClicked();
    void onSeekForwardButtonClicked();
    void onSeekBackwardButtonClicked();
    void enableSeekButtons();
    void updateElapsedTime();

private:
    void initializeGStreamer();
    bool pipelineIsValid();
    void seek(gint64 timeOffset);
    void rewindToBeginning();
    static void on_pad_added(GstElement *src, GstPad *pad, gpointer data);

    QSlider *volumeSlider;

    QPushButton *okButton;
    QPushButton *playButton;
    QPushButton *stopButton;
    QPushButton *seekForwardButton;
    QPushButton *seekBackwardButton;
    
    QTimer *seekDelayTimer;
    bool seekAllowed;

    QLabel *sliderLabel;
    QLabel *volumeValueLabel;
    QLabel *elapsedTimeLabel;

    QTimer *timer;

    GstElement *pipeline;
    GstElement *source;
    GstElement *decode;
    GstElement *convert;
    GstElement *volume;
    GstElement *sink;
    GstElement *seeker;
};

#endif // PREVIEWDIALOG_H
