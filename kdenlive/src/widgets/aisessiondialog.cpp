/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#include "aisessiondialog.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include <QDateTime>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <KLocalizedString>

AISessionDialog::AISessionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("AI Editing Conversation Sessions"));
    setMinimumSize(680, 480);
    resize(740, 520);
    setupUi();
    populateList();
}

void AISessionDialog::setupUi()
{
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #1e1e1e; color: #cccccc; }"
        "QLabel { color: #cccccc; }"
        "QLineEdit { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 3px; padding: 5px 8px; color: #ffffff; }"
        "QLineEdit:focus { border-color: #007acc; }"
        "QComboBox { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 3px; padding: 4px 8px; color: #cccccc; }"
        "QComboBox QAbstractItemView { background-color: #252526; border: 1px solid #3c3c3c; color: #cccccc; selection-background-color: #0e639c; }"
        "QListWidget { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px; outline: none; }"
        "QListWidget::item { border-bottom: 1px solid #2d2d2d; padding: 8px 10px; border-radius: 3px; }"
        "QListWidget::item:hover { background-color: #2a2d2e; }"
        "QListWidget::item:selected { background-color: #094771; color: #ffffff; }"
    ));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // Top Filter Bar
    auto *topFilterLayout = new QHBoxLayout();
    topFilterLayout->setSpacing(8);

    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(i18n("🔍 Search conversation history..."));
    m_searchInput->setClearButtonEnabled(true);
    connect(m_searchInput, &QLineEdit::textChanged, this, &AISessionDialog::slotFilterChanged);

    m_projectFilterCombo = new QComboBox(this);
    m_projectFilterCombo->addItem(i18n("All Projects"), QStringLiteral("all"));

    QString currentProj;
    if (pCore && pCore->currentDoc()) {
        currentProj = pCore->currentDoc()->url().toLocalFile();
        if (!currentProj.isEmpty()) {
            QFileInfo fi(currentProj);
            m_projectFilterCombo->addItem(i18n("Current Project: %1", fi.fileName()), currentProj);
            m_projectFilterCombo->setCurrentIndex(1);
        }
    }
    connect(m_projectFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AISessionDialog::slotFilterChanged);

    topFilterLayout->addWidget(m_searchInput, 2);
    topFilterLayout->addWidget(m_projectFilterCombo, 1);
    mainLayout->addLayout(topFilterLayout);

    // Main Content (List + Right Side Actions)
    auto *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(10);

    m_sessionListWidget = new QListWidget(this);
    connect(m_sessionListWidget, &QListWidget::itemSelectionChanged, this, &AISessionDialog::slotSessionSelected);
    connect(m_sessionListWidget, &QListWidget::itemDoubleClicked, this, &AISessionDialog::slotResumeClicked);
    contentLayout->addWidget(m_sessionListWidget, 1);

    // Action Buttons
    auto *actionsLayout = new QVBoxLayout();
    actionsLayout->setSpacing(8);

    const QString primaryBtnStyle = QStringLiteral(
        "QPushButton { background-color: #0e639c; border: 1px solid #1177bb; border-radius: 3px; padding: 6px 14px; color: #ffffff; font-weight: 600; }"
        "QPushButton:hover { background-color: #1177bb; }"
        "QPushButton:disabled { background-color: #333333; border-color: #444444; color: #777777; }"
    );

    const QString secondaryBtnStyle = QStringLiteral(
        "QPushButton { background-color: #2d2d2d; border: 1px solid #3c3c3c; border-radius: 3px; padding: 6px 14px; color: #cccccc; }"
        "QPushButton:hover { background-color: #3e3e42; color: #ffffff; border-color: #007acc; }"
        "QPushButton:disabled { background-color: #252526; border-color: #333333; color: #666666; }"
    );

    m_resumeBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Resume Chat"), this);
    m_resumeBtn->setStyleSheet(primaryBtnStyle);
    m_resumeBtn->setEnabled(false);
    connect(m_resumeBtn, &QPushButton::clicked, this, &AISessionDialog::slotResumeClicked);

    m_forkBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("call-start")), i18n("Branch / Fork"), this);
    m_forkBtn->setToolTip(i18n("Create a new independent session branching from this point"));
    m_forkBtn->setStyleSheet(secondaryBtnStyle);
    m_forkBtn->setEnabled(false);
    connect(m_forkBtn, &QPushButton::clicked, this, &AISessionDialog::slotForkClicked);

    m_renameBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-rename")), i18n("Rename"), this);
    m_renameBtn->setStyleSheet(secondaryBtnStyle);
    m_renameBtn->setEnabled(false);
    connect(m_renameBtn, &QPushButton::clicked, this, &AISessionDialog::slotRenameClicked);

    m_deleteBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Delete"), this);
    m_deleteBtn->setStyleSheet(secondaryBtnStyle);
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &AISessionDialog::slotDeleteClicked);

    m_newBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("+ New Chat"), this);
    m_newBtn->setStyleSheet(secondaryBtnStyle);
    connect(m_newBtn, &QPushButton::clicked, this, &AISessionDialog::slotNewSessionClicked);

    actionsLayout->addWidget(m_resumeBtn);
    actionsLayout->addWidget(m_forkBtn);
    actionsLayout->addWidget(m_renameBtn);
    actionsLayout->addWidget(m_deleteBtn);
    actionsLayout->addSpacing(10);
    actionsLayout->addWidget(m_newBtn);
    actionsLayout->addStretch();

    m_closeBtn = new QPushButton(i18n("Close"), this);
    m_closeBtn->setStyleSheet(secondaryBtnStyle);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    actionsLayout->addWidget(m_closeBtn);

    contentLayout->addLayout(actionsLayout);
    mainLayout->addLayout(contentLayout);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #858585; font-size: 11px;"));
    mainLayout->addWidget(m_statusLabel);
}

