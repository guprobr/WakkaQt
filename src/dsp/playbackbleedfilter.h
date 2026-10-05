#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QString>
#include <atomic>
#include <functional>

// Offline acoustic echo cancellation. Both inputs are interleaved Int16 PCM
// at the same rate; the reference is the unprocessed audio sent to speakers.
// Output keeps the microphone's channel layout, duration and song timeline.
class PlaybackBleedFilter
{
public:
    struct Result {
        QByteArray samples;
        QString error;
        bool applied = false;
        int delaySamples = 0; // positive: microphone echo follows reference
        double confidence = 0;
    };

    static bool isAvailable();
    static Result process(const QByteArray &microphone, const QByteArray &playback,
                          const QAudioFormat &format, qint64 referenceStartFrame = 0,
                          const std::atomic<bool> *cancelled = nullptr,
                          const std::function<void(int)> &progress = {});
};
