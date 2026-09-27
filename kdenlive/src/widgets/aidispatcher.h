/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class AIToolRegistry;

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

    void sendPrompt(const QString &prompt, const QString &targetEngine);
    void setApiEndpoint(const QString &url);
    void setApiKey(const QString &key);
    void setModel(const QString &model);
    void loadEnvConfig(const QString &customPath = QString());

    /** @brief Returns the tool registry for external access */
    AIToolRegistry *toolRegistry() const { return m_toolRegistry; }
    QString supabaseUrl() const { return m_supabaseUrl; }
    QString supabaseAnonKey() const { return m_supabaseAnonKey; }

Q_SIGNALS:
    void responseReceived(const QString &summaryText, const QJsonObject &actionPayload);
    void errorOccurred(const QString &errorMessage);
    void requestStarted();
    void requestFinished();

private Q_SLOTS:
    void slotReplyFinished(QNetworkReply *reply);

private:
    void processAiResponse(const QByteArray &data);
    void processFallbackLocalIntent(const QString &prompt, const QString &targetEngine);
    QString buildEditorStateSnapshot();
    QString buildSystemPrompt();

    QNetworkAccessManager *m_nam{nullptr};
    AIToolRegistry *m_toolRegistry{nullptr};
    QString m_apiUrl{QStringLiteral("http://localhost:8080/v1/chat/completions")};
    QString m_apiKey;
    QString m_model{QStringLiteral("gpt-4o")};
    QString m_supabaseUrl;
    QString m_supabaseAnonKey;
    QString m_supabaseServiceKey;
    QString m_lastEngine;
    QString m_lastPrompt;
};
