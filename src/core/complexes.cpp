#include "complexes.h"

#include <QDir>
#include <QAudioFormat>
#include <cstring>

#ifdef WAKKAQT_FFMPEG_NATIVE
#include "ffmpegnative.h"
#else
#include <QProcess>
#endif

QStringList allowedRenderExtensions(bool hasWebcam)
{
    return hasWebcam
        ? QStringList{"mp4", "mkv", "webm", "avi", "mp3", "flac", "wav", "opus"}
        : QStringList{"mp3", "flac", "wav", "opus"};
}

QString renderSaveFilter(bool hasWebcam)
{
    static const QString kRenderFilter =
        "MP4 Files (*.mp4);;MKV Files (*.mkv);;WebM Files (*.webm);;AVI Files (*.avi);;"
        "MP3 Files (*.mp3);;FLAC Files (*.flac);;WAV Files (*.wav);;Opus Files (*.opus)";
    static const QString kAudioOnlyRenderFilter =
        "MP3 Files (*.mp3);;FLAC Files (*.flac);;WAV Files (*.wav);;Opus Files (*.opus)";
    return hasWebcam ? kRenderFilter : kAudioOnlyRenderFilter;
}

void syncDefaultSuffixToFilter(QFileDialog &dlg)
{
    QObject::connect(&dlg, &QFileDialog::filterSelected, &dlg,
        [&dlg](const QString &filter) {
            const int star = filter.lastIndexOf("*.");
            if (star < 0) return;
            const QString ext = filter.mid(star + 2).section(')', 0, 0).trimmed().toLower();
            if (!ext.isEmpty())
                dlg.setDefaultSuffix(ext);
        });
}

bool mediaHasVideoStream(const QString &filePath)
{
#ifdef WAKKAQT_FFMPEG_NATIVE
    return FFmpegNative::hasVideoStream(filePath);
#else
    QProcess p;
    p.start("ffprobe", {"-v", "quiet", "-select_streams", "v:0",
                        "-show_entries", "stream=codec_type",
                        "-of", "default=noprint_wrappers=1:nokey=1", filePath});
    p.waitForFinished(10000);
    return p.exitCode() == 0 && !p.readAllStandardOutput().trimmed().isEmpty();
#endif
}

// FFMpeg filter_complexes
    const QString _audioEnhance = "aformat=channel_layouts=mono,";
    const QString _filterEcho = "aecho=0.8:0.7:32|64:0.21|0.13,";
    const QString _audioMasterization = "deesser,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200";

