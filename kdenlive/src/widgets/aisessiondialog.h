/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "aisessionmanager.h"

/**
 * @class AISessionDialog
 * @brief Antigravity-grade interactive session picker and conversation manager.
 *
 * Provides searchable history navigation, project scoping, session resume,
 * branching/forking, renaming, and transcript export.
 */
class AISessionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AISessionDialog(QWidget *parent = nullptr);
    ~AISessionDialog() override = default;

    QString selectedSessionId() const { return m_selectedSessionId; }

private Q_SLOTS:
    void slotFilterChanged();
    void slotSessionSelected();
    void slotResumeClicked();
    void slotForkClicked();
    void slotRenameClicked();
    void slotDeleteClicked();
    void slotNewSessionClicked();
    void slotRefreshList();

private:
    void setupUi();
    void populateList();
    static QString formatRelativeTime(qint64 timestampMs);

    QLineEdit *m_searchInput{nullptr};
    QComboBox *m_projectFilterCombo{nullptr};
    QListWidget *m_sessionListWidget{nullptr};
    QLabel *m_statusLabel{nullptr};

    QPushButton *m_resumeBtn{nullptr};
    QPushButton *m_forkBtn{nullptr};
    QPushButton *m_renameBtn{nullptr};
    QPushButton *m_deleteBtn{nullptr};
    QPushButton *m_newBtn{nullptr};
    QPushButton *m_closeBtn{nullptr};

    QString m_selectedSessionId;
    QList<AISessionMetadata> m_currentSessions;
};
