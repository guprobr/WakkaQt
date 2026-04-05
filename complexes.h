#ifndef COMPLEXES_H
#define COMPLEXES_H

#include <QFile>
#include <QAudioFormat>
#include <QString>
#include <QUrlQuery>
#include <QStringView>


extern const QString _audioMasterization;
extern const QString _filterEcho;
extern const QString _audioEnhance;

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