// Video effects — see complexes.h. Multiple entries can be enabled at once
// (PreviewDialog joins their built chains with commas); frei0r-backed
// entries are hidden automatically at runtime if the plugin isn't installed
// (VideoEffectProcessor::isChainAvailable, tested with each param at its
// default value).
const QVector<VideoEffectPreset> videoEffectPresets = {
    {
        "vertigo", "🌀 Vertigo",
        [](const QVector<double> &p) {
            return QString("frei0r=vertigo:%1|%2")
                .arg(p.value(0, 0.20), 0, 'f', 3)
                .arg(p.value(1, 0.03), 0, 'f', 3);
        },
        {
            { "speed", "Speed",     0.0, 1.0, 0.20, 2 },
            { "zoom",  "Zoom Rate", 0.0, 1.0, 0.03, 2 },
        }
    },
    {
        "technicolor", "🎞️ Technicolor",
        [](const QVector<double> &p) {
            // Single friendly "Intensity" knob, linearly interpolated
            // between neutral (0) and the full 2-strip dye-transfer
            // emulation (1) across all four underlying curves/eq values.
            const double t = p.value(0, 0.70);
            const double redMid  = 0.50 + t * (0.58 - 0.50);
            const double blueMid = 0.50 + t * (0.42 - 0.50);
            const double sat     = 1.00 + t * (1.40 - 1.00);
            const double con     = 1.00 + t * (1.15 - 1.00);
            return QString("curves=r='0/0 0.5/%1 1/1':b='0/0 0.5/%2 1/1',eq=saturation=%3:contrast=%4")
                .arg(redMid, 0, 'f', 4).arg(blueMid, 0, 'f', 4)
                .arg(sat, 0, 'f', 4).arg(con, 0, 'f', 4);
        },
        {
            { "intensity", "Intensity", 0.0, 1.0, 0.70, 2 },
        }
    },
    {
        "sepia", "🟤 Sepia",
        [](const QVector<double> &p) {
            // Blend between the identity color matrix and the classic sepia
            // matrix by Strength, so 0 = no change and 1 = full sepia.
            const double t = p.value(0, 0.80);
            const double rr = 1.0 - 0.607 * t, rg = 0.769 * t, rb = 0.189 * t;
            const double gr = 0.349 * t, gg = 1.0 - 0.314 * t, gb = 0.168 * t;
            const double br = 0.272 * t, bg = 0.534 * t, bb = 1.0 - 0.869 * t;
            return QString("colorchannelmixer=%1:%2:%3:0:%4:%5:%6:0:%7:%8:%9")
                .arg(rr, 0, 'f', 4).arg(rg, 0, 'f', 4).arg(rb, 0, 'f', 4)
                .arg(gr, 0, 'f', 4).arg(gg, 0, 'f', 4).arg(gb, 0, 'f', 4)
                .arg(br, 0, 'f', 4).arg(bg, 0, 'f', 4).arg(bb, 0, 'f', 4);
        },
        {
            { "strength", "Strength", 0.0, 1.0, 0.80, 2 },
        }
    },
    {
        "contrast", "⚡ High Contrast",
        [](const QVector<double> &p) {
            const double t = p.value(0, 0.60);
            return QString("eq=contrast=%1:brightness=%2")
                .arg(1.0 + t * 1.0, 0, 'f', 3).arg(t * 0.05, 0, 'f', 3);
        },
        {
            { "amount", "Amount", 0.0, 1.0, 0.60, 2 },
        }
    },
    {
        "blackwhite", "⚫ Black & White",
        [](const QVector<double> &p) {
            const double t = p.value(0, 1.0); // 0 = full color, 1 = full grayscale
            return QString("hue=s=%1").arg(1.0 - t, 0, 'f', 3);
        },
        {
            { "strength", "Strength", 0.0, 1.0, 1.0, 2 },
        }
    },
    {
        "vignette", "🔦 Vignette",
        [](const QVector<double> &p) {
            constexpr double kPi = 3.14159265358979323846;
            const double t = p.value(0, 0.60);
            const double angle = kPi / 2.0 - t * (kPi / 2.0 - kPi / 8.0);
            return QString("vignette=angle=%1").arg(angle, 0, 'f', 4);
        },
        {
            { "strength", "Strength", 0.0, 1.0, 0.60, 2 },
        }
    },
    {
        "negative", "🌓 Negative",
        [](const QVector<double> &) { return QString("negate"); },
        {}
    },
    {
        "mirror", "🪞 Mirror",
        [](const QVector<double> &) { return QString("hflip"); },
        {}
    },
    {
        "wave", "🌊 Wave",
        [](const QVector<double> &p) {
            const double amp = p.value(0, 6.0);
            const double speed = p.value(1, 0.5);
            const QString offset = QString("(X,Y+%1*sin(2*PI*X/90+2*PI*T*%2))")
                .arg(amp, 0, 'f', 2).arg(speed, 0, 'f', 2);
            return QString("geq=r='r%1':g='g%1':b='b%1'").arg(offset);
        },
        {
            { "amplitude", "Amplitude", 0.0, 20.0, 6.0, 1 },
            { "speed",     "Speed",     0.0, 2.0,  0.5, 2 },
        }
    },
    {
        "trails", "👻 Trails",
        [](const QVector<double> &p) {
            const int frames = qBound(2, qRound(p.value(0, 4.0)), 9);
            return QString("tmix=frames=%1").arg(frames);
        },
        {
            { "length", "Trail Length", 2.0, 9.0, 4.0, 0 },
        }
    },
    {
        "vhs", "📼 VHS / Old Film",
        [](const QVector<double> &p) {
            const int grain = qBound(0, qRound(p.value(0, 20.0)), 40);
            return QString("noise=alls=%1:allf=t+u,curves=preset=vintage").arg(grain);
        },
        {
            { "grain", "Grain Amount", 0.0, 40.0, 20.0, 0 },
        }
    },
    {
        "edgedetect", "✏️ Edge Detect",
        [](const QVector<double> &p) {
            return QString("edgedetect=mode=colormix:high=%1").arg(p.value(0, 0.40), 0, 'f', 3);
        },
        {
            { "sensitivity", "Sensitivity", 0.1, 0.6, 0.40, 2 },
        }
    },
};

