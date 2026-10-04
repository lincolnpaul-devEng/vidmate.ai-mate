/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>

class AIToolRegistry;

struct AIAgentSettings {
    QString mode{QStringLiteral("yolo")}; // "ask" or "yolo"
    QString editingModelId{QStringLiteral("auto")};
    QString provider{QStringLiteral("all")}; // "all", "openrouter", "groq"
    QString imageModel{QStringLiteral("auto")};
    QString videoModel{QStringLiteral("auto")};
    QString musicModel{QStringLiteral("auto")};
    QString voiceModel{QStringLiteral("auto")};
    QString soundModel{QStringLiteral("auto")};
    QString mgTier{QStringLiteral("balance")}; // "speed", "balance", "quality"
    QString cacheMode{QStringLiteral("short")}; // "short", "long"
    bool cloudAssetsAccess{true};
    bool planMode{false};
};

/**
 * @class AIDispatcher
 * @brief Handles network communications with AI endpoints and dispatches JSON commands.
 *
 * Supports both standard chat-completions API (with JSON extraction from markdown)
 * and OpenAI function-calling API using the AIToolRegistry schemas.
 */
class AIDispatcher : public QObject
{
    Q_OBJECT

public:
    explicit AIDispatcher(QObject *parent = nullptr);
    ~AIDispatcher() override = default;

    void sendPrompt(const QString &prompt, const QString &targetEngine = QStringLiteral("auto"));
    void feedObservationAndContinue(const QString &actionName, const QString &observationResult, bool success);
    void cancelCurrentGoal();
    void clearConversation();

    bool isGoalActive() const { return m_goalActive; }
    int currentStep() const { return m_currentStep; }
    int maxSteps() const { return m_maxSteps; }
    QString currentGoal() const { return m_currentGoal; }

    void setApiEndpoint(const QString &url);
    void setApiKey(const QString &key);
    void setModel(const QString &model);
    void loadEnvConfig(const QString &customPath = QString());

    /** @brief Dynamically fetches all live models from OpenRouter and Groq via Supabase ai-proxy */
    void fetchAvailableModels(std::function<void(const QJsonArray &models)> callback = nullptr);

    AIAgentSettings agentSettings() const { return m_settings; }
    void setAgentSettings(const AIAgentSettings &settings);
    void loadSettings();
    void saveSettings();

    /** @brief Returns the tool registry for external access */
    AIToolRegistry *toolRegistry() const { return m_toolRegistry; }
    QString supabaseUrl() const { return m_supabaseUrl; }
    QString supabaseAnonKey() const { return m_supabaseAnonKey; }
    QString currentModel() const { return m_model; }
    QString openRouterKey() const { return m_openRouterKey; }
    void setOpenRouterKey(const QString &key) { m_openRouterKey = key; }
    QString groqKey() const { return m_groqKey; }
    void setGroqKey(const QString &key) { m_groqKey = key; }

Q_SIGNALS:
    void responseReceived(const QString &summaryText, const QJsonObject &actionPayload);
    void metricsUpdated(int totalTokens, qint64 latencyMs, const QString &modelId);
    void errorOccurred(const QString &errorMessage);
    void requestStarted();
    void requestFinished();
    void agentSettingsChanged(const AIAgentSettings &settings);
    void modelsLoaded(const QJsonArray &models);
    void goalStarted(const QString &goal);
    void goalStepStarted(int step, int maxSteps, const QString &actionName);
    void goalStepFinished(int step, int maxSteps, const QString &actionName, bool success);
    void goalFinished(const QString &finalSummary);

private Q_SLOTS:
    void slotReplyFinished(QNetworkReply *reply, const QString &modelUsed = QString());

private:
    void sendCurrentMessages();
    void processAiResponse(const QByteArray &data);
    QString buildEditorStateSnapshot();
    QString buildSystemPrompt();

    QNetworkAccessManager *m_nam{nullptr};
    AIToolRegistry *m_toolRegistry{nullptr};
    QElapsedTimer m_requestTimer;
    QString m_apiUrl{QStringLiteral("http://localhost:8080/v1/chat/completions")};
    QString m_apiKey;
    QString m_openRouterKey;
    QString m_groqKey;
    QString m_model{QStringLiteral("groq/openai/gpt-oss-120b")};
    QString m_supabaseUrl;
    QString m_supabaseAnonKey;
    QString m_supabaseServiceKey;
    QString m_lastEngine;
    QString m_lastPrompt;
    QString m_currentGoal;
    QJsonArray m_conversationMessages;
    bool m_goalActive{false};
    int m_currentStep{0};
    int m_maxSteps{10};
    AIAgentSettings m_settings;
};
