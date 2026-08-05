#pragma once
#include <QString>
#include <functional>
#include <atomic>

class VocalSeparator {
public:
    static QString modelDir();
    static QString modelPath();
    static bool    modelExists();

    // Source and expected integrity hash for the model file at modelPath() —
    // consumed by ModelDownloadJob (see modeldownloadjob.h), which does the
    // actual asynchronous fetch. Kept here since VocalSeparator is the
    // authority on which model this build expects.
    static QString modelUrl();
    static QString modelSha256();

    // Separate vocals from inputFile. workspaceDir is a caller-owned,
    // caller-created scratch directory (e.g. a fresh per-run temp dir) —
    // the instrumental output and any intermediate files this needs are
    // written inside it rather than under fixed shared /tmp names, so two
    // separations (different WakkaQt instances, or overlapping runs) can
    // never collide on the same path. Returns the instrumental WAV's full
    // path (inside workspaceDir) on success, or empty string on error.
    // progressFn called with 0–100. If cancelled is set to true from another
    // thread, returns empty string with errorOut = "Cancelled".
    static QString separate(const QString &inputFile,
                            const QString &workspaceDir,
                            std::function<void(int)> progressFn,
                            QString &errorOut,
                            const std::atomic<bool> *cancelled = nullptr);
};
