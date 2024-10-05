#include "audiorecorder.h"

#include <QDebug>

AudioRecorder::AudioRecorder(QAudioDevice selectedDevice, QObject* parent)
    : QObject(parent),
      m_audioSource(nullptr),
      m_isRecording(false)
{
    qDebug() << "Determine proper format for AudioRecorder";
    m_audioFormat = selectedDevice.preferredFormat();
    
    qDebug() << "Preferred Audio Format:";
    qDebug() << "Sample rate:" << m_audioFormat.sampleRate();
    qDebug() << "Channels:" << m_audioFormat.channelCount();
    qDebug() << "Sample size:" << m_audioFormat.bytesPerSample();

    if (!m_audioFormat.sampleRate()) {
        qDebug() << "Preferred format is bogus.";
        m_audioFormat.setSampleRate(192000);
        m_audioFormat.setChannelCount(1);
        m_audioFormat.setSampleFormat(QAudioFormat::SampleFormat::Int16);
    }
    
    if ( !selectedDevice.isFormatSupported(m_audioFormat) ) {
        qDebug() << "Selected format is bogus. Fallback";
        m_audioFormat.setSampleRate(44100);
        m_audioFormat.setChannelCount(1);
        m_audioFormat.setSampleFormat(QAudioFormat::SampleFormat::Int16);
    }

    m_selectedDevice = selectedDevice;
    m_audioSource = new QAudioSource(m_selectedDevice, m_audioFormat, this);

    qWarning() << "AudioRecorder device: " << selectedDevice.description() 
                << " " << m_audioSource->format().sampleRate() << " Hz "  
                << m_audioSource->format().sampleFormat() << " " 
                << m_audioSource->format().channelCount() << " ch";}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
    delete m_audioSource;
}

void AudioRecorder::startRecording(const QString& outputFilePath)
{
    if (m_isRecording) return;

    m_outputFile.setFileName(outputFilePath);
    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open AudioRecorder output file.";
        return;
    }

    // Start recording audio
    m_audioSource->start(&m_outputFile);
    m_isRecording = true;
    qWarning() << "AudioRecorder Started";
}

void AudioRecorder::stopRecording()
{
    if (!m_isRecording) return;

    // Stop recording
    m_audioSource->stop();

    // Close the output file to ensure all data is written
    m_outputFile.close();

    // Open the file in read mode to get the PCM data
    if (!m_outputFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open AudioRecorder output file for reading.";
        return;
    }

    // Read the PCM data into a QByteArray
    QByteArray pcmData = m_outputFile.readAll();
    qint64 dataSize = pcmData.size(); // Get the size of the PCM data

    // Now close the file after reading
    m_outputFile.close();

    // Reopen the file in write mode to write the WAV header
    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to reopen AudioRecorder output file for writing header.";
        return;
    }

    // Write the complete WAV header
    writeWavHeader(m_outputFile, m_audioFormat, dataSize, pcmData);

    // Finalize and close the file
    m_outputFile.close();
    m_isRecording = false;
    qWarning() << "AudioRecorder stopped";
}

bool AudioRecorder::isRecording() const
{
    return m_isRecording;
}

void AudioRecorder::setSampleRate(int sampleRate)
{
    if (m_isRecording) {
        qWarning() <<  "Cannot change sample rate while recording.";
        return;
    }

    m_audioFormat.setSampleRate(sampleRate);
    if (!m_selectedDevice.isFormatSupported(m_audioFormat)) {
        qWarning() << "The selected AudioRecorder sample rate is not supported.";
    }
}

void AudioRecorder::writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize, const QByteArray &pcmData)
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