#include "audiorecorder.h"
#include "complexes.h"

#include <QApplication>

#include <QDebug>

namespace {

// Sits between QAudioSource and the real output QFile. Every buffer handed
// to writeData() is silently discarded (counted, not written) until arm()
// is called — after that it passes straight through. This gives the WAV
// file zero pre-roll by construction: the capture device is already running
// (and warmed up) well before the sync mark, we just don't keep any of what
// it produced before that instant.
class GatedFileDevice : public QIODevice
{
public:
    explicit GatedFileDevice(QFile *backing, QObject *parent = nullptr)
        : QIODevice(parent), m_backing(backing) {}

    void arm() { m_armed = true; }
    qint64 discardedBytes() const { return m_discarded; }

protected:
    qint64 readData(char *, qint64) override { return -1; } // write-only

    qint64 writeData(const char *data, qint64 len) override {
        if (!m_armed) {
            m_discarded += len;
            return len; // report success — the source shouldn't see a stall
        }
        return m_backing->write(data, len);
    }

private:
    QFile *m_backing;
    bool m_armed = false;
    qint64 m_discarded = 0;
};

} // namespace

AudioRecorder::AudioRecorder(QAudioDevice selectedDevice, QObject* parent)
    : QObject(parent),
      m_audioSource(nullptr),
      m_isRecording(false)
{

    m_audioFormat = selectedDevice.preferredFormat();

    if ( !selectedDevice.isFormatSupported(m_audioFormat) ) {
        qWarning() << "Audio input device Preferred format is bogus.";

        // Create a format to give a chance to 24-bit configuration
        QAudioFormat format;
        format.setSampleFormat(QAudioFormat::SampleFormat::Int32); // Use Int32 to test
        format.setChannelCount(1); 
        format.setSampleRate(48000);

        // Check if the device supports this configuration
        if (selectedDevice.isFormatSupported(format)) {

            m_audioFormat.setSampleRate(48000);
            m_audioFormat.setChannelCount(1);
            m_audioFormat.setSampleFormat(QAudioFormat::SampleFormat::Int32);

        } else {

            m_audioFormat.setSampleRate(44100);
            m_audioFormat.setChannelCount(1);
            m_audioFormat.setSampleFormat(QAudioFormat::SampleFormat::Int16);

        }
    }

if ( m_audioFormat.sampleFormat() == QAudioFormat::SampleFormat::Float ) // a bug since 6.8.2, always returning Float
    {
        // Create a format to give a chance to 24-bit configuration
        QAudioFormat format;
        format.setSampleFormat(QAudioFormat::SampleFormat::Int32); // Use Int32 to test
        format.setChannelCount(m_audioFormat.channelCount()); 
        format.setSampleRate(m_audioFormat.sampleRate());

        // Check if the device supports this configuration
        if (selectedDevice.isFormatSupported(format)) {

            m_audioFormat.setSampleFormat(QAudioFormat::SampleFormat::Int32);

        } else {

            m_audioFormat.setSampleFormat(QAudioFormat::SampleFormat::Int16);

        }
    }
    
    m_selectedDevice = selectedDevice;
    m_audioSource = new QAudioSource(m_selectedDevice, m_audioFormat, this);
    m_audioSource->setVolume(1.0f);

    // Same pattern SndWidget already uses for its own QAudioSource: a lost
    // device surfaces as stateChanged(StoppedState) with a non-NoError code,
    // not an exception. Only report it if we were actually mid-recording —
    // the same transition happens harmlessly on a normal stopRecording().
    connect(m_audioSource, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (state != QAudio::StoppedState || !m_isRecording)
            return;
        if (m_audioSource->error() == QAudio::NoError)
            return;

        qWarning() << "AudioRecorder: capture device stopped with error:" << m_audioSource->error();
        emit captureError("Microphone input error (" + audioErrorToString(m_audioSource->error())
                           + ") — it may have been disconnected.");
    });
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

    // Reserve the 44-byte WAV header up front with placeholder (zero) size
    // fields, so raw PCM streams in directly behind it as QAudioSource
    // writes. stopRecording() then only has to patch the two size fields in
    // place instead of reading the whole recording back into memory and
    // rewriting the entire file — the old approach, which meant a full
    // readAll() + rewrite of potentially hundreds of MB for a long take.
    writeWavHeader(m_outputFile, m_audioFormat, 0, QByteArray());

    // Start recording audio through the gate, closed until armSync(). The
    // capture device starts pulling data right away (warming up / avoiding
    // startup underruns), but none of it is kept until the sync mark.
    m_gate.reset(new GatedFileDevice(&m_outputFile, this));
    m_gate->open(QIODevice::WriteOnly);

    m_audioSource->start(m_gate.data());
    m_isRecording = true;
    qWarning() << "AudioRecorder Started (gated, awaiting sync mark)";
}

void AudioRecorder::armSync()
{
    if (m_gate)
        static_cast<GatedFileDevice*>(m_gate.data())->arm();
}

qint64 AudioRecorder::preRollMs() const
{
    if (!m_gate)
        return 0;
    const qint64 discarded = static_cast<GatedFileDevice*>(m_gate.data())->discardedBytes();
    // durationForBytes() takes a qint32 and returns microseconds.
    return m_audioFormat.durationForBytes(qint32(qMin<qint64>(discarded, INT32_MAX))) / 1000;
}

void AudioRecorder::stopRecording()
{
    if (!m_isRecording) return;

    // Stop recording
    m_audioSource->stop();

    // Close the output file to ensure all data is written
    m_outputFile.close();

    // Reopen ReadWrite (not WriteOnly, which truncates) so the PCM data
    // already on disk is left untouched — only the two WAV size fields get
    // patched in place, at the offsets writeWavHeader() puts them at.
    if (!m_outputFile.open(QIODevice::ReadWrite)) {
        qWarning() << "Failed to reopen AudioRecorder output file to patch WAV header.";
        m_isRecording = false;
        return;
    }

    static constexpr qint64 kHeaderSize = 44;
    const qint64 dataSize = m_outputFile.size() - kHeaderSize;
    if (dataSize < 0) {
        qWarning() << "AudioRecorder: output file smaller than WAV header, cannot patch.";
        m_outputFile.close();
        m_isRecording = false;
        return;
    }

    const qint32 chunkSize = qint32(dataSize + 36); // RIFF chunk size: data + 36 header bytes
    const qint32 subchunk2Size = qint32(dataSize);  // "data" subchunk size

    m_outputFile.seek(4); // RIFF chunk size field
    m_outputFile.write(reinterpret_cast<const char*>(&chunkSize), sizeof(chunkSize));
    m_outputFile.seek(40); // "data" subchunk size field
    m_outputFile.write(reinterpret_cast<const char*>(&subchunk2Size), sizeof(subchunk2Size));

    m_outputFile.close();

    m_isRecording = false;
    qWarning() << "AudioRecorder stopped";
}

bool AudioRecorder::isRecording() const
{
    return m_isRecording;
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

QString AudioRecorder::audioErrorToString(QAudio::Error error) {
    switch (error) {
        case QAudio::NoError:
            return "no error";
        case QAudio::OpenError:
            return "could not open device";
        case QAudio::IOError:
            return "I/O error";
        case QAudio::UnderrunError:
            return "buffer underrun";
        case QAudio::FatalError:
            return "fatal device error";
        default:
            return "unknown error";
    }
}