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
#include <QStackedWidget>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include "aidispatcher.h"

class AICommandRouter;

/**
 * @struct ChatMessageEntry
 * @brief Retained message entry for history tracking, tool grouping, and virtual windowing.
 * Inspired by Velo's message-groups.ts architecture.
 */
struct ChatMessageEntry {
    enum Role { User, Assistant, Tool, System };
    Role role{System};
    QString text;
    // Tool-specific fields
    QString toolName;
    QString toolArgsSummary;
    bool toolSuccess{true};
    QString toolError;
    // Thinking block content (parsed from <thinking> tags in assistant responses)
    QString thinkingText;
    qint64 timestamp{0};
};

/**
 * @struct ToolOutcomeEntry
 * @brief Ring buffer entry for the diagnostic inspector's tool outcome history.
 * Mirrors Velo's AgentRunInspector tool outcome display.
 */
struct ToolOutcomeEntry {
    QString toolName;
    QString argsSummary;
    bool success{true};
    QString error;
    qint64 timestamp{0};
};

/**
 * @struct SessionMetrics
 * @brief Accumulated session metrics for the diagnostic inspector dialog.
 * Mirrors Velo's AgentRunInspector telemetry breakdown.
 */
struct SessionMetrics {
    int totalRequests{0};
    int totalInputTokens{0};
    int totalOutputTokens{0};
    int totalRetries{0};
    int totalToolCalls{0};
    int successfulTools{0};
    int failedTools{0};
    qint64 totalLatencyMs{0};
    // Per-request metrics (last request)
    int lastInputTokens{0};
    int lastOutputTokens{0};
    qint64 lastLatencyMs{0};
    QString lastModel;
};

/**
 * @class AIChatWidget
 * @brief Autonomous Video Editor Agent Workspace with Velo-inspired real-time feedback,
 * tool call grouping, pulsing animations, thinking phrases, diagnostic inspector,
 * and auto-follow scroll.
 */
class AIChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AIChatWidget(QWidget *parent = nullptr);
    ~AIChatWidget() override = default;

    void appendUserMessage(const QString &text);
    void appendAssistantResponse(const QString &text);
    void appendToolExecution(const QString &toolName, const QString &paramsSummary, bool success, const QString &errorMsg = QString());
    void appendSystemMessage(const QString &text);

Q_SIGNALS:
    void sendPromptRequested(const QString &prompt, const QString &targetEngine);
    void openAssetStudioRequested();

private Q_SLOTS:
    void slotSendMessage();
    void slotClearChat();
    void slotToggleSettings();
    void slotResponseReceived(const QString &summaryText, const QJsonObject &actionPayload);
    void slotExecutionFinished(const QString &resultMessage, bool success);
    void slotToolDataOutput(const QString &toolName, const QJsonObject &data);
    void slotRequestStarted();
    void slotRequestFinished();
    void slotUpdateLiveTimer();
    void slotQuickActionTriggered();
    void slotApplyProposal();
    void slotRejectProposal();
    void slotSaveSettings();
    void slotFilterModels();
    void slotRefreshModels();
    void slotModelsLoaded(const QJsonArray &models);
    void slotMetricsUpdated(int totalTokens, qint64 latencyMs, const QString &modelId);
    void slotShowInspector();
    void slotShowAuthDialog();
    void slotAuthStateChanged(bool isLoggedIn, const QString &email);
    void slotGoalStarted(const QString &goal);
    void slotGoalStepStarted(int step, int maxSteps, const QString &actionName);
    void slotGoalStepFinished(int step, int maxSteps, const QString &actionName, bool success);
    void slotGoalFinished(const QString &finalSummary);
    void slotOpenSessionDialog();
    void slotNewSession();

