#include "youtubesearchdialog.h"

#include <QApplication>
#include <QEnterEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMouseEvent>
#include <QNetworkRequest>
#include <QScreen>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QUrl>
#include <algorithm>

// ── Constants ─────────────────────────────────────────────────────────────────
// Base sizes tuned for a standard 96 DPI display; scaled() below adapts them
// to the primary screen's actual DPI so cards/thumbnails stay a consistent
// physical size on high-DPI displays.
static constexpr int kCardW      = 186;
static constexpr int kCardH      = 172;
static constexpr int kThumbW     = 180;
static constexpr int kThumbH     = 101; // 16:9
static constexpr int kPageSize   = 8;

static qreal dpiScaleFactor()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    return screen ? screen->logicalDotsPerInch() / 96.0 : 1.0;
}

static int scaled(int px) { return qRound(px * dpiScaleFactor()); }
static QSize scaledSize(int w, int h) { return QSize(scaled(w), scaled(h)); }

// ── VideoCardWidget ───────────────────────────────────────────────────────────
VideoCardWidget::VideoCardWidget(const YtVideoInfo &info, QWidget *parent)
    : QFrame(parent), m_info(info)
{
    m_thumbSize = scaledSize(kThumbW, kThumbH);

    setFixedSize(scaledSize(kCardW, kCardH));
    setFrameShape(QFrame::StyledPanel);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Click to preview and select");
    setStyleSheet("VideoCardWidget { background: #1e1e2e; border: 1px solid #333355; border-radius: 6px; }"
                  "VideoCardWidget:hover { border: 1px solid #6666cc; }");

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(3, 3, 3, 4);
    vl->setSpacing(3);

    m_thumb = new QLabel(this);
    m_thumb->setFixedSize(m_thumbSize);
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setStyleSheet("background: #2a2a3e; border-radius: 4px;");
    m_thumb->setText("…");
    vl->addWidget(m_thumb);

    m_title = new QLabel(this);
    m_title->setWordWrap(true);
    m_title->setFixedWidth(m_thumbSize.width());
    m_title->setMaximumHeight(scaled(34));
    m_title->setStyleSheet("color: #ddddff; font-size: 11px; font-weight: bold;");
    // Two-line elided title
    const QFontMetrics fm(m_title->font());
    m_title->setText(fm.elidedText(info.title, Qt::ElideRight, m_thumbSize.width() * 2));
    vl->addWidget(m_title);

    m_channel = new QLabel(this);
    m_channel->setFixedWidth(m_thumbSize.width());
    m_channel->setStyleSheet("color: #8888aa; font-size: 10px;");
    m_channel->setText(info.channel.isEmpty() ? "Unknown" : info.channel);
    m_channel->setMaximumHeight(scaled(14));
    vl->addWidget(m_channel);

    m_duration = new QLabel(this);
    m_duration->setFixedWidth(m_thumbSize.width());
    m_duration->setStyleSheet("color: #6666aa; font-size: 10px;");
    if (info.durationSec > 0) {
        const int m = info.durationSec / 60, s = info.durationSec % 60;
        m_duration->setText(QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0')));
    }
    vl->addWidget(m_duration);
}

void VideoCardWidget::setThumbnail(const QPixmap &px)
{
    m_thumb->setPixmap(px.scaled(m_thumbSize.width(), m_thumbSize.height(),
                                  Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void VideoCardWidget::mousePressEvent(QMouseEvent *)   { emit cardClicked(this); }
void VideoCardWidget::enterEvent(QEnterEvent *)
{
    setStyleSheet("VideoCardWidget { background: #25253a; border: 1px solid #6666cc; border-radius: 6px; }");
}
void VideoCardWidget::leaveEvent(QEvent *)
{
    setStyleSheet("VideoCardWidget { background: #1e1e2e; border: 1px solid #333355; border-radius: 6px; }"
                  "VideoCardWidget:hover { border: 1px solid #6666cc; }");
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static QHBoxLayout *makeScrollRow()
{
    auto *l = new QHBoxLayout;
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(8);
    l->addStretch();
    return l;
}

static QScrollArea *wrapInScrollArea(QWidget *container, QWidget *parent)
{
    auto *sa = new QScrollArea(parent);
    sa->setWidget(container);
    sa->setWidgetResizable(false);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sa->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setFixedHeight(scaled(kCardH) + scaled(20));
    sa->setStyleSheet("QScrollArea { background: #12121e; border: 1px solid #222233; border-radius: 4px; }"
                      "QScrollBar:horizontal { height: 8px; background: #222233; border-radius: 4px; }"
                      "QScrollBar::handle:horizontal { background: #555577; border-radius: 4px; min-width: 30px; }");
    return sa;
}

// ── YoutubeSearchDialog ───────────────────────────────────────────────────────
YoutubeSearchDialog::YoutubeSearchDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Browse YouTube");
    setMinimumSize(860, 640);
    setStyleSheet("QDialog { background: #0f0f1a; color: #ccccee; }"
                  "QLabel { color: #ccccee; }"
                  "QLineEdit { background: #1a1a2e; color: #eeeeff; border: 1px solid #444466;"
                  "            border-radius: 4px; padding: 5px 8px; font-size: 13px; }"
                  "QPushButton { background: #3333aa; color: #eeeeff; border: none;"
                  "              border-radius: 4px; padding: 6px 16px; font-size: 12px; }"
                  "QPushButton:hover { background: #4444cc; }"
                  "QPushButton:disabled { background: #222244; color: #666688; }");

    m_nam = new QNetworkAccessManager(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // ── Search bar ────────────────────────────────────────────────────────────
    auto *searchRow = new QHBoxLayout;
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search for a song or artist…");
    m_searchEdit->setToolTip("Press Enter or click Search");
    m_searchButton = new QPushButton("Search", this);
    m_searchButton->setFixedWidth(80);
    m_searchButton->setToolTip("Search YouTube for karaoke and original videos");
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchButton);
    root->addLayout(searchRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #888899; font-size: 11px;");
    m_statusLabel->hide();
    root->addWidget(m_statusLabel);

    // ── Karaoke row ───────────────────────────────────────────────────────────
    m_karaokeLabel = new QLabel("🎤  Karaoke", this);
    m_karaokeLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #9999ee;");
    root->addWidget(m_karaokeLabel);

    auto *karaokeContainer = new QWidget;
    karaokeContainer->setMinimumHeight(scaled(kCardH) + scaled(8));
    m_karaokeLayout = makeScrollRow();
    karaokeContainer->setLayout(m_karaokeLayout);
    m_karaokeScroll = wrapInScrollArea(karaokeContainer, this);
    root->addWidget(m_karaokeScroll);

    // ── Originals row ─────────────────────────────────────────────────────────
    m_originalsLabel = new QLabel("🎵  Originals", this);
    m_originalsLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #9999ee;");
    root->addWidget(m_originalsLabel);

    auto *originalsContainer = new QWidget;
    originalsContainer->setMinimumHeight(scaled(kCardH) + scaled(8));
    m_originalsLayout = makeScrollRow();
    originalsContainer->setLayout(m_originalsLayout);
    m_originalsScroll = wrapInScrollArea(originalsContainer, this);
    root->addWidget(m_originalsScroll);

    // ── Preview panel ─────────────────────────────────────────────────────────
    m_preview = new QFrame(this);
    m_preview->setFrameShape(QFrame::StyledPanel);
    m_preview->setStyleSheet("QFrame { background: #1a1a2e; border: 1px solid #333355; border-radius: 6px; }");
    m_preview->hide();

    auto *previewLayout = new QHBoxLayout(m_preview);
    previewLayout->setContentsMargins(10, 10, 10, 10);
    previewLayout->setSpacing(14);

    m_previewThumb = new QLabel(m_preview);
    m_previewThumb->setFixedSize(scaledSize(240, 135));
    m_previewThumb->setStyleSheet("background: #2a2a3e; border-radius: 4px;");
    m_previewThumb->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_previewThumb);

    auto *infoCol = new QVBoxLayout;
    infoCol->setSpacing(4);

    m_previewTitle = new QLabel(m_preview);
    m_previewTitle->setWordWrap(true);
    m_previewTitle->setStyleSheet("color: #eeeeff; font-size: 14px; font-weight: bold;");
    infoCol->addWidget(m_previewTitle);

    m_previewChannel = new QLabel(m_preview);
    m_previewChannel->setStyleSheet("color: #8888bb; font-size: 12px;");
    infoCol->addWidget(m_previewChannel);

    m_previewDuration = new QLabel(m_preview);
    m_previewDuration->setStyleSheet("color: #6666aa; font-size: 11px;");
    infoCol->addWidget(m_previewDuration);

    infoCol->addStretch();

    m_downloadBtn = new QPushButton("⬇  Download this video", m_preview);
    m_downloadBtn->setToolTip("Download and load this video as your karaoke playback");
    m_downloadBtn->setStyleSheet("QPushButton { background: #225522; color: #aaffaa; border: 1px solid #44aa44;"
                                 "              border-radius: 4px; padding: 8px 18px; font-size: 12px; }"
                                 "QPushButton:hover { background: #336633; }");
    infoCol->addWidget(m_downloadBtn, 0, Qt::AlignLeft);

    previewLayout->addLayout(infoCol, 1);
    root->addWidget(m_preview);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_searchButton, &QPushButton::clicked, this, &YoutubeSearchDialog::onSearch);
    connect(m_searchEdit,   &QLineEdit::returnPressed, this, &YoutubeSearchDialog::onSearch);
    connect(m_downloadBtn,  &QPushButton::clicked, this, &YoutubeSearchDialog::onDownload);
}

YoutubeSearchDialog::~YoutubeSearchDialog()
{
    if (m_karaokeProc)      { m_karaokeProc->kill();      m_karaokeProc->deleteLater(); }
    if (m_originalsProc)    { m_originalsProc->kill();    m_originalsProc->deleteLater(); }
    if (m_karaokeMoreProc)  { m_karaokeMoreProc->kill();  m_karaokeMoreProc->deleteLater(); }
    if (m_originalsMoreProc){ m_originalsMoreProc->kill(); m_originalsMoreProc->deleteLater(); }
}

// ── Private helpers ───────────────────────────────────────────────────────────
QString YoutubeSearchDialog::formatDuration(int secs)
{
    if (secs <= 0) return {};
    const int h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

static void clearLayout(QHBoxLayout *l)
{
    while (l->count() > 1) {           // keep the trailing stretch
        QLayoutItem *item = l->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void YoutubeSearchDialog::setPreviewVisible(bool v) { m_preview->setVisible(v); }

void YoutubeSearchDialog::fetchThumbnail(VideoCardWidget *card)
{
    const QString thumbUrl =
        QString("https://img.youtube.com/vi/%1/mqdefault.jpg").arg(card->info().id);

    QNetworkReply *reply = m_nam->get(QNetworkRequest(QUrl(thumbUrl)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, card]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap px;
        if (px.loadFromData(reply->readAll()) && !px.isNull())
            card->setThumbnail(px);
    });
}

// ── Search ────────────────────────────────────────────────────────────────────
void YoutubeSearchDialog::onSearch()
{
    const QString q = m_searchEdit->text().trimmed();
    if (q.isEmpty()) return;

    // Clear previous results
    clearLayout(m_karaokeLayout);
    m_karaokeMoreBtn   = nullptr; // deleted by clearLayout
    clearLayout(m_originalsLayout);
    m_originalsMoreBtn = nullptr;
    setPreviewVisible(false);

    m_currentQuery    = q;
    m_karaokeOffset   = 0;
    m_originalsOffset = 0;

    if (m_karaokeProc)      { m_karaokeProc->kill();      m_karaokeProc->deleteLater();      m_karaokeProc      = nullptr; }
    if (m_originalsProc)    { m_originalsProc->kill();    m_originalsProc->deleteLater();    m_originalsProc    = nullptr; }
    if (m_karaokeMoreProc)  { m_karaokeMoreProc->kill();  m_karaokeMoreProc->deleteLater();  m_karaokeMoreProc  = nullptr; }
    if (m_originalsMoreProc){ m_originalsMoreProc->kill(); m_originalsMoreProc->deleteLater(); m_originalsMoreProc = nullptr; }

    m_searchButton->setEnabled(false);
    m_pendingSearches = 2;
    m_statusLabel->setText("Searching…");
    m_statusLabel->show();
    m_karaokeLabel->setText("🎤  Karaoke");
    m_originalsLabel->setText("🎵  Originals");

    const QStringList baseArgs = {"--flat-playlist", "--dump-json", "--no-download",
                                  "--no-warnings", "--quiet"};

    m_karaokeProc = new QProcess(this);
    m_karaokeProc->start("yt-dlp", QStringList() << QString("ytsearch%1:%2 karaoke").arg(kPageSize).arg(q) << baseArgs);
    connect(m_karaokeProc, &QProcess::finished, this, &YoutubeSearchDialog::onKaraokeDataReady);

    m_originalsProc = new QProcess(this);
    m_originalsProc->start("yt-dlp", QStringList() << QString("ytsearch%1:%2").arg(kPageSize).arg(q) << baseArgs);
    connect(m_originalsProc, &QProcess::finished, this, &YoutubeSearchDialog::onOriginalsDataReady);
}

// ── Result parsing ────────────────────────────────────────────────────────────
void YoutubeSearchDialog::parseResults(const QByteArray &data, QHBoxLayout *layout, bool isKaraoke)
{
    // Remove existing "Load more" button before appending new cards
    QPushButton *&moreBtn = isKaraoke ? m_karaokeMoreBtn : m_originalsMoreBtn;
    if (moreBtn) {
        layout->removeWidget(moreBtn);
        moreBtn->deleteLater();
        moreBtn = nullptr;
    }

    int &offset   = isKaraoke ? m_karaokeOffset : m_originalsOffset;
    int  newCards = 0;

    for (const QByteArray &line : data.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
        if (!doc.isObject()) continue;
        const QJsonObject obj = doc.object();

        YtVideoInfo info;
        info.id      = obj.value("id").toString();
        info.title   = obj.value("title").toString();
        info.channel = obj.value("channel").toString();
        if (info.channel.isEmpty())
            info.channel = obj.value("uploader").toString();
        info.durationSec = (int)obj.value("duration").toDouble(0);
        info.url  = obj.value("url").toString();
        if (info.url.isEmpty() && !info.id.isEmpty())
            info.url = "https://www.youtube.com/watch?v=" + info.id;
        if (info.id.isEmpty() || info.title.isEmpty()) continue;

        auto *card = new VideoCardWidget(info, nullptr);
        layout->insertWidget(layout->count() - 1, card); // before trailing stretch
        connect(card, &VideoCardWidget::cardClicked, this, &YoutubeSearchDialog::onCardClicked);
        fetchThumbnail(card);
        ++newCards;
    }

    offset += newCards;

    if (isKaraoke)
        m_karaokeLabel->setText(QString("🎤  Karaoke  (%1)").arg(offset));
    else
        m_originalsLabel->setText(QString("🎵  Originals  (%1)").arg(offset));

    QWidget *container = isKaraoke
        ? m_karaokeScroll->widget()
        : m_originalsScroll->widget();
    // +1 slot reserved for the "Load more" button
    container->setMinimumWidth(std::max(1, offset + 1) * (scaled(kCardW) + scaled(8)) + scaled(16));
    container->adjustSize();

    if (newCards > 0) {
        moreBtn = makeMoreButton(isKaraoke);
        layout->insertWidget(layout->count() - 1, moreBtn); // before stretch
    }
}

QPushButton *YoutubeSearchDialog::makeMoreButton(bool isKaraoke)
{
    auto *btn = new QPushButton("Load more  ▶", nullptr);
    btn->setFixedSize(scaledSize(kCardW, kCardH));
    btn->setToolTip("Fetch more results");
    btn->setStyleSheet(
        "QPushButton { background: #1a1a3a; color: #8888cc; border: 1px dashed #444466;"
        "              border-radius: 6px; font-size: 12px; }"
        "QPushButton:hover { background: #222255; color: #aaaaee; border-color: #6666cc; }"
        "QPushButton:disabled { color: #444466; border-color: #222244; }");
    connect(btn, &QPushButton::clicked, this, [this, isKaraoke]() { loadMore(isKaraoke); });
    return btn;
}

void YoutubeSearchDialog::loadMore(bool isKaraoke)
{
    QProcess   *&moreProc = isKaraoke ? m_karaokeMoreProc : m_originalsMoreProc;
    QPushButton *&moreBtn = isKaraoke ? m_karaokeMoreBtn  : m_originalsMoreBtn;

    if (moreProc) return; // already loading

    moreBtn->setEnabled(false);
    moreBtn->setText("Loading…");

    const int offset = isKaraoke ? m_karaokeOffset : m_originalsOffset;
    const int end    = offset + kPageSize;

    const QString searchArg = isKaraoke
        ? QString("ytsearch%1:%2 karaoke").arg(end).arg(m_currentQuery)
        : QString("ytsearch%1:%2").arg(end).arg(m_currentQuery);

    const QStringList args = {
        searchArg,
        "--flat-playlist", "--dump-json", "--no-download", "--no-warnings", "--quiet",
        "--playlist-start", QString::number(offset + 1),
        "--playlist-end",   QString::number(end)
    };

    moreProc = new QProcess(this);
    moreProc->start("yt-dlp", args);

    if (isKaraoke)
        connect(moreProc, &QProcess::finished, this, &YoutubeSearchDialog::onKaraokeMoreReady);
    else
        connect(moreProc, &QProcess::finished, this, &YoutubeSearchDialog::onOriginalsMoreReady);
}

void YoutubeSearchDialog::onKaraokeDataReady()
{
    parseResults(m_karaokeProc->readAllStandardOutput(), m_karaokeLayout, true);
    if (--m_pendingSearches == 0) {
        m_statusLabel->hide();
        m_searchButton->setEnabled(true);
    }
}

void YoutubeSearchDialog::onOriginalsDataReady()
{
    parseResults(m_originalsProc->readAllStandardOutput(), m_originalsLayout, false);
    if (--m_pendingSearches == 0) {
        m_statusLabel->hide();
        m_searchButton->setEnabled(true);
    }
}

void YoutubeSearchDialog::onKaraokeMoreReady()
{
    parseResults(m_karaokeMoreProc->readAllStandardOutput(), m_karaokeLayout, true);
    m_karaokeMoreProc->deleteLater();
    m_karaokeMoreProc = nullptr;
}

void YoutubeSearchDialog::onOriginalsMoreReady()
{
    parseResults(m_originalsMoreProc->readAllStandardOutput(), m_originalsLayout, false);
    m_originalsMoreProc->deleteLater();
    m_originalsMoreProc = nullptr;
}

// ── Card selection & preview ──────────────────────────────────────────────────
void YoutubeSearchDialog::onCardClicked(VideoCardWidget *card)
{
    m_selectedInfo = card->info();

    m_previewTitle->setText(m_selectedInfo.title);
    m_previewChannel->setText(m_selectedInfo.channel.isEmpty()
                               ? "Unknown channel" : m_selectedInfo.channel);
    m_previewDuration->setText(formatDuration(m_selectedInfo.durationSec));

    // Fetch preview thumbnail at higher resolution
    m_previewThumb->clear();
    m_previewThumb->setText("…");
    const QString hqUrl =
        QString("https://img.youtube.com/vi/%1/hqdefault.jpg").arg(m_selectedInfo.id);
    QNetworkReply *reply = m_nam->get(QNetworkRequest(QUrl(hqUrl)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap px;
        if (px.loadFromData(reply->readAll()) && !px.isNull())
            m_previewThumb->setPixmap(
                px.scaled(scaledSize(240, 135), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });

    setPreviewVisible(true);
}

void YoutubeSearchDialog::onDownload()
{
    if (m_selectedInfo.url.isEmpty()) return;
    emit downloadRequested(m_selectedInfo.url, m_selectedInfo.title);
    accept();
}
