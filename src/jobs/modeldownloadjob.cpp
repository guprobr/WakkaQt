#include "modeldownloadjob.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>

namespace {
// Generously above the real model's ~80 MB — just a backstop against a
// redirected/malformed response streaming an unbounded amount of data onto
// disk, not a realistic expected size.
constexpr qint64 kMaxDownloadBytes = 300LL * 1024 * 1024;
// No new bytes at all for this long means the connection has effectively
// stalled — abort rather than let a stuck download hang indefinitely with
// no way for the user to tell it apart from "just slow".
constexpr int kInactivityTimeoutMs = 30000;
}

ModelDownloadJob::ModelDownloadJob(QObject *parent) : QObject(parent) {}

ModelDownloadJob::~ModelDownloadJob()
{
    cancel();
    waitForFinished();
}

bool ModelDownloadJob::isActive() const
{
    return m_reply != nullptr;
}

void ModelDownloadJob::cancel()
{
    if (m_reply) {
        m_cancelled = true;
        m_reply->abort(); // triggers the reply's finished signal asynchronously
    }
}

void ModelDownloadJob::waitForFinished()
{
    if (!isActive())
        return;
    QEventLoop loop;
    connect(this, &ModelDownloadJob::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

void ModelDownloadJob::resetInactivityTimer()
{
    if (m_inactivityTimer)
        m_inactivityTimer->start(kInactivityTimeoutMs);
}

void ModelDownloadJob::start(const QString &urlStr, const QString &destPath,
                              const QString &expectedSha256)
{
    if (isActive())
        return;

    m_destPath = destPath;
    m_expectedSha256 = expectedSha256.toLower();
    m_bytesReceived = 0;
    m_cancelled = false;
    m_timedOut = false;
    m_sizeExceeded = false;
    m_hash.reset();

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    // Writes to a temp sibling of destPath and only replaces it on commit()
    // — see finish() — so a failed/cancelled/corrupt download never
    // touches an existing file at destPath.
    m_saveFile.reset(new QSaveFile(destPath));
    if (!m_saveFile->open(QIODevice::WriteOnly)) {
        const QString err = "Cannot write to: " + destPath;
        m_saveFile.reset();
        emit finished(false, false, err);
        return;
    }

    QUrl url(urlStr);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    if (!m_manager)
        m_manager = new QNetworkAccessManager(this);

    if (!m_inactivityTimer) {
        m_inactivityTimer = new QTimer(this);
        m_inactivityTimer->setSingleShot(true);
        connect(m_inactivityTimer, &QTimer::timeout, this, [this]() {
            if (m_reply) {
                m_timedOut = true;
                m_reply->abort(); // finish() below reports this as a timeout, not a cancel
            }
        });
    }

    QNetworkReply *reply = m_manager->get(req);
    m_reply = reply;
    resetInactivityTimer();

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty())
            return;
        resetInactivityTimer();

        m_bytesReceived += chunk.size();
        if (m_bytesReceived > kMaxDownloadBytes) {
            m_sizeExceeded = true;
            reply->abort(); // finish() reports this as a size-limit error
            return;
        }

        m_hash.addData(chunk);
        if (m_saveFile)
            m_saveFile->write(chunk);
    });

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        emit progress(total > 0 ? int(received * 100 / total) : -1);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QNetworkReply::NetworkError err = reply->error();
        const QString errStr = reply->errorString();
        const bool wasCancelled = m_cancelled;
        const bool timedOut = m_timedOut;
        const bool sizeExceeded = m_sizeExceeded;
        reply->deleteLater();
        m_reply = nullptr;
        if (m_inactivityTimer)
            m_inactivityTimer->stop();

        if (err != QNetworkReply::NoError) {
            if (m_saveFile) { m_saveFile->cancelWriting(); m_saveFile.reset(); }
            if (sizeExceeded) {
                finish(false, false, "Download exceeded the maximum expected model size — aborted.");
            } else if (timedOut) {
                finish(false, false, "Download stalled (no data received for "
                       + QString::number(kInactivityTimeoutMs / 1000) + "s) — aborted.");
            } else if (wasCancelled) {
                finish(false, true, QString());
            } else {
                finish(false, false, "Download failed: " + errStr);
            }
            return;
        }

        const QString actualHash = QString::fromLatin1(m_hash.result().toHex());
        if (actualHash != m_expectedSha256) {
            if (m_saveFile) { m_saveFile->cancelWriting(); m_saveFile.reset(); }
            finish(false, false, QString("Downloaded model failed the integrity check "
                                        "(expected sha256 %1, got %2). Discarding — try again.")
                                      .arg(m_expectedSha256, actualHash));
            return;
        }

        if (!m_saveFile || !m_saveFile->commit()) {
            const QString err = "Failed to finalize model file at: " + m_destPath;
            m_saveFile.reset();
            finish(false, false, err);
            return;
        }
        m_saveFile.reset();
        finish(true, false, QString());
    });
}

void ModelDownloadJob::finish(bool success, bool cancelled, const QString &errorMessage)
{
    if (success)
        emit progress(100);
    emit finished(success, cancelled, errorMessage);
}
