#include "previewdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QTime>

#include <gst/gst.h>

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent), pipeline(nullptr), source(nullptr), decode(nullptr),
      convert(nullptr), volume(nullptr), sink(nullptr), seeker(nullptr), timer(new QTimer(this)),
      seekDelayTimer(new QTimer(this)), seekAllowed(true) 
{
    setWindowTitle("Vocal-only Preview & Volume Adjustment");

    // Volume slider and labels
    volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 1000); // Volume range from 0 to 1000%
    volumeSlider->setValue(100); // Default volume 100%
    volumeValueLabel = new QLabel("Volume: 100%", this); // Label for the slider value
    sliderLabel = new QLabel("Adjust the volume of the vocals before rendering:", this);

    // Buttons
    okButton = new QPushButton("Render", this);
    playButton = new QPushButton("Play ►", this);
    stopButton = new QPushButton("Stop ■", this);
    seekForwardButton = new QPushButton("Seek >>", this);
    seekBackwardButton = new QPushButton("Seek <<", this);
    
    // Label for elapsed time
    elapsedTimeLabel = new QLabel("00:00", this);

    // Layouts
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    QHBoxLayout *seekLayout = new QHBoxLayout();

    controlsLayout->addWidget(playButton);
    controlsLayout->addWidget(stopButton);

    seekLayout->addWidget(seekBackwardButton);
    seekLayout->addWidget(seekForwardButton);

    mainLayout->addWidget(sliderLabel);
    mainLayout->addWidget(volumeValueLabel);
    mainLayout->addWidget(volumeSlider);
    mainLayout->addLayout(controlsLayout);
    mainLayout->addLayout(seekLayout);
    mainLayout->addWidget(elapsedTimeLabel);
    mainLayout->addWidget(okButton);

    setLayout(mainLayout);

    // Connect signals to slots
    connect(okButton, &QPushButton::clicked, this, &PreviewDialog::onOkButtonClicked);
    connect(volumeSlider, &QSlider::valueChanged, this, &PreviewDialog::onVolumeSliderChanged);
    connect(playButton, &QPushButton::clicked, this, &PreviewDialog::onPlayButtonClicked);
    connect(stopButton, &QPushButton::clicked, this, &PreviewDialog::onStopButtonClicked);
    connect(seekForwardButton, &QPushButton::clicked, this, &PreviewDialog::onSeekForwardButtonClicked);
    connect(seekBackwardButton, &QPushButton::clicked, this, &PreviewDialog::onSeekBackwardButtonClicked);
    
    connect(timer, &QTimer::timeout, this, &PreviewDialog::updateElapsedTime);

    // Set the seek delay timeout (to prevent flooding with clicks)
    seekDelayTimer->setInterval(500); // 500ms delay between seek operations
    seekDelayTimer->setSingleShot(true);
    connect(seekDelayTimer, &QTimer::timeout, this, &PreviewDialog::enableSeekButtons);

    // Initialize GStreamer pipeline
    initializeGStreamer();
}

void PreviewDialog::initializeGStreamer() {
    gst_init(nullptr, nullptr);

    // Create GStreamer elements
    pipeline = gst_pipeline_new("audio-preview");
    source = gst_element_factory_make("filesrc", "source");
    decode = gst_element_factory_make("decodebin", "decoder");
    convert = gst_element_factory_make("audioconvert", "convert");
    volume = gst_element_factory_make("volume", "volume");
    sink = gst_element_factory_make("autoaudiosink", "audio-output");

    // Check if all elements were successfully created
    if (!pipeline || !source || !decode || !convert || !volume || !sink) {
        qWarning("Failed to create GStreamer elements");
        return;
    }

    // Add elements to the pipeline and link source to decodebin
    gst_bin_add_many(GST_BIN(pipeline), source, decode, convert, volume, sink, nullptr);
    gst_element_link(source, decode);

    // Connect the pad-added signal to dynamically link decodebin to audioconvert
    g_signal_connect(decode, "pad-added", G_CALLBACK(on_pad_added), this);
}

