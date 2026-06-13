#pragma once
#include <QString>
#include <functional>
#include <atomic>

class VocalSeparator {
public:
    static QString modelDir();
    static QString modelPath();
    static bool    modelExists();

    // Download model with progress 0–100. Blocking (runs its own event loop).
    // Returns true on success; errorOut set on failure.
    static bool downloadModel(std::function<void(int)> progressFn, QString &errorOut);

    // Separate vocals from inputFile. Returns temp WAV path for the instrumental,
    // or empty string on error. progressFn called with 0–100.
    // If cancelled is set to true from another thread, returns empty string with
    // errorOut = "Cancelled".
    static QString separate(const QString &inputFile,
                            std::function<void(int)> progressFn,
                            QString &errorOut,
                            const std::atomic<bool> *cancelled = nullptr);
};
