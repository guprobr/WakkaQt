#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sndwidget.h"
#include "videodisplaywidget.h"
#include "audiorecorder.h"
#include "audiovizmediaplayer.h"
#include "audiovisualizerwidget.h"

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
    QString Wakka_versione = "v0.98a";
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void onVideoFrameReceived(const QVideoFrame &frame); // See below
    void proxyVideoFrame(QVideoFrame &frame); // MEthod to mirror the webcam output from mediacapturesession
    void addVideoDisplayWidgetInDialog(); // Method to add a VideoDisplayWidget in a dialog

private slots:
    //void onAudioInputsChanged();
    void onRecorderDurationChanged(qint64 currentDuration);
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPlayerPosChanged(qint64 pos);
    void handleRecorderError(QMediaRecorder::Error error);
    void onPreviewCheckboxToggled(bool checked);
    
private:

    QList<VideoDisplayWidget*> previewWidgets;
    VideoDisplayWidget *webcamPreviewWidget;
    QDialog *webcamDialog; // Pointer to the currently webcam preview dialog

    QColor highlightColor;
    QString setRez = "1920x540"; // its a vstack, same width, half the height

    QGraphicsScene *scene;
    QGraphicsView *previewView;
    QGraphicsRectItem *progressSong = nullptr;
    QGraphicsRectItem *progressSongFull = nullptr;
    QGraphicsTextItem *durationTextItem;

    QTimer *playbackTimer;

    bool isRecording;
    bool isPlayback;
    qint64 startEventTime = 0;
    qint64 recordingEventTime = 0;
    qint64 offset = 0;
    qint64 audioOffset = 0;

    QAudioDevice selectedDevice;

    QProgressBar *progressBar;
    int totalDuration;

    QVideoWidget *videoWidget;
    AudioVisualizerWidget *vizWidget;

    QScopedPointer<QMediaPlayer> player;
    QScopedPointer<AudioVizMediaPlayer> vizPlayer;
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
    QLabel *banner;

    QCheckBox *previewCheckbox;

    // yt-dlp
    QLineEdit *urlInput;
    QPushButton *fetchButton;
    QLabel *downloadStatusLabel;
    
    QTextEdit *logTextEdit; // logs
    
    QString currentPlayback;
    QString currentVideoFile;
    QString currentVideoName;
    QString webcamRecorded;
    QString audioRecorded; 
    QString outputFilePath; 

    SndWidget *soundLevelWidget;

    void playVideo(const QString& playbackVideoPath);
    void chooseVideo();
    void updatePlaybackDuration();

    void startRecording();
    void stopRecording();
    void handleRecordingError();

    void fetchVideo();

    QString millisecondsToSecondsString(qint64 milliseconds);
    double getMediaDuration(const QString &filePath);
    void addProgressSong(QGraphicsScene *scene, qint64 duration);
    void updateProgress(const QString& output, QProgressBar* progressBar, int totalDuration);
    
    void mixAndRender(double vocalVolume);
    void renderAgain();

    void resetMediaComponents(bool isStarting);
    void configureMediaComponents();
    void chooseInputDevice();
    void updateDeviceLabel(const QString &deviceLabelText);

    void disconnectAllSignals();
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent *event) override;

    void setBanner(const QString &msg);
    bool eventFilter(QObject *object, QEvent *event) override;
    
};

#endif // MAINWINDOW_H
