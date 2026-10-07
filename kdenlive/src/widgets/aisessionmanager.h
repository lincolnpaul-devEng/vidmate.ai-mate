/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>

/**
 * @struct AISessionMetadata
 * @brief Header information and telemetry for an AI editing conversation thread.
 */
struct AISessionMetadata {
    QString id;
    QString title;
    QString projectPath;
    qint64 createdAt{0};
    qint64 updatedAt{0};
    int messageCount{0};
    int toolCallCount{0};
    int totalTokens{0};
    QString lastModel;

    QJsonObject toJson() const;
    static AISessionMetadata fromJson(const QJsonObject &obj);
};

/**
 * @struct AISessionStep
 * @brief An append-only entry matching Antigravity JSONL transcript event schema.
 */
struct AISessionStep {
    int stepIndex{0};
    QString type; // "USER_INPUT", "ASSISTANT", "TOOL_RESULT", "SYSTEM"
    QString content;
    QString thinking;
    QJsonArray toolCalls;
    QString toolName;
    QString toolArgsSummary;
    bool toolSuccess{true};
    QString toolError;
    qint64 createdAt{0};
    int inputTokens{0};
    int outputTokens{0};
    QString model;

    QJsonObject toJson() const;
    static AISessionStep fromJson(const QJsonObject &obj);
};

/**
 * @class AISessionManager
 * @brief Antigravity-grade conversation session persistence, indexing, resume, and rollback.
 *
 * Persists conversations to ~/.local/share/kdenlive/ai_sessions/<id>/
 * - session.json: High-level metadata & telemetry
 * - transcript.jsonl: Append-only JSONL stream of all turns & tool calls
 * - context.json: Compacted LLM messages array for instant hydration
 */
class AISessionManager : public QObject
{
    Q_OBJECT

public:
    static AISessionManager *instance();

    // ── Session Lifecycle ───────────────────────────────────────────────────
    QString createNewSession(const QString &projectPath = QString(), const QString &title = QString());
    bool loadSession(const QString &sessionId);
    bool saveActiveSession();
    void closeCurrentSession();

    // ── Antigravity-style Resume, Fork & Rollback ────────────────────────────
    QString resumeLatestForProject(const QString &projectPath);
    QString forkSession(const QString &sourceSessionId, int fromStep = -1);
    bool rollbackSession(const QString &sessionId, int targetStep);
    bool deleteSession(const QString &sessionId);
    bool renameSession(const QString &sessionId, const QString &newTitle);

    // ── Real-time Turn Appending ────────────────────────────────────────────
    void appendUserStep(const QString &text);
    void appendAssistantStep(const QString &text, const QString &thinking, const QJsonArray &toolCalls,
                            int inTokens = 0, int outTokens = 0, const QString &model = QString());
    void appendToolResultStep(const QString &toolName, const QString &argsSummary, bool success, const QString &resultOrError);
    void saveLlmContext(const QJsonArray &messages);

    // ── Retrieval & Queries ─────────────────────────────────────────────────
    QJsonArray loadLlmContext(const QString &sessionId) const;
    QVector<AISessionStep> loadTranscript(const QString &sessionId) const;
    QList<AISessionMetadata> listSessions(const QString &filterProjectPath = QString()) const;

    QString activeSessionId() const { return m_activeSessionId; }
    AISessionMetadata activeSessionMetadata() const { return m_activeMeta; }

    QString sessionsDirectory() const;

Q_SIGNALS:
    void sessionChanged(const QString &sessionId);
    void sessionLoaded(const AISessionMetadata &meta, const QVector<AISessionStep> &steps, const QJsonArray &llmContext);
    void sessionListUpdated();

private:
    explicit AISessionManager(QObject *parent = nullptr);
    ~AISessionManager() override = default;

    void loadIndex();
    void saveIndex();
    QString sessionDirPath(const QString &sessionId) const;
    void appendStepToJsonl(const QString &sessionId, const AISessionStep &step);

    mutable QMutex m_mutex;
    QString m_activeSessionId;
    AISessionMetadata m_activeMeta;
    QList<AISessionMetadata> m_sessionIndex;
    int m_currentStepCounter{0};
};
