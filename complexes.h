#ifndef COMPLEXES_H
#define COMPLEXES_H

#include <QFile>
#include <QAudioFormat>
#include <QString>
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

extern QString webcamRecorded;
extern QString audioRecorded;
extern QString tunedRecorded;
extern QString extractedPlayback;
extern QString extractedTmpPlayback;

void writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize, const QByteArray &pcmData);
static bool isYouTubeHost(const QString& host);
bool isSingleYouTubeVideoUrl(const QUrl& url);


#endif