// fixed tmp file paths
    QString webcamRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.mkv");
    QString audioRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.wav");
    QString tunedRecorded = QDir::temp().filePath("WakkaQt_tmp_tuned.wav");
    QString extractedPlayback = QDir::temp().filePath("WakkaQt_playback.wav");
    QString extractedTmpPlayback = QDir::temp().filePath("WakkaQt_tmp_playback.wav");

namespace {
// Captured at static-init time, before anything can repoint the globals
// above — relies on intra-TU init order following declaration order.
const QString kDefaultWebcamRecorded = webcamRecorded;
const QString kDefaultAudioRecorded = audioRecorded;
const QString kDefaultExtractedTmpPlayback = extractedTmpPlayback;
}

void resetRecordingTempPaths()
{
    webcamRecorded = kDefaultWebcamRecorded;
    audioRecorded = kDefaultAudioRecorded;
    extractedTmpPlayback = kDefaultExtractedTmpPlayback;
}

bool isAudioOnlyFile(const QString &path) {
    return path.endsWith("mp3",  Qt::CaseInsensitive)
        || path.endsWith("wav",  Qt::CaseInsensitive)
        || path.endsWith("opus", Qt::CaseInsensitive)
        || path.endsWith("flac", Qt::CaseInsensitive);
}

bool isSingleYouTubeVideoUrl(const QUrl& url) {
    if (!isYouTubeHost(url.host())) return false;

    // Block playlists
    QUrlQuery q(url);
    if (q.hasQueryItem("list")) return false;

    // OK watch?v=, youtu.be/<id>, shorts/<id>
    const QString path = url.path();
    if (path.startsWith("/watch")) {
        return q.hasQueryItem("v");
    }
    if (url.host().contains("youtu.be")) {
        // youtu.be/<id>
        return path.size() > 1; // is there something after '/' ?
    }
    if (path.startsWith(QStringLiteral("/shorts/"))) {
        return path.length() > int(sizeof("/shorts/") - 1);
    }

    // fallback: allow std "?watch"
    return q.hasQueryItem("v");
}


