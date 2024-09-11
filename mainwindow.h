#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sndwidget.h"
#include <QScopedPointer>
#include <QMainWindow>
#include <QMessageBox>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QGraphicsVideoItem>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMediaCaptureSession>
#include <QCamera>
#include <QVideoWidget>
#include <QAudioInput>
#include <QAudioSink>
#include <QBuffer>
#include <QCloseEvent>
#include <QLineEdit>
#include <QTextEdit>
#include <QProgressBar>
#include <QFile>
#include <QFileInfo>


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void handleRecorderError(QMediaRecorder::Error error);
    
private:

    bool isRecording;

    qint64 playbackEventTime = 0;
    qint64 recordingEventTime = 0;
    qint64 offset;

    QVideoWidget *videoWidget;
    QVideoWidget *previewWidget;
    QGraphicsVideoItem *previewItem;

    QAudioDevice selectedDevice;
    QBuffer *audioBuffer;

    QProgressBar *progressBar;
    int totalDuration;

    QScopedPointer<QTimer> recordingCheckTimer; 
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

    // yt-dlp
    QLineEdit *urlInput;
    QPushButton *fetchButton;
    QLabel *downloadStatusLabel;
    
    QTextEdit *logTextEdit; // logs
    
    QFile *webcamOutputFile;
    QString currentVideoFile; 
    QString webcamRecorded; 
    QString outputFilePath; 

    SndWidget *soundLevelWidget;

    void chooseVideo();
    void checkRecordingStart();
    void startRecording();
    void stopRecording();
    void handleRecordingError();

    void fetchVideo();

    QString millisecondsToSecondsString(qint64 milliseconds);
    void updateProgress(const QString& output, QProgressBar* progressBar, int totalDuration);
    int getMediaDuration(const QString &filePath);
    void mixAndRender(const QString &videoFile, const QString &webcamFile, const QString &outputFile, double vocalVolume);
    void renderAgain();

    void resetAudioComponents(bool isStarting);
    void configureMediaComponents();
    void chooseInputDevice();
    void updateDeviceLabel(const QAudioDevice &device);
    void disconnectAllSignals();
    void closeEvent(QCloseEvent *event) override;
    
};

#endif // MAINWINDOW_H
