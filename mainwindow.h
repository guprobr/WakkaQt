#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sndwidget.h"
#include "videodisplaywidget.h"
#include "audiorecorder.h"

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

#include <QAudioSink>
#include <QBuffer>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    QString Wakka_versione = "v0.91a";
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void onVideoFrameReceived(const QVideoFrame &frame);
    void proxyVideoFrame(QVideoFrame &frame);
    void addVideoDisplayWidgetInDialog(); // Method to add a VideoDisplayWidget in a dialog

private slots:
    //void onAudioInputsChanged();
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void handleRecorderError(QMediaRecorder::Error error);
    void onPreviewCheckboxToggled(bool checked);
    
private:

    QList<VideoDisplayWidget*> previewWidgets;
    VideoDisplayWidget *mainPreviewWidget;
    QDialog *videoDialog; // Pointer to the currently webcam preview dialog

    QColor highlightColor;
    QString setRez = "1920x540"; // its a vstack, same width, half the height

    QGraphicsScene *scene;
    QGraphicsView *previewView;
    QGraphicsVideoItem *previewItem;
    QGraphicsRectItem *progressSong = nullptr;
    QGraphicsRectItem *progressSongFull = nullptr;
    QGraphicsTextItem *durationTextItem;
    QGraphicsPixmapItem *wakkaLogoItem;

    QTimer *playbackTimer;

    bool isRecording;
    qint64 startEventTime = 0;
    qint64 recordingEventTime = 0;
    qint64 offset = 0;

    QAudioDevice selectedDevice;
    QBuffer *audioBuffer;

    QProgressBar *progressBar;
    int totalDuration;

    QScopedPointer<QVideoWidget> videoWidget;
    QScopedPointer<QMediaPlayer> player;
    QScopedPointer<QAudioOutput> audioOutput;
    QScopedPointer<AudioRecorder> audioRecorder;
    QScopedPointer<QMediaFormat> format;
    QScopedPointer<QMediaRecorder> mediaRecorder;
    QScopedPointer<QMediaCaptureSession> mediaCaptureSession;
    QScopedPointer<QCamera> camera;
    QScopedPointer<QVideoSink> videoSink;
    
    QAction *loadPlaybackAction;
    QAction *chooseInputAction;

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
    QString audioRecorded; 
    QString outputFilePath; 

    SndWidget *soundLevelWidget;

    void chooseVideo();
    void updatePlaybackDuration();

    void startRecording();
    void stopRecording();
    void handleRecordingError();

    void fetchVideo();

    QString millisecondsToSecondsString(qint64 milliseconds);
    int getMediaDuration(const QString &filePath);
    void addProgressBarToScene(QGraphicsScene *scene, qint64 duration);
    void updateProgress(const QString& output, QProgressBar* progressBar, int totalDuration);
    
    void mixAndRender(double vocalVolume);
    void renderAgain();

    void resetMediaComponents(bool isStarting);
    void configureMediaComponents();
    void chooseInputDevice();
    void updateDeviceLabel(const QString &deviceLabelText);

    void disconnectAllSignals();
    void closeEvent(QCloseEvent *event) override;

    bool eventFilter(QObject *object, QEvent *event) override;
    
};

#endif // MAINWINDOW_H
