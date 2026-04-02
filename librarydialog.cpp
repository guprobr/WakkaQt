#include "librarydialog.h"

#include <QApplication>
#include <QFileInfo>
#include <QDebug>

LibraryDialog::LibraryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("WakkaQt \xe2\x80\x94 Session Library");
    setMinimumSize(660, 440);
    resize(740, 520);

    // ── Header ────────────────────────────────────────────────────────────
    QLabel *headerLabel = new QLabel(
        "<b>Session Library</b><br>"
        "<small>Every recording is saved automatically. "
        "Select a session and press <b>Resume Session</b> to re-adjust and re-render.</small>",
        this);
    headerLabel->setWordWrap(true);
    headerLabel->setAlignment(Qt::AlignCenter);
    headerLabel->setContentsMargins(0, 4, 0, 8);

    // ── Session list ──────────────────────────────────────────────────────
    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFont(QApplication::font());

    // ── Detail panel ─────────────────────────────────────────────────────
    m_detailLabel = new QLabel("Select a session to see details.", this);
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_detailLabel->setMinimumHeight(72);
    m_detailLabel->setTextFormat(Qt::RichText);
    m_detailLabel->setStyleSheet("padding: 6px; border-top: 1px solid palette(mid);");

    // ── Buttons ───────────────────────────────────────────────────────────
    m_restoreBtn = new QPushButton("\xe2\x96\xb6  Resume Session", this);
    m_restoreBtn->setToolTip("Restore this session and go to volume/offset adjustment and rendering");
    m_restoreBtn->setEnabled(false);

    m_renameBtn = new QPushButton("\xe2\x9c\x8e  Rename", this);
    m_renameBtn->setToolTip("Give this session a custom label");
    m_renameBtn->setEnabled(false);

    m_deleteBtn = new QPushButton("\xe2\x9c\x95  Delete", this);
    m_deleteBtn->setToolTip("Permanently remove this session from the library");
    m_deleteBtn->setEnabled(false);

    m_closeBtn = new QPushButton("Close", this);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_restoreBtn);
    btnRow->addWidget(m_renameBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_closeBtn);

    // ── Main layout ───────────────────────────────────────────────────────
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(headerLabel);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_detailLabel);
    layout->addSpacing(6);
    layout->addLayout(btnRow);
    setLayout(layout);

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_list,       &QListWidget::itemSelectionChanged,
            this,         &LibraryDialog::onSelectionChanged);
    connect(m_list,       &QListWidget::itemDoubleClicked,
            this,         &LibraryDialog::onRestoreClicked);
    connect(m_restoreBtn, &QPushButton::clicked,
            this,         &LibraryDialog::onRestoreClicked);
    connect(m_deleteBtn,  &QPushButton::clicked,
            this,         &LibraryDialog::onDeleteClicked);
    connect(m_renameBtn,  &QPushButton::clicked,
            this,         &LibraryDialog::onRenameClicked);
    connect(m_closeBtn,   &QPushButton::clicked,
            this,         &QDialog::reject);

    refreshList();
}

