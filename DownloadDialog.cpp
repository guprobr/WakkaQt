#include "DownloadDialog.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>

DownloadDialog::DownloadDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Download YouTube Video");
    setModal(true); // bloqueia a janela principal
    setMinimumWidth(420);

    m_titleLabel = new QLabel("Preparing download…");
    m_statusLabel = new QLabel("Getting file name…");
    m_progress = new QProgressBar;
    m_progress->setRange(0, 0); // indeterminado até começar o download

    m_cancelBtn = new QPushButton("Cancel");
    connect(m_cancelBtn, &QPushButton::clicked, this, &DownloadDialog::onCancel);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_cancelBtn);

    auto* lay = new QVBoxLayout;
    lay->addWidget(m_titleLabel);
    lay->addWidget(m_statusLabel);
    lay->addWidget(m_progress);
    lay->addLayout(btnRow);
    setLayout(lay);
}

void DownloadDialog::start(const QString& url, const QString& directory) {
    m_url = url;
    m_directory = directory;
    startFilenameProbe();
}

void DownloadDialog::startFilenameProbe() {
    if (m_filenameProc) {
        m_filenameProc->deleteLater();
        m_filenameProc = nullptr;
    }
    m_filenameProc = new QProcess(this);

    // Template com título; saída é um caminho já resolvido pelo yt-dlp
    const QString outputTemplate = m_directory + "/%(title)s.%(ext)s";
    QStringList args;
    args << "--print" << "filename"
         << "--output" << outputTemplate
         << "--no-playlist"          // força single video
         << m_url;

    connect(m_filenameProc, &QProcess::readyReadStandardOutput,
            this, &DownloadDialog::onFilenameStdOut);
    connect(m_filenameProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DownloadDialog::onFilenameFinished);

    m_filenameProc->start("yt-dlp", args);
}

void DownloadDialog::onFilenameStdOut() {
    const QString output = QString::fromUtf8(m_filenameProc->readAllStandardOutput()).trimmed();
    if (output.isEmpty()) return;

    QFileInfo fi(output);
    QString baseName = fi.completeBaseName();
    const QString ext = fi.suffix();

    // sanitize only the baseName (como você já fazia)
    QRegularExpression regex("[^a-zA-Z0-9_\\-\\.\\ ]");
    baseName.replace(regex, "_");

    m_predictedPath = m_directory + "/" + baseName + "." + ext;
    qDebug() << "Predicted filename:" << m_predictedPath;
    m_statusLabel->setText("File: " + baseName + "." + ext);
}

void DownloadDialog::onFilenameFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    m_filenameProc->deleteLater();
    m_filenameProc = nullptr;

    if (exitCode != 0 || m_predictedPath.isEmpty()) {
        m_statusLabel->setText("Failed to get filename.");
        emit downloadFailed("Failed to get filename.");
        reject();
        return;
    }

    // reset barra para modo determinado agora
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_titleLabel->setText("Downloading…");
    m_statusLabel->setText("Starting download…");

    startDownload();
}

void DownloadDialog::startDownload() {
    if (m_downloadProc) {
        m_downloadProc->deleteLater();
        m_downloadProc = nullptr;
    }
    m_downloadProc = new QProcess(this);

    QStringList args;
    args << "--output" << m_predictedPath
         << "--no-playlist"    // garante single video mesmo que link tenha params
         << "--newline"        // progresso linha a linha no stderr
         << m_url;

    connect(m_downloadProc, &QProcess::readyReadStandardError,
            this, &DownloadDialog::onDownloadStdErr);
    connect(m_downloadProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DownloadDialog::onDownloadFinished);

    m_downloadProc->start("yt-dlp", args);
}

void DownloadDialog::onDownloadStdErr() {
    const QString chunk = QString::fromUtf8(m_downloadProc->readAllStandardError());

    // Exemplos de linhas do yt-dlp:
    // [download]   4.3% of 10.15MiB at 1.10MiB/s ETA 00:09
    // Vamos capturar o "X.Y%" mais à direita
    static QRegularExpression re(R"((\d{1,3}(?:\.\d+)?)%)");
    QRegularExpressionMatchIterator it = re.globalMatch(chunk);
    double lastPercent = -1.0;
    while (it.hasNext()) {
        auto m = it.next();
        lastPercent = m.captured(1).toDouble();
    }
    if (lastPercent >= 0.0) {
        m_progress->setValue(static_cast<int>(lastPercent + 0.5));
        m_statusLabel->setText(QString("Downloading… %1%").arg(lastPercent, 0, 'f', 1));
    }

    // Mostra último stderr como status (opcional; útil para logs curtos)
    // m_statusLabel->setText(chunk.split('\n').last().trimmed());
}

void DownloadDialog::onDownloadFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    m_downloadProc->deleteLater();
    m_downloadProc = nullptr;

    if (exitCode == 0) {
        m_progress->setValue(100);
        m_statusLabel->setText("Download complete.");
        m_downloadedPath = m_predictedPath;
        emit downloadSucceeded(m_downloadedPath);
        accept();
    } else {
        m_statusLabel->setText("Download failed.");
        emit downloadFailed("Download failed.");
        reject();
    }
}

void DownloadDialog::onCancel() {
    if (m_filenameProc) {
        m_filenameProc->kill();
    }
    if (m_downloadProc) {
        m_downloadProc->kill();
    }
    m_statusLabel->setText("Canceled by user.");
    reject();
}
