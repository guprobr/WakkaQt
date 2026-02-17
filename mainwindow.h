#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sndwidget.h"
#include "audiorecorder.h"
#include "audiovizmediaplayer.h"
#include "audiovisualizerwidget.h"
#include "previewdialog.h"

#include <QWidget>
#include <QVideoWidget>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVBoxLayout>
#include <QTimer>
#include <QTime>
#include <QTextEdit>
#include <QStringList>
#include <QScopedPointer>
#include <QRegularExpression>
#include <QPushButton>
#include <QProgressBar>
#include <QProcess>
#include <QMessageBox>
#include <QMenuBar>
#include <QMenu>
#include <QMediaRecorder>
#include <QMediaPlayer>
#include <QMediaFormat>
#include <QMediaDevices>
#include <QMediaCaptureSession>
#include <QMainWindow>
#include <QListWidgetItem>
#include <QListWidget>
#include <QList>
#include <QLineEdit>
#include <QLabel>
#include <QInputDialog>
#include <QIcon>
#include <QHBoxLayout>
#include <QGraphicsView>
#include <QGraphicsVideoItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QFileInfo>
#include <QFileDialog>
#include <QFile>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QCamera>
#include <QBuffer>
#include <QAudioSink>
#include <QAudioOutput>
#include <QAudioInput>
#include <QAudioFormat>

#include <QDebug>


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    QString Wakka_versione = "v1.6";
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void addVideoDisplayWidgetInDialog(); // Method to add a VideoDisplayWidget in a dialog

private slots:
    void onRecorderDurationChanged(qint64 currentDuration);
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPlayerPositionChanged(qint64 position);
    void handleRecorderError(QMediaRecorder::Error error);
    void onPreviewCheckboxToggled(bool checked);
    void onVizCheckboxToggled(bool enable);

private:

    QGraphicsVideoItem *webcamPreviewItem;
    QGraphicsScene *webcamScene;
    QGraphicsView *webcamView;
    QHBoxLayout *webcamPreviewLayout;
    QDialog *webcamDialog;

    QColor highlightColor;
    QString setRez = "1920x1080"; // its a vstack, same width, half the height

    QGraphicsScene *progressScene;
    QGraphicsView *progressView;
    QGraphicsRectItem *progressSong = nullptr;
    QGraphicsRectItem *progressSongFull = nullptr;
    QGraphicsTextItem *durationTextItem;

    QTimer *playbackTimer;
    QElapsedTimer sysLatency;
    
    bool isRecording = false;
    bool isAborting = false;
    bool isPlayback = false;
    
    qint64 pos = 0;
    qint64 offset = 0;
    qint64 videoOffset = 0;
    qint64 audioOffset = 0;

    QAudioDevice selectedDevice;
    QCameraDevice selectedCameraDevice;

    QProgressBar *progressBar;
    int totalDuration;

    QVideoWidget *videoWidget;
    
    AudioVisualizerWidget *vizUpperLeft;
    AudioVisualizerWidget *vizUpperRight;

    QScopedPointer<QMediaPlayer> player;
    QScopedPointer<AudioVizMediaPlayer> vizPlayer;
    QScopedPointer<QAudioOutput> audioOutput;
    QScopedPointer<AudioRecorder> audioRecorder;
    QScopedPointer<QMediaFormat> format;
    QScopedPointer<QMediaRecorder> mediaRecorder;
    QScopedPointer<QMediaCaptureSession> mediaCaptureSession;
    QScopedPointer<QCamera> camera;

    QScopedPointer<PreviewDialog> previewDialog;
    
    QAction *loadPlaybackAction;
    QAction *chooseInputAction;
    QAction *singAction;

    QPushButton *singButton;
    QPushButton *abortButton;
    QPushButton *chooseVideoButton;
    QPushButton *chooseInputButton;
    QPushButton *renderAgainButton;
    QPushButton *chooseLastButton;
    QLabel* placeholderLabel;
    QLabel *recordingIndicator; 
    QLabel *deviceLabel;
    QLabel *banner;

    QCheckBox *previewCheckbox;
    QCheckBox *vizCheckbox;

    // yt-dlp
    QLineEdit *urlInput;
    QPushButton *fetchButton;
    QLabel *downloadStatusLabel;
    
    QTextEdit *logTextEdit;
    
    QString currentPlayback;
    QString currentVideoFile;
    QString currentVideoName;
    QString outputFilePath;

    QString downloadedVideoPath; 

    SndWidget *soundLevelWidget;

    void playVideo(const QString& playbackVideoPath);
    void chooseVideo();
    void chooseLast();
    void updatePlaybackDuration();

    void startRecording();
    void stopRecording();
    void abortRecording();
    void waitForFileFinalization(const QString &filePath, std::function<void()> callback);
    void handleRecordingError();

    void fetchVideo();

    QString millisecondsToSecondsString(qint64 milliseconds);
    double getMediaDuration(const QString &filePath);
    void addProgressSong(QGraphicsScene *scene, qint64 duration);
    void updateProgress(const QString& output, QProgressBar* progressBar, int totalDuration);
    
    void mixAndRender(double vocalVolume, qint64 manualOffset);
    void renderAgain();

    void resetMediaComponents(bool isStarting);
    void configureMediaComponents();
    void chooseInputDevice();
    void updateDeviceLabel(const QString &deviceLabelText);
    void enable_playback(bool flag);

    void disconnectAllSignals();
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent *event) override;

    void logUI(const QString &msg);
    void toggleLogVisibility();
    void setBanner(const QString &msg);
    bool eventFilter(QObject *object, QEvent *event) override;
    
};

#endif // MAINWINDOW_H
