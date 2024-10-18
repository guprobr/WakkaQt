#include "complexes.h"

// filterComplexes Lego bits©
    QString _audioMasterization = "afftdn=nf=-20:nr=10:nt=w,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200,";
    QString _audioTreble = "treble=g=12,";
    QString _echo_filter = "aecho=0.84:0.48:69|96:0.21|0.13,";
    QString _talent_filter = "lv2=p=http\\\\://gareus.org/oss/lv2/fat1:c=mode=1|tuning=440|bias=0.5|filter=0.5|offset=0|bendrange=1.0|fastmode=1|corr=1.0,";
    QString _rubberband_filter = "rubberband=pitch=1.0696:tempo=1.0:detector=compound:phase=laminar:window=standard:smoothing=on:formant=preserved:pitchq=quality:channels=apart,";
    QString _auburn_filter = "lv2=p=https\\\\://www.auburnsounds.com/products/Graillon.html40733133#stereo,";