#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sndwidget.h"

#include <QMainWindow>
#include <QScopedPointer>
#include <QCloseEvent>

#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QTextEdit>
#include <QProgressBar>

#include <QVideoWidget>
#include <QGraphicsView>
#include <QGraphicsVideoItem>

#include <QMediaFormat>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QMediaCaptureSession>
#include <QCamera>

#include <QAudioInput>
#include <QAudioSink>
#include <QBuffer>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    QString Wakka_versione = "v0.3alpha";
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    //void onAudioInputsChanged();
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void handleRecorderError(QMediaRecorder::Error error);
    void onPreviewCheckboxToggled(bool checked);
    
private:

    QColor alternateColor;
    QString setRez = "1920x540"; // its a vstack, same width, half the height

    QGraphicsScene *scene;
    QGraphicsView *previewView;
    QGraphicsVideoItem *previewItem;
    QGraphicsRectItem *progressSong = nullptr;
    QGraphicsTextItem *durationTextItem;
    QGraphicsPixmapItem *wakkaLogoItem;

    bool isRecording;
    QTimer *playbackTimer;
    QScopedPointer<QTimer> recordingCheckTimer; 
    qint64 playbackEventTime = 0;
    qint64 recordingEventTime = 0;
    qint64 offset = 0;

    QAudioDevice selectedDevice;
    QBuffer *audioBuffer;

    QProgressBar *progressBar;
    int totalDuration;

    QVideoWidget *videoWidget;    
    QScopedPointer<QMediaPlayer> player;
    QScopedPointer<QAudioOutput> audioOutput;
    QScopedPointer<QAudioInput> audioInput;
    QScopedPointer<QMediaFormat> format;
    QScopedPointer<QMediaRecorder> mediaRecorder;
    QScopedPointer<QMediaCaptureSession> mediaCaptureSession;
    QScopedPointer<QCamera> camera;
    
    QPushButton *singButton;
    QPushButton *chooseVideoButton;
    QPushButton *chooseInputButton;
    QPushButton *renderAgainButton;
    QLabel* placeholderLabel;   // logo
    QLabel *recordingIndicator; 
    QLabel *deviceLabel;

    QCheckBox *previewCheckbox;

    // yt-dlp
    QLineEdit *urlInput;
    QPushButton *fetchButton;
    QLabel *downloadStatusLabel;
    
    QTextEdit *logTextEdit; // logs
    
    QString currentVideoFile;
    QString currentVideoName;
    QString webcamRecorded; 
    QString outputFilePath; 

    SndWidget *soundLevelWidget;

    void chooseVideo();
    void updatePlaybackDuration();
    void checkRecordingStart();
    void startRecording();
    void stopRecording();
    void handleRecordingError();

    void fetchVideo();

    QString millisecondsToSecondsString(qint64 milliseconds);
    int getMediaDuration(const QString &filePath);
    void addProgressBarToScene(QGraphicsScene *scene, qint64 duration);
    void updateProgress(const QString& output, QProgressBar* progressBar, int totalDuration);
    
    void mixAndRender(const QString &videoFile, const QString &webcamFile, const QString &outputFile, double vocalVolume, QString userRez);
    void renderAgain();

    void resetAudioComponents(bool isStarting);
    void configureMediaComponents();
    void chooseInputDevice();
    void updateDeviceLabel(const QAudioDevice &device);
    void disconnectAllSignals();
    void closeEvent(QCloseEvent *event) override;
    
};

#endif // MAINWINDOW_H
