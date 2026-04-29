#pragma once
#ifdef WAKKAQT_FFMPEG_NATIVE

#include <QString>
#include <functional>

namespace FFmpegNative {

/// Returns duration of a media file in fractional seconds, or 0.0 on error.
double getDuration(const QString &filePath);

/// Returns true when the file contains at least one valid video stream.
bool hasVideoStream(const QString &filePath);

/// Extracts the audio track from `input`, resamples to 44100 Hz / stereo or
/// mono / Int16, optionally trims `offsetMs` from the start, and writes a
/// PCM WAV to `output`.
/// `filterStr` hints at the desired channel layout (e.g. "mono").
bool extractAudio(const QString &input, const QString &output,
                  qint64 offsetMs = 0,
                  const QString &filterStr = {});

/// Full render: vocal audio + webcam video + playback media → final mix.
/// progressCb is invoked with 0.0–1.0 progress values on the calling thread.
bool renderVideo(const QString &audioPath,          ///< enhanced vocal audio (WAV)
                 const QString &webcamPath,         ///< webcam recording
                 const QString &playbackPath,       ///< original karaoke playback
                 const QString &outputPath,         ///< render destination
                 double         vocalVolume,        ///< scalar gain (1.0 = unity)
                 qint64         audioOffsetMs,      ///< vocal timing adjustment (ms)
                 qint64         videoOffsetMs,      ///< webcam timing adjustment (ms)
                 const QString &resolution,         ///< e.g. "1920x1080"
                 const QString &audioMasterization, ///< libavfilter chain string
                 const QString &rawVocalPath = {},  ///< raw vocal for pitch overlay (optional)
                 std::function<void(double)> progressCb = {});

} // namespace FFmpegNative

#endif // WAKKAQT_FFMPEG_NATIVE
