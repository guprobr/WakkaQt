#ifndef COMPLEXES_H
#define COMPLEXES_H

#include <QFile>
#include <QAudioFormat>
#include <QString>

extern QString _audioMasterization;
extern QString _filterEcho;
extern QString _filterTreble;

extern QString webcamRecorded;
extern QString audioRecorded;
extern QString tunedRecorded;
extern QString extractedPlayback;
extern QString extractedTmpPlayback;

void writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize, const QByteArray &pcmData);

#endif