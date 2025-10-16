#pragma once
#include <QDialog>
#include <QProcess>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "complexes.h"

class DownloadDialog : public QDialog {
    Q_OBJECT
public:
    explicit DownloadDialog(QWidget* parent = nullptr);

    // Start full flow: predict filename, then download
    void start(const QString& url, const QString& directory);

    QString downloadedFilePath() const { return m_downloadedPath; }
signals:
    void downloadSucceeded(const QString& path);
    void downloadFailed(const QString& reason);

private slots:
    void onFilenameStdOut();
    void onFilenameFinished(int exitCode, QProcess::ExitStatus status);

    void onDownloadStdErr();
    void onDownloadFinished(int exitCode, QProcess::ExitStatus status);
    void onCancel();

private:
    void startFilenameProbe();
    void startDownload();

    // UI
    QLabel* m_titleLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    // Processes
    QProcess* m_filenameProc = nullptr;
    QProcess* m_downloadProc = nullptr;

    // Data
    QString m_url;
    QString m_directory;
    QString m_predictedPath;
    QString m_downloadedPath;
};