// utility function to write the WAVE headers
void writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize, const QByteArray &pcmData)
{
    // Prepare header values
    qint32 sampleRate = format.sampleRate(); 
    qint16 numChannels = format.channelCount(); 
    qint16 bitsPerSample = format.bytesPerSample() * 8; // Convert bytes to bits
    qint32 byteRate = sampleRate * numChannels * (bitsPerSample / 8); // Calculate byte rate
    qint16 blockAlign = numChannels * (bitsPerSample / 8); // Calculate block align
    qint16 audioFormatValue = 1; // 1 for PCM format

    // Create header
    QByteArray header;
    header.append("RIFF");                                         // Chunk ID
    qint32 chunkSize = dataSize + 36;                            // Data size + 36 bytes for the header
    header.append(reinterpret_cast<const char*>(&chunkSize), sizeof(chunkSize)); // Chunk Size
    header.append("WAVE");                                         // Format
    header.append("fmt ");                                         // Subchunk 1 ID
    qint32 subchunk1Size = 16;                                   // Subchunk 1 Size (16 for PCM)
    header.append(reinterpret_cast<const char*>(&subchunk1Size), sizeof(subchunk1Size)); // Subchunk 1 Size
    header.append(reinterpret_cast<const char*>(&audioFormatValue), sizeof(audioFormatValue)); // Audio Format
    header.append(reinterpret_cast<const char*>(&numChannels), sizeof(numChannels));        // Channels
    header.append(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));          // Sample Rate
    header.append(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));                // Byte Rate
    header.append(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));            // Block Align
    header.append(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));      // Bits per Sample
    header.append("data");                                         // Subchunk 2 ID
    qint32 subchunk2Size = pcmData.size();                       // Size of the audio data
    header.append(reinterpret_cast<const char*>(&subchunk2Size), sizeof(subchunk2Size)); // Subchunk2 Size

    // Write the header and audio data to the file in one go
    file.write(header);
    file.write(pcmData); // Write audio data after the header

    qDebug() << "WAV header and audio data written.";
}

