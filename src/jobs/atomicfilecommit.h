#ifndef ATOMICFILECOMMIT_H
#define ATOMICFILECOMMIT_H

#include <QString>

// Shared by RenderJob and VocalSeparationJob: both hand FFmpeg (native
// libav* or the CLI) a file path to write a finished video/audio export to,
// and both want that write to never clobber a pre-existing valid file at
// the real destination if anything goes wrong partway through.

// FFmpeg infers a file's output container/codec from its *last* extension,
// so a sidecar name must preserve it — "song.mp4" -> "song.partial.mp4",
// not "song.mp4.partial" (which FFmpeg would read as a ".partial" file and
// fail to find a muxer for).
QString sidecarPathFor(const QString &finalPath, const QString &tag);

// Replaces finalPath with the already-written, already-validated file at
// partialPath. If finalPath exists, it's moved aside to a backup sidecar
// *before* the partial is renamed into place, so a failure partway through
// restores the previous file instead of losing it — Qt's QFile::rename()
// refuses to overwrite an existing destination, so a naive
// remove-then-rename sequence has a window where a failed rename leaves
// neither the old nor the new file at finalPath. Returns an empty string on
// success, or an error message describing where things were left on failure.
QString commitPartialOverFinal(const QString &partialPath, const QString &finalPath);

#endif // ATOMICFILECOMMIT_H
