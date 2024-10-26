#include "complexes.h"

// filterComplexes Lego bits©
    QString _audioMasterization = "aformat=channel_layouts=stereo,afftdn=nf=-20:nr=10:nt=w,deesser,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200,";
    QString _audioTreble = "treble=g=12,";
    QString _echo_filter = "aecho=0.84:0.69:69|84:0.33|0.21,";
    QString _talent_filter = "lv2=p=http\\\\://gareus.org/oss/lv2/fat1:c=corr=1.0,";
    QString _rubberband_filter = "rubberband=pitch=1.0696:tempo=1.0:detector=compound:phase=laminar:window=standard:smoothing=on:formant=preserved:pitchq=quality:channels=apart,";
    QString _auburn_filter = "lv2=p=https\\\\://www.auburnsounds.com/products/Graillon.html40733133#stereo,";