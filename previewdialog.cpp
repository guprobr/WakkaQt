#include "previewdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QTimer>

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent),
      volumeSlider(new QSlider(Qt::Horizontal)),
      volumeValueLabel(new QLabel("Volume: 100%")), // Initial label for 100%
      durationLabel(new QLabel("Duration: 00:00:00")), // Label for duration
      playButton(new QPushButton("Play")),
      stopButton(new QPushButton("Stop")),
      renderButton(new QPushButton("Render")),
      currentVolume(100), // Initialize currentVolume to 100%
      timer(new QTimer(this)) // Initialize the timer
{
    setupUi();

    // Connect the timer's timeout signal to the update duration slot
    connect(timer, &QTimer::timeout, this, &PreviewDialog::updateDuration);
}

PreviewDialog::~PreviewDialog() {
    // Clean up media players and audio outputs
    qDeleteAll(mediaPlayers);
    qDeleteAll(audioOutputs);
    mediaPlayers.clear();
    audioOutputs.clear();
}

void PreviewDialog::setupUi() {
    // Set window title
    this->setWindowTitle("Vocal Preview & Volume Adjustment");

    volumeSlider->setRange(0, 1000); // Volume range from 0% to 1000%
    volumeSlider->setValue(100); // Default volume to 100%

    // Create explanation label
    QLabel *explanationLabel = new QLabel("This is a very low quality vocal preview just to adjust volume before rendering");

    // Set up layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Add explanation label to the top of the layout
    mainLayout->addWidget(explanationLabel);

    // Create a layout for the volume control
    QHBoxLayout *volumeLayout = new QHBoxLayout();
    volumeLayout->addWidget(volumeSlider);
    volumeLayout->addWidget(volumeValueLabel);
    
    // Create a layout for the control buttons
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    controlsLayout->addWidget(playButton);
    controlsLayout->addWidget(stopButton);
    controlsLayout->addWidget(renderButton);
    
    // Create a layout for the duration label
    QHBoxLayout *durationLayout = new QHBoxLayout();
    durationLayout->addWidget(durationLabel);

    // Add all layouts to the main layout
    mainLayout->addLayout(volumeLayout);
    mainLayout->addLayout(controlsLayout);
    mainLayout->addLayout(durationLayout);

    setLayout(mainLayout);

    // Connect signals and slots
    connect(volumeSlider, &QSlider::valueChanged, this, &PreviewDialog::updateVolume); // Connect volume slider change
    connect(playButton, &QPushButton::clicked, this, &PreviewDialog::playAudio);
    connect(stopButton, &QPushButton::clicked, this, &PreviewDialog::stopAudio);
    connect(renderButton, &QPushButton::clicked, this, &PreviewDialog::accept); // Call accept when render button is clicked
}

void PreviewDialog::setAudioFile(const QString &filePath) {
    audioFilePath = filePath; // Store the audio file path
}

double PreviewDialog::getVolume() const {
    return currentVolume / 100; 
}

void PreviewDialog::accept() {
    currentVolume = volumeSlider->value(); // Update the current volume before accepting
    QDialog::accept(); // Call base class accept
}

void PreviewDialog::playAudio() {
    // Clear previous media players and audio outputs
    qDeleteAll(mediaPlayers);
    qDeleteAll(audioOutputs);
    mediaPlayers.clear();
    audioOutputs.clear();

    // Check if audio file path is set
    if (audioFilePath.isEmpty()) {
        QMessageBox::warning(this, "Warning", "No audio file selected.");
        return;
    }

    int numberOfPlayers = 5;     

    for (int i = 0; i < numberOfPlayers; ++i) {
        QMediaPlayer *player = new QMediaPlayer(this);
        QAudioOutput *audioOutput = new QAudioOutput(this);

        player->setSource(QUrl::fromLocalFile(audioFilePath));
        audioOutput->setVolume(1.0); // Set volume to 100% for each audio output
        player->setAudioOutput(audioOutput); // Link the media player to the audio output
        
        // Start playback
        player->play();

        // Store the player and audio output for cleanup later
        mediaPlayers.append(player);
        audioOutputs.append(audioOutput);
    }
    for (QMediaPlayer *player : mediaPlayers) 
       player->setPosition(0);
        

    // Start the timer to update duration and adj vol to current slider value
    updateVolume(currentVolume);
    timer->start(1000); // Update every second
}

void PreviewDialog::stopAudio() {
    // Stop all media players
    for (QMediaPlayer *player : mediaPlayers)
        player->stop();

    // Stop the timer
    timer->stop();
    durationLabel->setText("Duration: 00:00:00"); // Reset duration label when stopped
}

void PreviewDialog::updateVolume(int value) {
    volumeValueLabel->setText(QString("Volume: %1%").arg(value)); // Update the label to show the current volume
    currentVolume = value; // Update current volume

    // Update volume for all audio outputs
    double scaledVolume = value / 500.0; 
    for (QAudioOutput *audioOutput : audioOutputs) {
        audioOutput->setVolume(scaledVolume); // Set the volume for each audio output
    }
}

void PreviewDialog::updateDuration() {
    // Update elapsed duration and total duration label for the first media player only
    if (!mediaPlayers.isEmpty()) {
        QMediaPlayer *player = mediaPlayers.first(); // Get the first player

        // Get elapsed position in milliseconds
        qint64 elapsedDuration = player->position(); 
        // Get total duration in milliseconds
        qint64 totalDuration = player->duration(); 

        // Convert elapsed milliseconds to hh:mm:ss format
        int elapsedSeconds = (elapsedDuration / 1000) % 60;
        int elapsedMinutes = (elapsedDuration / (1000 * 60)) % 60;
        int elapsedHours = (elapsedDuration / (1000 * 60 * 60)) % 24;

        // Convert total milliseconds to hh:mm:ss format
        int totalSeconds = (totalDuration / 1000) % 60;
        int totalMinutes = (totalDuration / (1000 * 60)) % 60;
        int totalHours = (totalDuration / (1000 * 60 * 60)) % 24;

        // Update duration label to show elapsed and total duration
        durationLabel->setText(QString("Elapsed: %1:%2:%3 / Total: %4:%5:%6")
            .arg(elapsedHours, 2, 10, QChar('0'))
            .arg(elapsedMinutes, 2, 10, QChar('0'))
            .arg(elapsedSeconds, 2, 10, QChar('0'))
            .arg(totalHours, 2, 10, QChar('0'))
            .arg(totalMinutes, 2, 10, QChar('0'))
            .arg(totalSeconds, 2, 10, QChar('0')));
    }
}
