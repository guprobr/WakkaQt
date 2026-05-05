#pragma once
#include <QString>
#include <functional>

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
    static QString separate(const QString &inputFile,
                            std::function<void(int)> progressFn,
                            QString &errorOut);
};
