#ifndef MODELDOWNLOADJOB_H
#define MODELDOWNLOADJOB_H

#include <QObject>
#include <QString>
#include <QPointer>
#include <QScopedPointer>
#include <QCryptographicHash>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;
class QTimer;

// Downloads a file (specifically: the vocal-separation ONNX model) fully
// asynchronously — no blocking QEventLoop pumped from the GUI thread the way
// VocalSeparator::downloadModel() used to. Writes and hashes incrementally
// as data arrives instead of buffering the whole ~80 MB response in memory,
// aborts a connection that stalls mid-transfer instead of hanging
// indefinitely, refuses a response that grows past a sane size cap, and
// supports cancellation.
class ModelDownloadJob : public QObject
{
    Q_OBJECT
public:
    explicit ModelDownloadJob(QObject *parent = nullptr);
    ~ModelDownloadJob() override;

    // destPath is written via a QSaveFile sibling and only atomically
    // committed once the download completes AND its SHA-256 matches
    // expectedSha256 (lowercase hex) — a failed/cancelled/corrupt download
    // never touches an existing file at destPath.
    void start(const QString &urlStr, const QString &destPath, const QString &expectedSha256);
    void cancel();
    bool isActive() const;
    void waitForFinished(); // pumps a nested event loop until finished() fires; near-instant after cancel()

signals:
    void progress(int percentage); // 0-100, or -1 if the server didn't report a content length
    void finished(bool success, bool cancelled, QString errorMessage);

private:
    void resetInactivityTimer();
    void finish(bool success, bool cancelled, const QString &errorMessage);

    QNetworkAccessManager *m_manager = nullptr;
    QPointer<QNetworkReply> m_reply;
    QScopedPointer<QSaveFile> m_saveFile;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QTimer *m_inactivityTimer = nullptr;
    QString m_expectedSha256;
    QString m_destPath;
    qint64 m_bytesReceived = 0;
    bool m_cancelled = false;
    bool m_timedOut = false;
    bool m_sizeExceeded = false;
};

#endif // MODELDOWNLOADJOB_H
