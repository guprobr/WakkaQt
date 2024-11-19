#include "audiorecorder.h"
#include "complexes.h"

#include <QApplication>
#include <QDebug>

AudioRecorder::AudioRecorder(QAudioDevice selectedDevice, QObject* parent)
    : QObject(parent),
      m_audioSource(nullptr),
      m_isRecording(false)
{

    m_audioFormat = selectedDevice.preferredFormat();
 
    if ( !selectedDevice.isFormatSupported(m_audioFormat) ) {
        qWarning() << "Audio input device Preferred format is bogus.";
        m_audioFormat.setSampleRate(44100);
        m_audioFormat.setChannelCount(1);
        m_audioFormat.setSampleFormat(QAudioFormat::SampleFormat::Int16);
    }

    m_selectedDevice = selectedDevice;
    m_audioSource = new QAudioSource(m_selectedDevice, m_audioFormat, this);
    m_audioSource->setVolume(1.0f);

}

void AudioRecorder::initialize() {

    // Lets update the device label :)
    QString audioRecorderDevice = m_selectedDevice.description()                + 
            " " + QString::number(m_audioSource->format().sampleRate()) + "Hz"  +
            " " + sampleFormatToString(m_audioSource->format().sampleFormat())  +
            " " + QString::number(m_audioSource->format().channelCount())   + "ch";

    emit deviceLabelChanged(audioRecorderDevice);

}

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
    //qint64 dataSize = pcmData.size(); // Get the size of the PCM data

    // Now close the file after reading
    m_outputFile.close();

    // Reopen the file in write mode to write the WAV header
    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to reopen AudioRecorder output file for writing header.";
        return;
    }
    qint64 dataSize = pcmData.size(); // Get the size of the PCM data
    // Write the complete WAV header and file
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

QString AudioRecorder::sampleFormatToString(QAudioFormat::SampleFormat format) {
    switch (format) {
        case QAudioFormat::Unknown:
            return "Unknown";
        case QAudioFormat::UInt8:
            return "UInt8";
        case QAudioFormat::Int16:
            return "Int16";
        case QAudioFormat::Int32:
            return "Int32";
        case QAudioFormat::Float:
            return "Float";
        default:
            return "Unknown";
    }
}