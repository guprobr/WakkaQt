#include "complexes.h"

// FFMpeg filter_complexes
    QString _audioMasterization = "aformat=channel_layouts=stereo,afftdn,deesser,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200,aecho=1.0:0.8:84|111:0.21|0.13,";
