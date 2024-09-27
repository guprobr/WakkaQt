#include "audioamplifier.h"
#include <QDebug>
#include <QTimer>
#include <QAudioSink>
#include <cmath>  // Amplification logic

AudioAmplifier::AudioAmplifier(const QAudioFormat &format, QObject *parent)
    : QObject(parent),
      audioFormat(format),
      volumeFactor(1.0),
      playbackPosition(0)
{
    // Initialize audio sink with scoped pointer
    audioSink.reset(new QAudioSink(format, this));
    connect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);

    // Initialize QBuffer
    audioBuffer.reset(new QBuffer());
    audioBuffer->setBuffer(new QByteArray());

    // Initialize timer for pushing data
    dataPushTimer.reset(new QTimer(this));
    connect(dataPushTimer.data(), &QTimer::timeout, this, &AudioAmplifier::checkBufferState);
}

AudioAmplifier::~AudioAmplifier() {
    stop();  // Ensure audio stops and resources are cleaned up
    resetAudioComponents();  // Clear all resources
}

void AudioAmplifier::start() {
    // Verificar se há dados originais para processar
    if (!originalAudioData.isEmpty()) {
        applyAmplification();

        // Validação do tamanho do buffer de áudio antes de abrir e iniciar
        if (amplifiedAudioData.size() < 1024) { // Exemplo: Buffer mínimo de 1024 bytes
            qWarning() << "Tamanho do buffer de áudio amplificado é muito pequeno. Tamanho:" << amplifiedAudioData.size();
            return; // Evita iniciar a reprodução com um buffer muito pequeno
        }

        // Verifique se o buffer já está aberto e feche antes de reiniciar
        if (audioBuffer->isOpen()) {
            audioBuffer->close();
        }

        // Configura os dados no QBuffer e abre para leitura
        audioBuffer->setData(amplifiedAudioData);
        audioBuffer->open(QIODevice::ReadOnly);

        // Posicionar no início ou na posição salva
        audioBuffer->seek(playbackPosition);

        audioSink->start(audioBuffer.data()); // Iniciar o playback
        dataPushTimer->start(11); // Começa a verificar o estado do buffer periodicamente

        qDebug() << "Iniciada a reprodução com dados de áudio amplificados.";
    } else {
        qWarning() << "Nenhum dado de áudio para reproduzir.";
    }
}

void AudioAmplifier::checkBufferState() {
    if (!audioBuffer->isOpen() || !audioSink || audioSink->isNull()) return;

    qint64 totalDuration = audioBuffer->size() * 1000000 / (audioSink->format().sampleRate() * 
                           audioSink->format().channelCount() * audioSink->format().bytesPerSample());
    qint64 processedDuration = audioSink->processedUSecs();
    qint64 threshold = 800000;  // Exemplo: parar 800 ms antes do fim

    // Verifique o tamanho do buffer durante a reprodução
    if (audioBuffer->bytesAvailable() < 512) {  // Exemplo: Se menos de 512 bytes restantes
        qWarning() << "O buffer de áudio está muito pequeno durante a reprodução. Reiniciando.";
        stop();
        rewind();
        start();
        return;
    }

    // Se estiver perto do fim, reiniciar
    if (processedDuration >= totalDuration - threshold) {
        qDebug() << "Buffer quase no fim. Reiniciando áudio.";
        stop();
        rewind();
        start();
    }
}

void AudioAmplifier::stop() {
    if (audioSink->state() == QAudio::ActiveState) {
        playbackPosition = audioBuffer->pos(); // Store current position before stopping
        audioSink->stop();  // Stop the audio sink
        qDebug() << "Stopped playback.";
    }

    // Close the buffer if playback is fully stopped
    if (audioBuffer->isOpen() && !isPlaying()) {
        audioBuffer->close();
        qDebug() << "Closed audio buffer.";
    }

    dataPushTimer->stop();  // Stop the timer when playback stops
}

void AudioAmplifier::setVolumeFactor(double factor) {
    if (factor != volumeFactor) {
        volumeFactor = factor;

        // Only restart playback if there is audio data
        if (!originalAudioData.isEmpty() && isPlaying()) {
            stop();
            start(); // Start playback with new volume
        }
    }
}

void AudioAmplifier::setAudioData(const QByteArray &data) {
    originalAudioData = data;  // Store the new audio data
}

void AudioAmplifier::applyAmplification() {
    amplifiedAudioData.clear();
    amplifiedAudioData.reserve(originalAudioData.size());

    const char *data = originalAudioData.constData();
    for (int i = 0; i < originalAudioData.size(); i += 2) {
        // Converter os dois bytes para qint16 (16-bit áudio)
        qint16 sample = static_cast<qint16>(static_cast<unsigned char>(data[i]) | 
                                            (static_cast<unsigned char>(data[i + 1]) << 8));

        // Amplificar o sample e clipear para evitar overflow
        int amplifiedSample = static_cast<int>(sample * volumeFactor);
        amplifiedSample = std::min(std::max(amplifiedSample, -32768), 32767);

        // Escrever o sample amplificado de volta no formato little-endian
        amplifiedAudioData.append(static_cast<char>(amplifiedSample & 0xFF));
        amplifiedAudioData.append(static_cast<char>((amplifiedSample >> 8) & 0xFF));
    }

    if (amplifiedAudioData.isEmpty()) {
        qWarning() << "Amplified audio data is empty!";
    }
}



bool AudioAmplifier::isPlaying() const {
    return audioSink->state() == QAudio::ActiveState; // Check if audio is currently playing
}

void AudioAmplifier::rewind() {
    if (audioBuffer->isOpen()) {
        audioBuffer->seek(0);  // Seek the buffer to the start
    } else {
        qWarning() << "Audio buffer is not open. Cannot rewind.";
    }
    playbackPosition = 0;  // Reset playback position
}

void AudioAmplifier::handleStateChanged(QAudio::State newState) {
    if (newState == QAudio::StoppedState) {
        if (audioSink && audioSink->error() != QAudio::NoError) {
            qWarning() << "Audio playback error:" << audioSink->error();
        }
    }
}

void AudioAmplifier::resetAudioComponents() {
    if (audioSink && audioSink->state() == QAudio::ActiveState) {
        audioSink->stop();
    }

    if (audioBuffer && audioBuffer->isOpen()) {
        audioBuffer->close();
    }

    audioBuffer.reset(new QBuffer(this));
    audioBuffer->setData(amplifiedAudioData);
    audioBuffer->open(QIODevice::ReadOnly);

    audioSink.reset(new QAudioSink(audioFormat, this));
    connect(audioSink.data(), &QAudioSink::stateChanged, this, &AudioAmplifier::handleStateChanged);
    audioSink->setBufferSize(8192);  // buffer

}
