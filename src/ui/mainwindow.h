#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sndwidget.h"
#include "pitchmonitorwidget.h"
#include "audiorecorder.h"
#include "audiovizmediaplayer.h"
#include "audiovisualizerwidget.h"
#include "previewdialog.h"
#include "librarydialog.h"
#include "sessionrepository.h"
#include "renderjob.h"
#include "vocalseparationjob.h"
#include "modeldownloadjob.h"

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
#include <QScreen>

#include <QDebug>


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    QString Wakka_versione = "v3.0.0";
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void addVideoDisplayWidgetInDialog(); // Method to add a VideoDisplayWidget in a dialog
    QSize scaledWebcamPreviewSize(const QSize &baseSize) const;
    bool validateRenderOutputPath(const QString &outputFilePath,
                                   const QStringList &allowedExtensions,
                                   bool recordingHasWebcamFlag);
    void promptRenderResolution();
    void showPreviewAndRender(std::function<void()> onCancelled);
    void reattachAudioOutputWorkaround(bool pauseFirst = false);
    void resumePlaybackAfterSeek(qint64 position, bool pauseFirst = false);
    void logUI(const QString &msg);

private slots:
    void onRecorderDurationChanged(qint64 currentDuration);
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPlayerPositionChanged(qint64 position);
    void handleRecorderError(QMediaRecorder::Error error);
    // Camera-level errors (as opposed to QMediaRecorder-level ones, handled
    // above) — e.g. the webcam being unplugged. Distinct signal in Qt6
    // Multimedia, previously never connected anywhere in this codebase.
    void handleCameraError(QCamera::Error error, const QString &errorString);
    // AudioRecorder::captureError — the mic disappearing mid-recording.
    void handleAudioCaptureError(const QString &message);
    // QMediaDevices hotplug notifications — only used to keep hasCamera/
    // deviceLabel honest while idle; startRecording() does its own
    // just-in-time validity check rather than relying on these having fired
    // in time.
    void onAudioInputsChanged();
    void onVideoInputsChanged();
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
    
    // Explicit lifecycle state — replaces the former isRecording/isAborting
    // bools. Idle/Recording/Aborting/Finalizing/Restoring/Rendering/Separating
    // are the only phases that can monopolize the app (recording, the
    // post-record-or-restore output/preview flow, an active render, or vocal
    // separation); hasCamera/recordingHasWebcam and isPlayback are deliberately
    // NOT part of this — they're orthogonal data/readiness concerns, not
    // lifecycle phases (see their own comments below).
    enum class State {
        Idle,         // nothing in progress — recording/library/separation may start
        Recording,    // camera/audio actively recording
        Aborting,     // transient: user aborted, stopRecording() takes the discard branch
        Finalizing,   // post-stop work + output-path/resolution/PreviewDialog flow (live record)
        Restoring,    // same flow, driven by a restored library session instead
        Rendering,    // RenderJob active — reached from Finalizing or Restoring
        Separating,   // generateBackingTrack() vocal separation in progress
    };
    State m_state = State::Idle;
    // Validates the transition against the legal-transitions table below,
    // logs it, and applies it. Returns false (no-op) and warns if the
    // transition isn't legal from the current state — this is what makes
    // e.g. starting vocal separation mid-recording, or restoring a session
    // mid-render, structurally impossible instead of accidentally avoided.
    bool trySetState(State next);

    bool isPlayback = false;
    
    qint64 pos = 0;
    qint64 offset = 0;
    qint64 videoOffset = 0;
    qint64 audioOffset = 0;
    // One-shot guard: on the first QMediaPlayer::positionChanged tick of a
    // recording session, AudioRecorder::armSync() fires and videoOffset is
    // set from AudioRecorder::preRollMs() — see onPlayerPositionChanged().
    bool audioSyncArmed = false;

    QAudioDevice selectedDevice;
    QCameraDevice selectedCameraDevice;
    // Long-lived instance whose signals reflect the OS-global device list —
    // the only place audioInputsChanged()/videoInputsChanged() (actual
    // hotplug notifications) are connected in this codebase. Constructed
    // once in MainWindow's constructor; never needs recreating.
    QMediaDevices *m_deviceMonitor = nullptr;
    // False when no camera was selected/available (empty video-input list, or
    // the user picked audio only) — gates every camera/mediaRecorder codepath
    // so WakkaQt can record, preview, and render audio-only performances.
    // This reflects the CURRENTLY connected/selected device, which is not
    // necessarily the same as whether the session actually being rendered
    // has webcam material — see recordingHasWebcam.
    bool hasCamera = false;
    // Whether the session currently being finalized/rendered actually has a
    // webcam recording — set from hasCamera when a live recording finishes,
    // or from SessionRepository::restoreSession()'s ground-truthed hasWebcam
    // when restoring an old session. renderAgain()/mixAndRender() must use
    // this, not hasCamera, so restoring an audio-only session while a camera
    // happens to be connected right now can't pull in stale/wrong webcam
    // material (or vice versa: a session that does have webcam footage
    // getting rendered audio-only just because no camera is attached today).
    bool recordingHasWebcam = false;

    // Set by restoreAndRender() to the per-restore workspace directory
    // returned by SessionRepository::restoreSession() (holding that
    // session's own copies of webcam/audio/playback) while webcamRecorded/
    // audioRecorded/extractedTmpPlayback are repointed into it. Empty when
    // no restore workspace is active. clearRestoreWorkspace() removes the
    // directory and repoints those globals back to their canonical /tmp
    // paths — called before a fresh live recording starts, before a new
    // restore begins, and on shutdown, so a workspace never leaks past the
    // one operation it was created for.
    QString m_activeRestoreWorkspaceDir;
    void clearRestoreWorkspace();

    QProgressBar *progressBar;
    int totalDuration;

    // Owns the actual FFmpeg render work (native QtConcurrent path or
    // QProcess+ffmpeg-CLI fallback) started by mixAndRender(). closeEvent()
    // cancels and waits on it (via cancel()/waitForFinished()) so no
    // background thread can be running when this window closes.
    RenderJob *m_renderJob = nullptr;

    // Owns the background work behind generateBackingTrack() (UVR-MDX-NET
    // separation, then muxing/transcoding into the final destination).
    // closeEvent() cancels and waits on it the same way it does m_renderJob.
    VocalSeparationJob *m_separationJob = nullptr;

    // Owns the one-time async download of the MDX-Net model, when
    // generateBackingTrack() finds it missing. Not covered by closeEvent()
    // beyond destruction: cancel()+waitForFinished() both run from its own
    // destructor (see ModelDownloadJob), so simply letting MainWindow's
    // QObject child-destruction take it down on close is already safe.
    ModelDownloadJob *m_modelDownloadJob = nullptr;

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

    void mixAndRender(double vocalVolume, qint64 manualOffset, const QString &videoEffectChain = {});
    void renderAgain();

    // Builds the progress dialog/bar UI, wires up m_renderJob's progress/
    // finished signals, and calls start(). Split out of mixAndRender() so
    // handleRenderFailure()'s "Try Again" can re-run the same Params without
    // re-doing the save-destination/resolution/preview-volume dialogs that
    // produced them — every input (source WAVs, webcam file, playback file)
    // is a durable file on disk, not a throwaway result, so a retry is just
    // "run RenderJob again with the same Params". Disconnects any previous
    // progress/finished listeners first since this can run more than once on
    // the same m_renderJob instance (a retry) — without disconnecting, each
    // retry would stack another set of listeners that all fire on the next run.
    void startRender(const RenderJob::Params &params);
    // Shown when RenderJob::finished reports failure (not cancellation).
    // Unlike VocalSeparationJob's export failure, there is no expensive
    // intermediate result to salvage here — a failed render just leaves
    // nothing at outputPath (RenderJob's atomic commit only ever touches the
    // real destination on success) — so the only meaningful recovery choice
    // is retrying the render itself.
    void handleRenderFailure(const RenderJob::Params &params, const QString &errorMessage);

    void openLibrary();
    void saveCurrentSession();
    void restoreAndRender(const QString &sessionId);
    void generateBackingTrack();
    // The actual separation+export flow — split out of generateBackingTrack()
    // so it can run either immediately (model already present) or from
    // ModelDownloadJob::finished's success branch (model just downloaded).
    void runVocalSeparation();

    // Everything needed to (re)run or recover a VocalSeparationJob export —
    // bundled so handleExportFailure()'s recovery choices (Try Again, Save
    // WAV, Open Folder) don't need a five-parameter signature repeated
    // across every method that can reach them.
    struct ExportRecoveryContext {
        QString tempOut;     // separated instrumental, inside the job's workspace
        QString inputFile;   // original source (for video muxing)
        QString savePath;    // the destination the user originally chose
        bool    saveAsVideo = false;
    };
    void startExport(const ExportRecoveryContext &ctx);
    // Shown when exportResult() fails; the workspace at ctx.tempOut is still
    // intact (VocalSeparationJob never discards it on failure) so every
    // choice here — retry, save the raw WAV instead, or just point the user
    // at the folder — has real data to act on. m_separationJob is only ever
    // destroyed after the user picks one of these (or Discard), since the
    // Separating state stays held until then.
    void handleExportFailure(const ExportRecoveryContext &ctx, const QString &errorMessage);
    // "Save WAV" recovery path: prompts a WAV-only save dialog for
    // ctx.tempOut. Falls back to re-showing handleExportFailure() (instead
    // of silently abandoning the workspace) if the user cancels the dialog.
    void promptSaveInstrumentalAsWav(const ExportRecoveryContext &ctx, const QString &priorError);
    // Shared by the normal WAV-output path and the "Save WAV" recovery
    // choice: streams tempOut into savePath via QSaveFile, discards the
    // workspace and returns to Idle on success. Returns false (leaving
    // state/workspace untouched) on failure so callers can decide what to
    // show/do next.
    bool finishWavSave(const QString &tempOut, const QString &savePath);

    void resetMediaComponents(bool isStarting);
    void configureMediaComponents();
    void chooseInputDevice();
    void updateDeviceLabel(const QString &deviceLabelText);
    void enable_playback(bool flag);
    // Wires audioRecorder's deviceLabelChanged + captureError — shared by
    // configureMediaComponents() and chooseInputDevice()'s picker dialog,
    // which both construct a fresh AudioRecorder but previously duplicated
    // (and, for captureError, would have had to duplicate) this wiring.
    void connectAudioRecorderSignals();
    // True if `device` (by id) is still present in the OS's current input
    // list — used both as startRecording()'s just-in-time guard and by the
    // hotplug slots above to decide whether anything actually changed for
    // the device this session cares about.
    static bool isAudioDeviceStillAvailable(const QAudioDevice &device);
    static bool isCameraDeviceStillAvailable(const QCameraDevice &device);

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
