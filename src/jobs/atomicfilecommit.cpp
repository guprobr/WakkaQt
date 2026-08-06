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
    QFile::remove(backupPath); // clear any leftover from a previous crashed attempt

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