void PreviewDialog::on_pad_added(GstElement *src, GstPad *pad, gpointer data) {
    PreviewDialog *dialog = static_cast<PreviewDialog*>(data);
    GstPad *sink_pad = gst_element_get_static_pad(dialog->convert, "sink");

    if (!sink_pad) {
        qWarning("Sink pad for audioconvert not found");
        return;
    }

    if (gst_pad_link(pad, sink_pad) != GST_PAD_LINK_OK) {
        qWarning("Failed to link decodebin pad to audioconvert sink pad");
    } else {
        qDebug() << "Successfully linked decodebin to audioconvert";

        // Now link the remaining elements: convert -> volume -> sink
        gst_element_link_many(dialog->convert, dialog->volume, dialog->sink, nullptr);
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
    if (!pipelineIsValid()) return;

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    timer->start(1000); // Update elapsed time every second
}

void PreviewDialog::onStopButtonClicked() {
    if (!pipelineIsValid()) return;

    gst_element_set_state(pipeline, GST_STATE_PAUSED);
    rewindToBeginning();
}

void PreviewDialog::onSeekForwardButtonClicked() {
    if (!pipelineIsValid() || !seekAllowed) return; // Prevent seeking if not allowed
    
    seek(1 * GST_SECOND); // Seek forward 1 second

    // Disable seeking and start the delay timer
    seekAllowed = false;
    seekForwardButton->setEnabled(false);
    seekBackwardButton->setEnabled(false);
    seekDelayTimer->start();
}

void PreviewDialog::onSeekBackwardButtonClicked() {
    if (!pipelineIsValid() || !seekAllowed) return; // Prevent seeking if not allowed
    
    seek(-2 * GST_SECOND); // Seek backward 2 seconds

    // Disable seeking and start the delay timer
    seekAllowed = false;
    seekForwardButton->setEnabled(false);
    seekBackwardButton->setEnabled(false);
    seekDelayTimer->start();
}

void PreviewDialog::enableSeekButtons() {
    seekAllowed = true;
    seekForwardButton->setEnabled(true);
    seekBackwardButton->setEnabled(true);
}

bool PreviewDialog::pipelineIsValid() {
    if (!pipeline) {
        QMessageBox::critical(this, "gStreamer Error", "Pipeline is not initialized. No sound available on preview, but you can still change the volume and press Render anyway.");
        return false;
    }

    GstState state;
    GstStateChangeReturn ret = gst_element_get_state(pipeline, &state, nullptr, GST_CLOCK_TIME_NONE);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        QMessageBox::critical(this, "gStreamer Error", "Failed to retrieve pipeline state. No sound available on preview, but you can still change the volume and press Render anyway.");
        return false;
    }

    if (state == GST_STATE_NULL) {
        QMessageBox::warning(this, "Warning", "Pipeline is in the NULL state.");
        return false;
    }

    return true;
}


void PreviewDialog::seek(gint64 timeOffset) {
    gint64 position, duration;

    // Query the current position
    if (!gst_element_query_position(pipeline, GST_FORMAT_TIME, &position)) {
        qWarning("Failed to query position");
        return;
    }

    // Query the total duration of the media
    if (!gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duration)) {
        qWarning("Failed to query duration");
        return;
    }

    gint64 newPosition = position + timeOffset;

    // Ensure the new position is within the valid range
    if ( newPosition < 0 || newPosition >= duration ) {
        newPosition = 0;
    }

    // Perform the seek operation
    if (!gst_element_seek(pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, newPosition, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        qWarning("Failed to seek");
    }
}


void PreviewDialog::rewindToBeginning() {
    if (!gst_element_seek(pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        qWarning("Failed to rewind to the beginning");
    }
    
    timer->stop();
    elapsedTimeLabel->setText("00:00");
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
    return volumeSlider->value() / 100.0; // Convert slider value to a decimal range (0.0 to 10.0)
}

PreviewDialog::~PreviewDialog() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}
