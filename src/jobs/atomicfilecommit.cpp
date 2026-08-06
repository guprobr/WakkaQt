#include "atomicfilecommit.h"

#include <QFile>
#include <QFileInfo>

QString sidecarPathFor(const QString &finalPath, const QString &tag)
{
    const QFileInfo fi(finalPath);
    const QString ext = fi.suffix();
    if (ext.isEmpty())
        return finalPath + "." + tag;
    return fi.absolutePath() + "/" + fi.completeBaseName() + "." + tag + "." + ext;
}

QString commitPartialOverFinal(const QString &partialPath, const QString &finalPath)
{
    const QString backupPath = sidecarPathFor(finalPath, "backup");
    // Checked, not fire-and-forget: if a leftover backup from a previous
    // crashed attempt can't be removed (permissions, locked by another
    // process), the rename below would fail too — but with a misleading
    // "could not move existing file aside" error that hides the real cause
    // (the stale backup blocking it, not finalPath itself).
    if (QFile::exists(backupPath) && !QFile::remove(backupPath)) {
        return "A leftover backup file from a previous attempt could not be removed:\n" +
               backupPath + "\nThe new output was preserved at:\n" + partialPath;
    }

    const bool hadExisting = QFile::exists(finalPath);
    if (hadExisting && !QFile::rename(finalPath, backupPath)) {
        return "Could not move existing file aside for replacement:\n" + finalPath +
               "\nThe new output was preserved at:\n" + partialPath;
    }

    if (!QFile::rename(partialPath, finalPath)) {
        QString err = "Could not finalize output at:\n" + finalPath +
                      "\nThe new output was preserved at:\n" + partialPath;
        if (hadExisting) {
            // Restore the previous file so a failed commit never loses it —
            // the whole point of staging through a backup instead of just
            // removing finalPath before the rename.
            if (QFile::rename(backupPath, finalPath))
                err += "\nThe previous file was restored.";
            else
                err += "\nThe previous file could not be restored and was preserved at:\n" + backupPath;
        }
        return err;
    }

    if (hadExisting)
        QFile::remove(backupPath);
    return QString();
}
