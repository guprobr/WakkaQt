#include "previewdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QTime>
#include <gst/gst.h>

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent), pipeline(nullptr), source(nullptr), decode(nullptr),
      convert(nullptr), volume(nullptr), sink(nullptr), seek(nullptr), timer(new QTimer(this))
{
    setWindowTitle("Vocal-only Preview & Volume Adjustment");

    volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 500); // Volume range from 0 to 500%
    volumeSlider->setValue(100); // Default volume 100%

    okButton = new QPushButton("Render", this);
    playButton = new QPushButton("Play ► ", this);
    stopButton = new QPushButton("Stop ■ ", this);
    sliderLabel = new QLabel("Adjust the volume of the vocals before rendering:", this);
    volumeValueLabel = new QLabel("Volume: 100%", this); // Label for the slider value
    elapsedTimeLabel = new QLabel("00:00", this); // Label for elapsed time

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *controlsLayout = new QHBoxLayout();

    controlsLayout->addWidget(playButton);
    controlsLayout->addWidget(stopButton);

    mainLayout->addWidget(sliderLabel);
    mainLayout->addWidget(volumeValueLabel);
    mainLayout->addWidget(volumeSlider);
    mainLayout->addLayout(controlsLayout);
    mainLayout->addWidget(elapsedTimeLabel);
    mainLayout->addWidget(okButton);

    setLayout(mainLayout);

    connect(okButton, &QPushButton::clicked, this, &PreviewDialog::onOkButtonClicked);
    connect(volumeSlider, &QSlider::valueChanged, this, &PreviewDialog::onVolumeSliderChanged);
    connect(playButton, &QPushButton::clicked, this, &PreviewDialog::onPlayButtonClicked);
    connect(stopButton, &QPushButton::clicked, this, &PreviewDialog::onStopButtonClicked);
    
    connect(timer, &QTimer::timeout, this, &PreviewDialog::updateElapsedTime);

    initializeGStreamer();
}

void PreviewDialog::initializeGStreamer() {
    gst_init(nullptr, nullptr);

    pipeline = gst_pipeline_new("audio-preview");
    source = gst_element_factory_make("filesrc", "source");
    decode = gst_element_factory_make("decodebin", "decoder");
    convert = gst_element_factory_make("audioconvert", "convert");
    volume = gst_element_factory_make("volume", "volume");
    sink = gst_element_factory_make("autoaudiosink", "audio-output");

    if (!pipeline || !source || !decode || !convert || !volume || !sink) {
        qWarning("Failed to create GStreamer elements");
        return;
    }

    gst_bin_add_many(GST_BIN(pipeline), source, decode, convert, volume, sink, nullptr);
    gst_element_link(source, decode);
    gst_element_link_many(convert, volume, sink, nullptr);

    g_signal_connect(decode, "pad-added", G_CALLBACK(on_pad_added), convert);

}


void PreviewDialog::on_pad_added(GstElement *src, GstPad *pad, gpointer data) {

    GstPad *sink_pad = gst_element_get_static_pad(static_cast<GstElement*>(data), "sink");
    if (!sink_pad) {
        qWarning("Sink pad for audioconvert not found");
        return;
    }

    if (gst_pad_link(pad, sink_pad) != GST_PAD_LINK_OK) {
        qWarning("Failed to link decodebin pad to audioconvert sink pad");
    } else {
        qDebug() << "Successfully linked decodebin to audioconvert";
    }
    gst_object_unref(sink_pad);

}

void PreviewDialog::setAudioFile(const QString &filePath) {
    if (source) {
        g_object_set(source, "location", filePath.toStdString().c_str(), nullptr);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
    }
}

void PreviewDialog::onOkButtonClicked() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }
    accept();
}

void PreviewDialog::onVolumeSliderChanged(int value) {
    if (volume) {
        g_object_set(volume, "volume", value / 100.0, nullptr);
    }
    volumeValueLabel->setText(QString("Volume: %1%").arg(value));
}

void PreviewDialog::onPlayButtonClicked() {
    if (pipeline) {
        onStopButtonClicked();

        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        timer->start(1000); // Update elapsed time every second
    }
}

void PreviewDialog::onStopButtonClicked() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_PAUSED);

        // Seek to the beginning of the media
        if (!gst_element_seek(pipeline, 1.0, GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                              GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
            qWarning("Failed to rewind to the beginning");
        }

        // Stop the timer used for updating the elapsed time
        timer->stop();

        elapsedTimeLabel->setText("00:00");
    }
}

void PreviewDialog::updateElapsedTime() {
    if (!pipeline) return;

    gint64 position;
    if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &position)) {
        QTime elapsedTime = QTime::fromMSecsSinceStartOfDay(position / 1000000); // Convert nanoseconds to milliseconds
        elapsedTimeLabel->setText(elapsedTime.toString("mm:ss"));
    } else {
        qWarning("Failed to query position");
    }
}

double PreviewDialog::getVolume() const {
    return volumeSlider->value() / 100.0; // Convert slider value to a decimal range (0.0 to 5.0)
}

PreviewDialog::~PreviewDialog() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}