// See complexes.h. Walks actual RIFF chunks instead of assuming a fixed
// 44-byte header, so a "fmt " chunk extension or an extra chunk (e.g. a
// LIST/INFO block some encoders prepend before "data") doesn't shift the
// payload out from under a fixed-offset read — and, critically, the
// returned samples never include header bytes that would otherwise get
// treated as (and, for anything routed straight to a QAudioSink, audibly
// played as) audio.
PcmBuffer parseWavPcm(const QByteArray &wavBytes)
{
    PcmBuffer result;

    if (wavBytes.size() < 12
        || wavBytes.mid(0, 4) != "RIFF"
        || wavBytes.mid(8, 4) != "WAVE") {
        qWarning() << "parseWavPcm: not a RIFF/WAVE buffer (size" << wavBytes.size() << ")";
        return result;
    }

    quint16 audioFormatTag = 0, numChannels = 0, bitsPerSample = 0, blockAlign = 0;
    quint32 sampleRate = 0, byteRate = 0;
    bool haveFmt = false;

    qint64 pos = 12;
    while (pos + 8 <= wavBytes.size()) {
        const QByteArray chunkId = wavBytes.mid(pos, 4);
        quint32 chunkSize = 0;
        memcpy(&chunkSize, wavBytes.constData() + pos + 4, 4);
        const qint64 dataStart = pos + 8;

        if (dataStart + qint64(chunkSize) > wavBytes.size()) {
            qWarning() << "parseWavPcm: chunk" << chunkId << "size" << chunkSize
                       << "runs past end of buffer, stopping";
            break;
        }

        if (chunkId == "fmt ") {
            if (chunkSize < 16) {
                qWarning() << "parseWavPcm: 'fmt ' chunk too small:" << chunkSize;
                return result;
            }
            const char *fmt = wavBytes.constData() + dataStart;
            memcpy(&audioFormatTag, fmt + 0, 2);
            memcpy(&numChannels,    fmt + 2, 2);
            memcpy(&sampleRate,     fmt + 4, 4);
            memcpy(&byteRate,       fmt + 8, 4);
            memcpy(&blockAlign,     fmt + 12, 2);
            memcpy(&bitsPerSample,  fmt + 14, 2);

            // WAVE_FORMAT_EXTENSIBLE (0xFFFE) defers the real format to a
            // SubFormat GUID appended after the base 16-byte struct — its
            // first two bytes are the actual format tag (1=PCM, 3=IEEE
            // float), same convention as the plain tag field.
            if (audioFormatTag == 0xFFFE) {
                if (chunkSize < 40) {
                    qWarning() << "parseWavPcm: WAVE_FORMAT_EXTENSIBLE 'fmt ' chunk too small:" << chunkSize;
                    return result;
                }
                quint16 subFormatTag = 0;
                memcpy(&subFormatTag, fmt + 24, 2);
                audioFormatTag = subFormatTag;
            }
            haveFmt = true;
        } else if (chunkId == "data") {
            if (!haveFmt) {
                qWarning() << "parseWavPcm: 'data' chunk arrived before 'fmt '";
                return result;
            }

            // Only uncompressed PCM (tag 1) or IEEE float (tag 3) actually
            // store raw samples in the data chunk — anything else (ADPCM,
            // mu-law/A-law, MP3-in-WAV, etc.) packs its bytes in a
            // codec-specific way and must not be read as if it were PCM
            // just because bitsPerSample happens to match a PCM width.
            if (audioFormatTag != 1 && audioFormatTag != 3) {
                qWarning() << "parseWavPcm: unsupported compressed format tag" << audioFormatTag
                           << "— only uncompressed PCM/IEEE-float WAV is supported";
                return result;
            }

            QAudioFormat::SampleFormat sampleFormat = QAudioFormat::Unknown;
            if (audioFormatTag == 3 && bitsPerSample == 32) sampleFormat = QAudioFormat::Float;
            else if (audioFormatTag == 1 && bitsPerSample == 16) sampleFormat = QAudioFormat::Int16;
            else if (audioFormatTag == 1 && bitsPerSample == 32) sampleFormat = QAudioFormat::Int32;
            else if (audioFormatTag == 1 && bitsPerSample == 8)  sampleFormat = QAudioFormat::UInt8;

            if (sampleFormat == QAudioFormat::Unknown || numChannels == 0 || sampleRate == 0) {
                qWarning() << "parseWavPcm: unsupported/invalid fmt —"
                           << "tag=" << audioFormatTag << "bits=" << bitsPerSample
                           << "channels=" << numChannels << "rate=" << sampleRate;
                return result;
            }

            // Cross-check blockAlign/byteRate against what the declared
            // format actually implies — a mismatch means a malformed or
            // hand-edited header, which is exactly the kind of file that
            // would otherwise get silently misread as valid PCM.
            const int bytesPerSample = bitsPerSample / 8;
            const quint32 expectedBlockAlign = quint32(numChannels) * quint32(bytesPerSample);
            if (blockAlign != 0 && blockAlign != expectedBlockAlign) {
                qWarning() << "parseWavPcm: blockAlign" << blockAlign << "!= expected"
                           << expectedBlockAlign << "(channels * bytesPerSample) — malformed fmt chunk";
                return result;
            }
            const quint32 expectedByteRate = sampleRate * expectedBlockAlign;
            if (byteRate != 0 && byteRate != expectedByteRate) {
                qWarning() << "parseWavPcm: byteRate" << byteRate << "!= expected"
                           << expectedByteRate << "— malformed fmt chunk";
                return result;
            }

            QByteArray samples = wavBytes.mid(dataStart, int(chunkSize));
            const int frameBytes = numChannels * bytesPerSample;
            if (frameBytes > 0 && samples.size() % frameBytes != 0) {
                const int trimmed = samples.size() - (samples.size() % frameBytes);
                qWarning() << "parseWavPcm: data chunk size" << samples.size()
                           << "is not a whole number of" << frameBytes << "-byte frames; trimming"
                           << (samples.size() - trimmed) << "trailing byte(s)";
                samples.truncate(trimmed);
            }

            result.samples = samples;
            result.format.setSampleRate(int(sampleRate));
            result.format.setChannelCount(int(numChannels));
            result.format.setSampleFormat(sampleFormat);
            return result;
        }

        // Chunks are word-aligned: an odd-sized chunk has one pad byte after it.
        pos = dataStart + qint64(chunkSize) + (chunkSize & 1);
    }

    qWarning() << "parseWavPcm: no 'data' chunk found";
    return result;
}

static bool isYouTubeHost(const QString& host) {
    const QString h = host.toLower();
    return h.contains("youtube.com") || h.contains("youtu.be");
}

