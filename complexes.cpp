#include "complexes.h"

#include <QDir>

// FFMpeg filter_complexes
    QString _audioMasterization = "aformat=channel_layouts=stereo,afftdn,deesser,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200,";
    QString _filter_Echo = "treble=g=8,aecho=0.8:0.7:32|64:0.21|0.13,";

// fixed tmp file paths
    QString webcamRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.mp4");
    QString audioRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.wav");
    QString tunedRecorded = QDir::temp().filePath("WakkaQt_extracted_audio.wav");
