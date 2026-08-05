#pragma once
#ifdef WAKKAQT_FFMPEG_NATIVE

#include <QString>
#include <QByteArray>
#include <QImage>
#include <QScopedPointer>
#include <functional>
#include <atomic>
#include <vector>

namespace FFmpegNative {

/// Returns duration of a media file in fractional seconds, or 0.0 on error.
double getDuration(const QString &filePath);

/// Returns true when the file contains at least one valid video stream.
bool hasVideoStream(const QString &filePath);

/// Decode media file to interleaved float32 stereo PCM at 44100 Hz.
/// Returns an empty vector on error, or if cancelled becomes true mid-decode.
std::vector<float> decodeToFloatStereo(const QString &filePath,
                                       const std::atomic<bool> *cancelled = nullptr);

/// Write interleaved float32 stereo at 44100 Hz to a WAV file (pcm_f32le).
/// Returns false (and leaves no output file) if cancelled becomes true mid-write.
bool writeFloatWav(const std::vector<float> &pcm, const QString &outPath,
                   const std::atomic<bool> *cancelled = nullptr);

/// Transcode audio to another format (output format determined by file extension).
/// progressCb (optional) is called with 0–100 values on the calling thread.
/// Returns false (and leaves no output file) if cancelled becomes true mid-decode.
bool transcodeAudio(const QString &input, const QString &output,
                    std::function<void(int)> progressCb = {},
                    const std::atomic<bool> *cancelled = nullptr);

/// Copy video stream from videoSrc and replace its audio track with the
/// contents of audioSrc. Output format determined by the output file extension.
/// progressCb (optional) is called with 0–100 values on the calling thread.
/// Returns false (and leaves no output file) if cancelled becomes true mid-decode
/// or mid-copy.
bool muxVideoWithAudio(const QString &videoSrc, const QString &audioSrc,
                       const QString &output,
                       std::function<void(int)> progressCb = {},
                       const std::atomic<bool> *cancelled = nullptr);

/// Extracts the audio track from `input`, resamples to 44100 Hz / stereo or
/// mono / Int16, optionally trims `offsetMs` from the start, and writes a
/// PCM WAV to `output`.
/// `filterStr` hints at the desired channel layout (e.g. "mono").
bool extractAudio(const QString &input, const QString &output,
                  qint64 offsetMs = 0,
                  const QString &filterStr = {},
                  const std::atomic<bool> *cancelled = nullptr);

/// Applies a libavfilter audio chain (e.g. "deesser,speechnorm,...") to
/// interleaved Int16 PCM at the given sample rate/channel count, returning
/// filtered PCM in the same layout. Falls back to returning `pcmS16`
/// unchanged if the filter graph fails to build.
QByteArray applyFilterChainS16(const QByteArray &pcmS16, int sampleRate, int channels,
                               const QString &filterChain);

/// Live, stateful video-effect filter (frei0r/curves/etc. via libavfilter),
/// meant for real-time preview. The underlying filter graph is rebuilt only
/// when the chain or frame size actually changes — rebuilding per frame is
/// too slow for real-time playback, especially for frei0r-backed effects
/// that reload and reinitialize the plugin from scratch on every build.
class VideoEffectProcessor {
public:
    VideoEffectProcessor();
    ~VideoEffectProcessor();

    /// Filters one video frame (converted internally to 32-bit BGRA) through
    /// `filterChain` — pass an empty chain for passthrough (returns `frame`
    /// unchanged, no graph work at all). Rebuilds the internal graph
    /// automatically when the chain or frame size changes since the last
    /// call. If the chain can't be built (e.g. a frei0r plugin isn't
    /// installed on this machine), warns once and passes frames through
    /// unmodified until reconfigured with a working chain.
    QImage process(const QImage &frame, const QString &filterChain);

    /// Quick availability probe: tries to build (and immediately tear down) a
    /// throwaway graph for `filterChain`. Used to decide whether an effect
    /// should be offered in the UI at all on this machine.
    static bool isChainAvailable(const QString &filterChain);

private:
    struct Impl;
    QScopedPointer<Impl> d;
};

/// Full render: vocal audio + webcam video + playback media → final mix.
/// progressCb is invoked with 0.0–1.0 progress values on the calling thread.
bool renderVideo(const QString &audioPath,          ///< enhanced+mastered vocal audio (WAV)
                 const QString &webcamPath,         ///< webcam recording
                 const QString &playbackPath,       ///< original karaoke playback
                 const QString &outputPath,         ///< render destination
                 double         vocalVolume,        ///< scalar gain (1.0 = unity)
                 qint64         audioOffsetMs,      ///< vocal timing adjustment (ms)
                 qint64         videoOffsetMs,      ///< webcam timing adjustment (ms)
                 const QString &resolution,         ///< e.g. "1920x1080"
                 const QString &rawVocalPath = {},  ///< raw vocal for pitch overlay (optional)
                 const std::atomic<bool> *cancelled = nullptr, ///< set true to abort
                 std::function<void(double)> progressCb = {},
                 const QString &videoEffectChain = {}); ///< libavfilter chain applied to webcam frames (empty = none)

} // namespace FFmpegNative

#endif // WAKKAQT_FFMPEG_NATIVE