// ── refreshList ───────────────────────────────────────────────────────────────
void LibraryDialog::refreshList()
{
    m_list->blockSignals(true);
    m_list->clear();
    m_entries = m_mgr.loadAll();
    m_list->blockSignals(false);

    if (m_entries.isEmpty()) {
        QListWidgetItem *empty = new QListWidgetItem(
            "  (No sessions saved yet — sing something first!)");
        empty->setFlags(Qt::NoItemFlags);   // not selectable
        m_list->addItem(empty);
        m_detailLabel->setText(
            "Sessions are saved automatically every time you reach the render step.<br>"
            "They will appear here, ready to be restored at any time.");
        m_restoreBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
        m_renameBtn->setEnabled(false);
        return;
    }

    for (const SessionEntry &e : m_entries) {
        const QString line = QString("[%1]  %2")
            .arg(e.savedAt.toString("yyyy-MM-dd  hh:mm"))
            .arg(e.label);
        QListWidgetItem *item = new QListWidgetItem(line);
        item->setData(Qt::UserRole, e.id);
        applyRowStyle(item, e);
        m_list->addItem(item);
    }

    // Auto-select first row for convenience
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

// ── applyRowStyle ─────────────────────────────────────────────────────────────
void LibraryDialog::applyRowStyle(QListWidgetItem *item, const SessionEntry &entry)
{
    const bool incomplete = !entry.hasAudio || !entry.hasWebcam;
    if (incomplete) {
        item->setForeground(QColor(180, 100, 90));
        item->setToolTip("\xe2\x9a\xa0 Some recording files are missing for this session.");
    } else {
        item->setToolTip("Double-click or press \xe2\x80\x98Resume Session\xe2\x80\x99 to restore.");
    }
}

// ── selectedId ────────────────────────────────────────────────────────────────
// Helper: returns the library id of the currently selected row, or empty.
static QString selectedId(QListWidget *list)
{
    const QList<QListWidgetItem*> sel = list->selectedItems();
    if (sel.isEmpty()) return {};
    if (!(sel.first()->flags() & Qt::ItemIsSelectable)) return {};
    return sel.first()->data(Qt::UserRole).toString();
}

// ── onSelectionChanged ────────────────────────────────────────────────────────
void LibraryDialog::onSelectionChanged()
{
    const QString id = ::selectedId(m_list);
    const bool have = !id.isEmpty();

    m_restoreBtn->setEnabled(have);
    m_deleteBtn->setEnabled(have);
    m_renameBtn->setEnabled(have);

    if (!have) {
        m_detailLabel->setText("Select a session to see details.");
        return;
    }

    for (const SessionEntry &e : m_entries) {
        if (e.id != id) continue;

        const QString playbackInfo = e.playbackFile.isEmpty()
            ? QString("(original file path not recorded)")
            : QFileInfo(e.playbackFile).fileName();

        const QString filesStr =
              (e.hasAudio && e.hasWebcam) ? "\xe2\x9c\x94 Vocal + Webcam"
            : e.hasAudio                  ? "\xe2\x9c\x94 Vocal only (webcam missing)"
            : e.hasWebcam                 ? "\xe2\x9c\x94 Webcam only (vocal missing)"
            :                              "\xe2\x9a\xa0 Missing recording files";

        m_detailLabel->setText(
            QString("<b>Song / Media:</b> %1<br>"
                    "<b>Saved:</b> %2<br>"
                    "<b>Files:</b> %3 &nbsp;&nbsp; "
                    "<span style='color: gray; font-size: 10px;'>ID: %4</span>")
                .arg(e.playbackName.isEmpty() ? playbackInfo : e.playbackName)
                .arg(e.savedAt.toString("dddd, d MMMM yyyy  \xe2\x80\x93  hh:mm:ss"))
                .arg(filesStr)
                .arg(e.id)
        );
        break;
    }
}

// ── onRestoreClicked ──────────────────────────────────────────────────────────
void LibraryDialog::onRestoreClicked()
{
    const QString id = ::selectedId(m_list);
    if (id.isEmpty()) return;

    m_selectedId = id;
    emit sessionRestoreRequested(id);
    accept();
}

// ── onDeleteClicked ───────────────────────────────────────────────────────────
void LibraryDialog::onDeleteClicked()
{
    const QString id = ::selectedId(m_list);
    if (id.isEmpty()) return;

    QString label;
    for (const SessionEntry &e : m_entries)
        if (e.id == id) { label = e.label; break; }

    const int ret = QMessageBox::question(
        this,
        "Delete Session",
        QString("Permanently delete this session?\n\n\"%1\"\n\nThis cannot be undone.")
            .arg(label),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        m_mgr.deleteSession(id);
        m_detailLabel->setText("Session deleted.");
        refreshList();
    }
}

// ── onRenameClicked ───────────────────────────────────────────────────────────
void LibraryDialog::onRenameClicked()
{
    const QString id = ::selectedId(m_list);
    if (id.isEmpty()) return;

    QString currentLabel;
    for (const SessionEntry &e : m_entries)
        if (e.id == id) { currentLabel = e.label; break; }

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this,
        "Rename Session",
        "New session name:",
        QLineEdit::Normal,
        currentLabel,
        &ok);

    if (ok && !newName.trimmed().isEmpty()) {
        m_mgr.renameSession(id, newName.trimmed());
        const QString savedId = id;   // refreshList clears the list
        refreshList();
        // Re-select the same entry
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == savedId) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
}
