/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#pragma once

#include <QWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

class AIDispatcher;
class AICommandRouter;

/**
 * @class AIChatWidget
 * @brief Native AI Assistant Chat Dock for interacting with Kdenlive & Natron
 */
class AIChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AIChatWidget(QWidget *parent = nullptr);
    ~AIChatWidget() override = default;

    void appendMessage(const QString &sender, const QString &text, bool isUser = false);
    void appendSystemMessage(const QString &text);

Q_SIGNALS:
    void sendPromptRequested(const QString &prompt, const QString &targetEngine);
    void openAssetStudioRequested();

private Q_SLOTS:
    void slotSendMessage();
    void slotClearChat();
    void slotResponseReceived(const QString &summaryText, const QJsonObject &actionPayload);
    void slotExecutionFinished(const QString &resultMessage, bool success);

private:
    void setupUi();

    QTextBrowser *m_chatLog{nullptr};
    QLineEdit *m_promptInput{nullptr};
    QPushButton *m_sendBtn{nullptr};
    QPushButton *m_clearBtn{nullptr};
    QPushButton *m_assetStudioBtn{nullptr};
    QComboBox *m_engineTargetSelector{nullptr};

    AIDispatcher *m_dispatcher{nullptr};
    AICommandRouter *m_router{nullptr};
};
