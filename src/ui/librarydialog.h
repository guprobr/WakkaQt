#ifndef LIBRARYDIALOG_H
#define LIBRARYDIALOG_H

#include "sessionrepository.h"

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>

class LibraryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LibraryDialog(QWidget *parent = nullptr);

    // Returns the id of the session the user chose to restore,
    // or empty if they just closed / deleted things.
    QString selectedSessionId() const { return m_selectedId; }

signals:
    void sessionRestoreRequested(const QString &id);

private slots:
    void onRestoreClicked();
    void onDeleteClicked();
    void onRenameClicked();
    void onSelectionChanged();

private:
    void refreshList();
    void applyRowStyle(QListWidgetItem *item, const SessionEntry &entry);

    QListWidget   *m_list;
    QPushButton   *m_restoreBtn;
    QPushButton   *m_deleteBtn;
    QPushButton   *m_renameBtn;
    QPushButton   *m_closeBtn;
    QLabel        *m_detailLabel;

    SessionRepository m_repo;
    QString        m_selectedId;

    QList<SessionEntry> m_entries; // parallel list to m_list rows
};

#endif // LIBRARYDIALOG_H