QString AISessionDialog::formatRelativeTime(qint64 timestampMs)
{
    if (timestampMs <= 0) return QStringLiteral("--");
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 diffSec = (now - timestampMs) / 1000;

    if (diffSec < 60) return i18n("Just now");
    if (diffSec < 3600) return i18n("%1 min ago", diffSec / 60);
    if (diffSec < 86400) return i18n("%1 hr ago", diffSec / 3600);
    if (diffSec < 172800) return i18n("Yesterday");

    QDateTime dt = QDateTime::fromMSecsSinceEpoch(timestampMs);
    return dt.toString(QStringLiteral("MMM d, yyyy"));
}

void AISessionDialog::populateList()
{
    m_sessionListWidget->clear();
    QString query = m_searchInput->text().trimmed().toLower();
    QString projFilter = m_projectFilterCombo->currentData().toString();

    m_currentSessions = AISessionManager::instance()->listSessions();
    QString activeId = AISessionManager::instance()->activeSessionId();

    int matchedCount = 0;
    for (const auto &meta : m_currentSessions) {
        if (projFilter != QStringLiteral("all") && !projFilter.isEmpty()) {
            if (meta.projectPath != projFilter) continue;
        }

        if (!query.isEmpty()) {
            if (!meta.title.toLower().contains(query) && !meta.projectPath.toLower().contains(query)) {
                continue;
            }
        }

        auto *item = new QListWidgetItem(m_sessionListWidget);
        item->setData(Qt::UserRole, meta.id);

        bool isActive = (meta.id == activeId);
        QString activeBadge = isActive ? QStringLiteral(" <span style='color: #4ec9b0; font-weight: bold;'>[ACTIVE]</span>") : QString();
        QString projName = meta.projectPath.isEmpty() ? i18n("Untitled Project") : QFileInfo(meta.projectPath).fileName();

        QString html = QStringLiteral(
            "<div style='line-height: 1.4;'>"
            "  <div><b style='color: #ffffff; font-size: 13px;'>%1</b>%2</div>"
            "  <div style='color: #9cdcfe; font-size: 11px;'>📁 %3</div>"
            "  <div style='color: #858585; font-size: 10.5px;'>💬 %4 messages | 🔧 %5 tools | %6 tokens | Last active: %7</div>"
            "</div>"
        ).arg(meta.title, activeBadge, projName, QString::number(meta.messageCount),
             QString::number(meta.toolCallCount), QString::number(meta.totalTokens),
             formatRelativeTime(meta.updatedAt));

        auto *label = new QLabel(html);
        label->setTextFormat(Qt::RichText);
        label->setContentsMargins(6, 6, 6, 6);
        item->setSizeHint(label->sizeHint());
        m_sessionListWidget->addItem(item);
        m_sessionListWidget->setItemWidget(item, label);

        matchedCount++;
    }

    m_statusLabel->setText(i18n("Found %1 conversation sessions", matchedCount));

    if (m_sessionListWidget->count() > 0) {
        m_sessionListWidget->setCurrentRow(0);
    } else {
        slotSessionSelected();
    }
}

void AISessionDialog::slotFilterChanged()
{
    populateList();
}

void AISessionDialog::slotSessionSelected()
{
    auto *cur = m_sessionListWidget->currentItem();
    bool hasSelection = (cur != nullptr);

    m_resumeBtn->setEnabled(hasSelection);
    m_forkBtn->setEnabled(hasSelection);
    m_renameBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);

    if (hasSelection) {
        m_selectedSessionId = cur->data(Qt::UserRole).toString();
    } else {
        m_selectedSessionId.clear();
    }
}

void AISessionDialog::slotResumeClicked()
{
    if (m_selectedSessionId.isEmpty()) return;
    AISessionManager::instance()->loadSession(m_selectedSessionId);
    accept();
}

void AISessionDialog::slotForkClicked()
{
    if (m_selectedSessionId.isEmpty()) return;
    QString newId = AISessionManager::instance()->forkSession(m_selectedSessionId);
    if (!newId.isEmpty()) {
        accept();
    }
}

void AISessionDialog::slotRenameClicked()
{
    if (m_selectedSessionId.isEmpty()) return;

    QString currentTitle;
    for (const auto &meta : m_currentSessions) {
        if (meta.id == m_selectedSessionId) {
            currentTitle = meta.title;
            break;
        }
    }

    bool ok = false;
    QString newTitle = QInputDialog::getText(this, i18n("Rename Session"), i18n("Enter new session title:"),
                                            QLineEdit::Normal, currentTitle, &ok);
    if (ok && !newTitle.trimmed().isEmpty()) {
        AISessionManager::instance()->renameSession(m_selectedSessionId, newTitle.trimmed());
        populateList();
    }
}

void AISessionDialog::slotDeleteClicked()
{
    if (m_selectedSessionId.isEmpty()) return;

    auto reply = QMessageBox::question(this, i18n("Delete Session"),
                                       i18n("Are you sure you want to delete this chat session?\nAll transcripts and context will be permanently removed."),
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        AISessionManager::instance()->deleteSession(m_selectedSessionId);
        populateList();
    }
}

void AISessionDialog::slotNewSessionClicked()
{
    QString currentProj;
    if (pCore && pCore->currentDoc()) {
        currentProj = pCore->currentDoc()->url().toLocalFile();
    }

    QString newId = AISessionManager::instance()->createNewSession(currentProj);
    AISessionManager::instance()->loadSession(newId);
    accept();
}

void AISessionDialog::slotRefreshList()
{
    populateList();
}
