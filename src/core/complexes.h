#ifndef COMPLEXES_H
#define COMPLEXES_H

#include <QFile>
#include <QAudioFormat>
#include <QFileDialog>
#include <QString>
#include <QStringList>
#include <QUrlQuery>
#include <QStringView>
#include <QVector>
#include <functional>


extern const QString _audioMasterization;
extern const QString _filterEcho;
extern const QString _audioEnhance;

// A single user-facing knob for a video effect (e.g. "Speed", "Intensity").
// Sliders in the UI are built directly from min/max/defaultValue; decimals
// controls how the current value is displayed (and how finely the slider
// can be adjusted).
struct VideoEffectParam {
    QString id;
    QString label;
    double minValue;
    double maxValue;
    double defaultValue;
    int decimals;
};

// Video effects offered on PreviewDialog's "Effects" tab. Several effects
// can be enabled simultaneously — buildFilterChain() renders ONE effect's
// current parameter values (same order as `params`) into a libavfilter
// graph fragment (same syntax as ffmpeg's -vf); PreviewDialog joins every
// enabled effect's fragment with commas to build the full chain. Applied
// identically to the live preview and the final render, so what the user
// sees is what gets rendered.
struct VideoEffectPreset {
    QString id;
    QString label;
    std::function<QString(const QVector<double> &paramValues)> buildFilterChain;
    QVector<VideoEffectParam> params;
};
extern const QVector<VideoEffectPreset> videoEffectPresets;

// Returns true when the file is an audio-only format (mp3/wav/opus/flac).
// Use this instead of repeating the four-extension chain everywhere.
bool isAudioOnlyFile(const QString &path);

// Returns true when `filePath` actually contains a decodable video stream —
// existence/size alone doesn't catch a truncated/corrupt recording that
// still has some bytes but no real video track. Uses FFmpegNative when
// built with WAKKAQT_FFMPEG_NATIVE, otherwise shells out to ffprobe. Single
// shared implementation for the two call sites that both used to duplicate
// this exact ffprobe invocation (session-restore webcam validation, and the
// backing-track separator's video/audio-only save-format decision).
bool mediaHasVideoStream(const QString &filePath);

// Keeps a save-mode QFileDialog's default suffix in sync with whichever
// filter the user picks, so typing a bare filename (no extension) saves with
// the extension matching the currently selected filter. Single shared
// implementation for the output-path pickers that used to duplicate this
// exact filterSelected lambda (render-again and library-restore).
void syncDefaultSuffixToFilter(QFileDialog &dlg);

// The extension whitelist and QFileDialog filter string for the mix/render
// output-path pickers, keyed on whether the session has webcam footage. No
// camera means no video track to mux, so the choice is restricted to
// audio-only containers. Single shared implementation for the render-again
// and library-restore flows, which used to duplicate these exact literals.
QStringList allowedRenderExtensions(bool hasWebcam);
QString renderSaveFilter(bool hasWebcam);

extern QString webcamRecorded;
extern QString audioRecorded;
extern QString tunedRecorded;
extern QString extractedPlayback;
extern QString extractedTmpPlayback;

// Resets webcamRecorded/audioRecorded/extractedTmpPlayback to their default
// fixed /tmp paths, undoing any repoint done while consuming a session
// restore's per-session workspace (see SessionRepository::restoreSession()).
void resetRecordingTempPaths();

void writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize, const QByteArray &pcmData);
static bool isYouTubeHost(const QString& host);
bool isSingleYouTubeVideoUrl(const QUrl& url);

// A decoded WAV file's payload, kept explicitly separate from the RIFF
// container it came out of — samples never includes the header/chunk bytes.
// isValid() is false if parseWavPcm() couldn't make sense of the input.
struct PcmBuffer {
    QByteArray samples;
    QAudioFormat format;
    bool isValid() const { return !samples.isEmpty() && format.sampleRate() > 0; }
};

// Parses a RIFF/WAVE byte buffer by walking its actual chunk structure —
// not by assuming a fixed 44-byte header, which silently breaks on any file
// with extra chunks before "data" (e.g. a LIST/INFO chunk some encoders
// add, or an extended "fmt " chunk) and, worse, leaves the header's own
// bytes attached to what callers then treat as raw PCM samples. Returns an
// invalid (empty-samples) PcmBuffer if `wavBytes` isn't a well-formed
// PCM/IEEE-float WAV file.
PcmBuffer parseWavPcm(const QByteArray &wavBytes);


#endif