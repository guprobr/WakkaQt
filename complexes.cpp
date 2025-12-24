#include "complexes.h"

#include <QDir>
#include <QAudioFormat>

// FFMpeg filter_complexes
    QString _audioMasterization = "aformat=channel_layouts=stereo,afftdn,adeclip,deesser,speechnorm,acompressor=threshold=0.5:ratio=4,highpass=f=200,";
    QString _filterEcho = "aecho=0.8:0.7:32|64:0.21|0.13,";
    QString _filterTreble = "treble=g=8,adeclip,deesser";

// fixed tmp file paths
    QString webcamRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.avi");
    QString audioRecorded = QDir::temp().filePath("WakkaQt_tmp_recording.wav");
    QString tunedRecorded = QDir::temp().filePath("WakkaQt_tmp_tuned.wav");
    QString extractedPlayback = QDir::temp().filePath("WakkaQt_playback.wav");
    QString extractedTmpPlayback = QDir::temp().filePath("WakkaQt_tmp_playback.wav");

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

static bool isYouTubeHost(const QString& host) {
    const QString h = host.toLower();
    return h.contains("youtube.com") || h.contains("youtu.be");
}

