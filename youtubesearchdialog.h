#pragma once
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>

struct YtVideoInfo {
    QString id;
    QString title;
    QString channel;
    QString url;
    int     durationSec = 0;
};

// ── Card widget ───────────────────────────────────────────────────────────────
class VideoCardWidget : public QFrame {
    Q_OBJECT
public:
    explicit VideoCardWidget(const YtVideoInfo &info, QWidget *parent = nullptr);

    const YtVideoInfo &info() const { return m_info; }
    void setThumbnail(const QPixmap &px);

signals:
    void cardClicked(VideoCardWidget *card);

protected:
    void mousePressEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    YtVideoInfo m_info;
    QLabel     *m_thumb    = nullptr;
    QLabel     *m_title    = nullptr;
    QLabel     *m_channel  = nullptr;
    QLabel     *m_duration = nullptr;
};

// ── Search dialog ─────────────────────────────────────────────────────────────
class YoutubeSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit YoutubeSearchDialog(QWidget *parent = nullptr);
    ~YoutubeSearchDialog() override;

signals:
    void downloadRequested(const QString &url, const QString &title);

private slots:
    void onSearch();
    void onKaraokeDataReady();
    void onOriginalsDataReady();
    void onCardClicked(VideoCardWidget *card);
    void onDownload();

private:
    void parseResults(const QByteArray &data, QHBoxLayout *layout, bool isKaraoke);
    void fetchThumbnail(VideoCardWidget *card);
    void setPreviewVisible(bool visible);
    static QString formatDuration(int secs);

    // Top bar
    QLineEdit   *m_searchEdit   = nullptr;
    QPushButton *m_searchButton = nullptr;
    QLabel      *m_statusLabel  = nullptr;

    // Rows
    QLabel      *m_karaokeLabel    = nullptr;
    QScrollArea *m_karaokeScroll   = nullptr;
    QHBoxLayout *m_karaokeLayout   = nullptr;
    QLabel      *m_originalsLabel  = nullptr;
    QScrollArea *m_originalsScroll = nullptr;
    QHBoxLayout *m_originalsLayout = nullptr;

    // Preview panel
    QFrame      *m_preview         = nullptr;
    QLabel      *m_previewThumb    = nullptr;
    QLabel      *m_previewTitle    = nullptr;
    QLabel      *m_previewChannel  = nullptr;
    QLabel      *m_previewDuration = nullptr;
    QPushButton *m_downloadBtn     = nullptr;

    // State
    QProcess   *m_karaokeProc   = nullptr;
    QProcess   *m_originalsProc = nullptr;
    QNetworkAccessManager *m_nam = nullptr;

    YtVideoInfo m_selectedInfo;
    int m_pendingSearches = 0;
};
