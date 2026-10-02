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
#include "aidispatcher.h"

class AICommandRouter;

/**
 * @class AIChatWidget
 * @brief Autonomous Video Editor Agent Workspace with real-time feedback & Velo settings.
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

private:
    void setupUi();
    void setupWorkspacePage(QWidget *page);
    void setupSettingsPage(QWidget *page);
    void updateModeBadge();
    void updateMetricsDisplay(int totalTokens, qint64 latencyMs, const QString &modelId);
    QString formatMarkdownHtml(const QString &rawText);

    QStackedWidget *m_stackedWidget{nullptr};

    // ── Workspace Page Widgets ──────────────────────────────────────────────
    QWidget *m_workspacePage{nullptr};
    QTextBrowser *m_messageStream{nullptr};
    QLineEdit *m_promptInput{nullptr};
    QPushButton *m_sendBtn{nullptr};
    QPushButton *m_settingsBtn{nullptr};
    QPushButton *m_clearBtn{nullptr};
    QPushButton *m_assetStudioBtn{nullptr};
    QLabel *m_modeBadge{nullptr};
    QComboBox *m_engineTargetSelector{nullptr};

    // Bottom Metrics Chips (Tokens, Latency, Model)
    QWidget *m_metricsFooter{nullptr};
    QLabel *m_modelTag{nullptr};
    QLabel *m_tokensTag{nullptr};
    QLabel *m_latencyTag{nullptr};

    // Live Run Status Bar
    QWidget *m_liveStatusBar{nullptr};
    QLabel *m_liveStatusDot{nullptr};
    QLabel *m_liveStatusText{nullptr};
    QLabel *m_liveStatusTimer{nullptr};
    QTimer *m_statusTimer{nullptr};
    QElapsedTimer m_elapsedTimer;

    // Proposal Card (in Ask Mode)
    QWidget *m_proposalCard{nullptr};
    QLabel *m_proposalTitle{nullptr};
    QLabel *m_proposalSummary{nullptr};
    QPushButton *m_applyProposalBtn{nullptr};
    QPushButton *m_rejectProposalBtn{nullptr};
    QJsonObject m_pendingProposalAction;

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

    // Backend Engines
    AIDispatcher *m_dispatcher{nullptr};
    AICommandRouter *m_router{nullptr};
};
