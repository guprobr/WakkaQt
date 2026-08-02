#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sndwidget.h"
#include "pitchmonitorwidget.h"
#include "audiorecorder.h"
#include "audiovizmediaplayer.h"
#include "audiovisualizerwidget.h"
#include "previewdialog.h"
#include "librarydialog.h"
#include "sessionmanager.h"

#include <QWidget>
#include <QFutureWatcher>
#include <atomic>
#include <memory>
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
#include <QApplication>
#include <QFontDatabase>

#include <QDebug>


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    QString Wakka_versione = "v2.7.7";
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void addVideoDisplayWidgetInDialog(); // Method to add a VideoDisplayWidget in a dialog
    void logUI(const QString &msg);

private slots:
    void onRecorderDurationChanged(qint64 currentDuration);
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPlayerPositionChanged(qint64 position);
    void handleRecorderError(QMediaRecorder::Error error);
    void onPreviewCheckboxToggled(bool checked);
    void onVizCheckboxToggled(bool enable);
    void onPlayPauseClicked();

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
    // False when no camera was selected/available (empty video-input list, or
    // the user picked audio only) — gates every camera/mediaRecorder codepath
    // so WakkaQt can record, preview, and render audio-only performances.
    // This reflects the CURRENTLY connected/selected device, which is not
    // necessarily the same as whether the session actually being rendered
    // has webcam material — see recordingHasWebcam.
    bool hasCamera = false;
    // Whether the session currently being finalized/rendered actually has a
    // webcam recording — set from hasCamera when a live recording finishes,
    // or from SessionManager::restoreSession()'s ground-truthed hasWebcam
    // when restoring an old session. renderAgain()/mixAndRender() must use
    // this, not hasCamera, so restoring an audio-only session while a camera
    // happens to be connected right now can't pull in stale/wrong webcam
    // material (or vice versa: a session that does have webcam footage
    // getting rendered audio-only just because no camera is attached today).
    bool recordingHasWebcam = false;

    QProgressBar *progressBar;
    int totalDuration;

    // Owns the background FFmpegNative::renderVideo() call in mixAndRender()
    // (native path only). Previously a bare QThreadPool::globalInstance()
    // ->start([=]{...}) task whose worker lambda captured `this` implicitly
    // and read MainWindow members (tunedRecorded, webcamRecorded, etc.)
    // directly on the background thread, and whose completion was marshalled
    // back via QMetaObject::invokeMethod(qApp, ...) — qApp as context gives
    // NO protection against `this` having been destroyed by the time that
    // queued call runs, unlike using `this` itself (which Qt purges pending
    // posted events for). A QFutureWatcher, connected with `this` as context
    // like previewdialog.cpp's enhanceWatcher/extractWatcher, restores that
    // guarantee; closeEvent() blocks on it (after requesting cancellation
    // via renderCancelled, the same token the "Abort Render" button already
    // uses) so no background thread can be running when this window closes.
    QFutureWatcher<bool> *renderWatcher = nullptr;
    std::shared_ptr<std::atomic<bool>> renderCancelled;

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
    QAction *libraryAction;

    QPushButton *singButton;
    QPushButton *abortButton;
    QPushButton *chooseVideoButton;
    QPushButton *chooseInputButton;
    QPushButton *chooseLastButton;
    QPushButton *libraryButton;
    QPushButton *backingTrackButton;

    // Transport controls (play/pause, stop/rewind, seek ±10 s)
    QPushButton *playPauseButton;
    QPushButton *stopButton;
    QPushButton *seekBackButton;
    QPushButton *seekFwdButton;
    QWidget     *transportWidget;
    QLabel* placeholderLabel;
    QLabel *recordingIndicator; 
    QLabel *deviceLabel;
    QLabel *banner;

    QCheckBox *previewCheckbox;
    QCheckBox *vizCheckbox;

    // yt-dlp
    QLineEdit   *urlInput;
    QPushButton *fetchButton;
    QPushButton *browseYoutubeButton;
    QLabel      *downloadStatusLabel;
    
    QTextEdit *logTextEdit;
    
    QString currentPlayback;
    QString currentVideoFile;
    QString currentVideoName;
    QString outputFilePath;

    QString downloadedVideoPath; 

    SndWidget           *soundLevelWidget;
    PitchMonitorWidget  *pitchMonitor;

    void playVideo(const QString& playbackVideoPath);
    void chooseVideo();
    void chooseLast();
    void updatePlaybackDuration();
    void updateVideoVisibility();

    void startRecording();
    void stopRecording();
    void abortRecording();
    void waitForFileFinalization(const QString &filePath, std::function<void()> callback);
    void pollFileFinalization(const QString &filePath, int attempts, std::function<void()> callback);
    void handleRecordingError();

    void fetchVideo();
    void openYoutubeBrowser();

    QString millisecondsToSecondsString(qint64 milliseconds);
    double getMediaDuration(const QString &filePath);
    void addProgressSong(QGraphicsScene *scene, qint64 duration);
    void updateProgress(const QString& output, QProgressBar* progressBar, int totalDuration);
    
    void mixAndRender(double vocalVolume, qint64 manualOffset, const QString &videoEffectChain = {});
    void renderAgain();

    void openLibrary();
    void saveCurrentSession();
    void restoreAndRender(const QString &sessionId);
    void generateBackingTrack();

    void resetMediaComponents(bool isStarting);
    void configureMediaComponents();
    void chooseInputDevice();
    void updateDeviceLabel(const QString &deviceLabelText);
    void enable_playback(bool flag);

    void disconnectAllSignals();
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent *event) override;

    void toggleLogVisibility();
    void setBanner(const QString &msg);
    bool eventFilter(QObject *object, QEvent *event) override;

    void setDefaultFontForClass(const char* className, qreal pt);
    qreal progressBarDisplayWidth() const;
    
};

#endif // MAINWINDOW_H
