#ifndef ATOMICFILECOMMIT_H
#define ATOMICFILECOMMIT_H

#include <QString>

// Shared by RenderJob and VocalSeparationJob: both hand FFmpeg (native
// libav* or the CLI) a file path to write a finished video/audio export to,
// and both want that write to never clobber a pre-existing valid file at
// the real destination if anything goes wrong partway through.
//
// This is the codebase's *other* atomic-commit strategy — the app also has
// MainWindow::finishWavSave() (mainwindowSeparatorMgr.cpp), which uses
// QSaveFile instead. Not a duplicate of this one: they solve the same
// problem for two genuinely different situations that call for different
// techniques.
//   - Here (RenderJob/VocalSeparationJob's mux/encode step): FFmpeg itself
//     writes the new content, directly to a path this code chooses — the
//     sidecar returned by sidecarPathFor(), deliberately placed in the same
//     directory as finalPath so the commit below is a same-filesystem
//     rename(), which is cheap and (per the restore logic below) can be
//     made to fail safe.
//   - finishWavSave(): the new content already exists as a *finished file*
//     sitting in a job's private /tmp workspace, and has to reach a
//     destination the user chose freely (could be a different filesystem —
//     QFile::rename() silently fails across filesystems, it doesn't fall
//     back to a copy). That needs an actual byte copy to the destination's
//     own filesystem, which is exactly what QSaveFile does — and it also
//     already gets the "never touch a pre-existing file until the new one
//     is fully written and valid" property for free from writing through
//     its own temp file, without needing sidecarPathFor()/a backup dance at
//     all (there's no separate "restore the old file on failure" step
//     because the old file is simply never touched until QSaveFile::commit()
//     succeeds).
// If a third call site ever needs this, prefer whichever of the two shapes
// above actually matches it, rather than trying to force a single unified
// helper across both — the cross-filesystem-safe/not-safe distinction is
// real and would just get reintroduced as a branch inside one function.

// FFmpeg infers a file's output container/codec from its *last* extension,
// so a sidecar name must preserve it — "song.mp4" -> "song.partial.mp4",
// not "song.mp4.partial" (which FFmpeg would read as a ".partial" file and
// fail to find a muxer for).
QString sidecarPathFor(const QString &finalPath, const QString &tag);

// Replaces finalPath with the already-written, already-validated file at
// partialPath — assumed to already be a sibling of finalPath (see
// sidecarPathFor()) on the same filesystem, so the renames below are cheap
// and never need a cross-filesystem fallback. If finalPath exists, it's
// moved aside to a backup sidecar *before* the partial is renamed into
// place, so a failure partway through restores the previous file instead of
// losing it — Qt's QFile::rename() refuses to overwrite an existing
// destination, so a naive remove-then-rename sequence has a window where a
// failed rename leaves neither the old nor the new file at finalPath.
// Returns an empty string on success, or an error message describing where
// things were left on failure.
QString commitPartialOverFinal(const QString &partialPath, const QString &finalPath);

#endif // ATOMICFILECOMMIT_H