private:
    void setupUi();
    void setupWorkspacePage(QWidget *page);
    void setupSettingsPage(QWidget *page);
    void updateModeBadge();
    void updateAuthButton();
    void updateMetricsDisplay(int totalTokens, qint64 latencyMs, const QString &modelId);
    QString formatMarkdownHtml(const QString &rawText);

    // Velo-inspired message management
    void rebuildMessageView();
    bool isScrollNearBottom() const;
    void scrollToBottomIfFollowing();

    // Velo-inspired tool argument summarizer (prioritized key extraction, UUID truncation)
    static QString extractToolArgSummary(const QString &paramsJson);
    // Film production thinking phrases (deterministic per-turn selection)
    static const char *thinkingPhrase(int seed);

    QStackedWidget *m_stackedWidget{nullptr};

    // ── Workspace Page Widgets ──────────────────────────────────────────────
    QWidget *m_workspacePage{nullptr};
    QTextBrowser *m_messageStream{nullptr};
    QLineEdit *m_promptInput{nullptr};
    QPushButton *m_sendBtn{nullptr};
    QPushButton *m_authBtn{nullptr};
    QPushButton *m_settingsBtn{nullptr};
    QPushButton *m_clearBtn{nullptr};
    QPushButton *m_assetStudioBtn{nullptr};
    QPushButton *m_inspectorBtn{nullptr};
    QPushButton *m_sessionTitleBtn{nullptr};
    QPushButton *m_newSessionBtn{nullptr};
    QPushButton *m_sessionsHistoryBtn{nullptr};
    QLabel *m_modeBadge{nullptr};
    QComboBox *m_engineTargetSelector{nullptr};

    // Bottom Metrics Chips (Tokens, Latency, Model)
    QWidget *m_metricsFooter{nullptr};
    QLabel *m_modelTag{nullptr};
    QLabel *m_tokensTag{nullptr};
    QLabel *m_latencyTag{nullptr};

    // Live Run Status Bar with pulsing animation
    QWidget *m_liveStatusBar{nullptr};
    QLabel *m_liveStatusDot{nullptr};
    QLabel *m_liveStatusText{nullptr};
    QLabel *m_liveStatusTimer{nullptr};
    QTimer *m_statusTimer{nullptr};
    QElapsedTimer m_elapsedTimer;
    int m_thinkingSeed{0};

    // Proposal Card (in Ask Mode)
    QWidget *m_proposalCard{nullptr};
    QLabel *m_proposalTitle{nullptr};
    QLabel *m_proposalSummary{nullptr};
    QPushButton *m_applyProposalBtn{nullptr};
    QPushButton *m_rejectProposalBtn{nullptr};
    QJsonObject m_pendingProposalAction;

    // ── Message History & Grouping (Velo message-groups.ts pattern) ─────────
    QVector<ChatMessageEntry> m_messages;
    static constexpr int MESSAGE_WINDOW_SIZE = 60;
    static constexpr int TOOL_GROUP_MIN = 3;
    bool m_autoFollow{true};

    // ── Diagnostic Inspector Data (Velo AgentRunInspector pattern) ──────────
    SessionMetrics m_sessionMetrics;
    QVector<ToolOutcomeEntry> m_toolOutcomes;
    static constexpr int MAX_TOOL_OUTCOMES = 12;

    // ── Settings Page Widgets ───────────────────────────────────────────────
    QWidget *m_settingsPage{nullptr};
    QRadioButton *m_modeAskRadio{nullptr};
    QRadioButton *m_modeYoloRadio{nullptr};
    QButtonGroup *m_modeGroup{nullptr};

    QLineEdit *m_modelSearchInput{nullptr};
    QComboBox *m_providerFilter{nullptr};
    QComboBox *m_editingModelSelector{nullptr};

    QComboBox *m_imageModelSelector{nullptr};
    QComboBox *m_videoModelSelector{nullptr};
    QComboBox *m_musicModelSelector{nullptr};
    QComboBox *m_voiceModelSelector{nullptr};
    QComboBox *m_soundModelSelector{nullptr};

    QButtonGroup *m_mgTierGroup{nullptr};
    QRadioButton *m_mgSpeedRadio{nullptr};
    QRadioButton *m_mgBalanceRadio{nullptr};
    QRadioButton *m_mgQualityRadio{nullptr};

    QButtonGroup *m_cacheGroup{nullptr};
    QRadioButton *m_cacheShortRadio{nullptr};
    QRadioButton *m_cacheLongRadio{nullptr};

    QCheckBox *m_cloudAssetsCheck{nullptr};
    QCheckBox *m_planModeCheck{nullptr};

    struct DynamicModelInfo {
        QString id;
        QString name;
        QString provider;
        int contextLength{0};
    };
    QList<DynamicModelInfo> m_dynamicModels;
    QPushButton *m_refreshModelsBtn{nullptr};
    QLabel *m_modelsStatusLabel{nullptr};
    QString m_lastExecutedActionName;

    // Backend Engines
    AIDispatcher *m_dispatcher{nullptr};
    AICommandRouter *m_router{nullptr};
};
