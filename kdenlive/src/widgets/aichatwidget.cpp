/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#include "aichatwidget.h"
#include <KLocalizedString>
#include <QDateTime>
#include <QScrollBar>

#include "aidispatcher.h"
#include "aicommandrouter.h"

AIChatWidget::AIChatWidget(QWidget *parent)
    : QWidget(parent)
    , m_dispatcher(new AIDispatcher(this))
    , m_router(new AICommandRouter(this))
{
    setupUi();

    connect(this, &AIChatWidget::sendPromptRequested, m_dispatcher, &AIDispatcher::sendPrompt);
    connect(m_dispatcher, &AIDispatcher::responseReceived, this, &AIChatWidget::slotResponseReceived);
    connect(m_dispatcher, &AIDispatcher::errorOccurred, this, [&](const QString &err) {
        appendSystemMessage(i18n("Network error: %1", err));
    });
    connect(m_router, &AICommandRouter::executionFinished, this, &AIChatWidget::slotExecutionFinished);
}

void AIChatWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    // Top control bar: Target engine mode selector & Clear button
    auto *topBarLayout = new QHBoxLayout();
    auto *engineLabel = new QLabel(i18n("Engine:"), this);
    m_engineTargetSelector = new QComboBox(this);
    m_engineTargetSelector->addItem(i18n("Auto (Kdenlive & Natron)"), QStringLiteral("auto"));
    m_engineTargetSelector->addItem(i18n("Kdenlive (NLE / Cuts)"), QStringLiteral("kdenlive"));
    m_engineTargetSelector->addItem(i18n("Natron (VFX / Compositing)"), QStringLiteral("natron"));

    m_assetStudioBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-media-playlist")), i18n("📦 Assets"), this);
    m_assetStudioBtn->setToolTip(i18n("Open Velo Stock Media & AI Voiceover Studio"));
    connect(m_assetStudioBtn, &QPushButton::clicked, this, &AIChatWidget::openAssetStudioRequested);

    m_clearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), QString(), this);
    m_clearBtn->setToolTip(i18n("Clear Chat"));
    connect(m_clearBtn, &QPushButton::clicked, this, &AIChatWidget::slotClearChat);

    topBarLayout->addWidget(engineLabel);
    topBarLayout->addWidget(m_engineTargetSelector, 1);
    topBarLayout->addWidget(m_assetStudioBtn);
    topBarLayout->addWidget(m_clearBtn);
    mainLayout->addLayout(topBarLayout);

    // Chat Log display
    m_chatLog = new QTextBrowser(this);
    m_chatLog->setOpenExternalLinks(true);
    m_chatLog->setReadOnly(true);
    m_chatLog->setStyleSheet(QStringLiteral(
        "QTextBrowser { background-color: palette(base); border-radius: 4px; padding: 4px; }"
    ));
    mainLayout->addWidget(m_chatLog, 1);

    // Bottom prompt input row
    auto *inputLayout = new QHBoxLayout();
    m_promptInput = new QLineEdit(this);
    m_promptInput->setPlaceholderText(i18n("Ask AI to edit timeline, add effects, or compose VFX..."));
    m_promptInput->setClearButtonEnabled(true);
    connect(m_promptInput, &QLineEdit::returnPressed, this, &AIChatWidget::slotSendMessage);

    m_sendBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-send")), i18n("Send"), this);
    connect(m_sendBtn, &QPushButton::clicked, this, &AIChatWidget::slotSendMessage);

    inputLayout->addWidget(m_promptInput, 1);
    inputLayout->addWidget(m_sendBtn);
    mainLayout->addLayout(inputLayout);

    // Initial greeting
    appendSystemMessage(i18n("<b>AI-Mate initialized.</b> You can request timeline edits (cuts, transitions) or trigger headless Natron VFX pipelines."));
}

void AIChatWidget::appendMessage(const QString &sender, const QString &text, bool isUser)
{
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));
    QString color = isUser ? QStringLiteral("#3daee9") : QStringLiteral("#27ae60");
    QString align = isUser ? QStringLiteral("right") : QStringLiteral("left");

    QString formattedMsg = QStringLiteral(
        "<div style='margin-bottom: 8px; text-align: %1;'>"
        "<span style='font-size: 10px; color: gray;'>[%2] </span>"
        "<b style='color: %3;'>%4:</b><br/>"
        "<div style='display: inline-block; background: palette(alternate-base); border-radius: 4px; padding: 6px; margin-top: 2px;'>%5</div>"
        "</div>"
    ).arg(align, timestamp, color, sender, text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>")));

    m_chatLog->append(formattedMsg);
    m_chatLog->verticalScrollBar()->setValue(m_chatLog->verticalScrollBar()->maximum());
}

void AIChatWidget::appendSystemMessage(const QString &text)
{
    QString formattedMsg = QStringLiteral(
        "<div style='margin-bottom: 6px; color: gray; font-style: italic; text-align: center; font-size: 11px;'>"
        "%1"
        "</div>"
    ).arg(text);

    m_chatLog->append(formattedMsg);
    m_chatLog->verticalScrollBar()->setValue(m_chatLog->verticalScrollBar()->maximum());
}

void AIChatWidget::slotSendMessage()
{
    QString prompt = m_promptInput->text().trimmed();
    if (prompt.isEmpty()) {
        return;
    }

    appendMessage(i18n("You"), prompt, true);
    m_promptInput->clear();

    QString engine = m_engineTargetSelector->currentData().toString();
    Q_EMIT sendPromptRequested(prompt, engine);
}

void AIChatWidget::slotClearChat()
{
    m_chatLog->clear();
    appendSystemMessage(i18n("Chat history cleared."));
}

void AIChatWidget::slotResponseReceived(const QString &summaryText, const QJsonObject &actionPayload)
{
    appendMessage(i18n("AI Assistant"), summaryText, false);

    if (!actionPayload.isEmpty()) {
        m_router->executeAction(actionPayload);
    }
}

void AIChatWidget::slotExecutionFinished(const QString &resultMessage, bool success)
{
    if (success) {
        appendSystemMessage(QStringLiteral("✓ %1").arg(resultMessage));
    } else {
        appendSystemMessage(QStringLiteral("⚠ %1").arg(resultMessage));
    }
}

