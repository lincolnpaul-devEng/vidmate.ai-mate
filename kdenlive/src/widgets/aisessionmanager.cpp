/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#include "aisessionmanager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QDateTime>
#include <QUuid>
#include <QTextStream>
#include <QDebug>

// ════════════════════════════════════════════════════════════════════════════
// Serialization Helpers
// ════════════════════════════════════════════════════════════════════════════

QJsonObject AISessionMetadata::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("title")] = title;
    obj[QStringLiteral("project_path")] = projectPath;
    obj[QStringLiteral("created_at")] = createdAt;
    obj[QStringLiteral("updated_at")] = updatedAt;
    obj[QStringLiteral("message_count")] = messageCount;
    obj[QStringLiteral("tool_call_count")] = toolCallCount;
    obj[QStringLiteral("total_tokens")] = totalTokens;
    obj[QStringLiteral("last_model")] = lastModel;
    return obj;
}

AISessionMetadata AISessionMetadata::fromJson(const QJsonObject &obj)
{
    AISessionMetadata meta;
    meta.id = obj[QStringLiteral("id")].toString();
    meta.title = obj[QStringLiteral("title")].toString();
    meta.projectPath = obj[QStringLiteral("project_path")].toString();
    meta.createdAt = obj[QStringLiteral("created_at")].toVariant().toLongLong();
    meta.updatedAt = obj[QStringLiteral("updated_at")].toVariant().toLongLong();
    meta.messageCount = obj[QStringLiteral("message_count")].toInt();
    meta.toolCallCount = obj[QStringLiteral("tool_call_count")].toInt();
    meta.totalTokens = obj[QStringLiteral("total_tokens")].toInt();
    meta.lastModel = obj[QStringLiteral("last_model")].toString();
    return meta;
}

QJsonObject AISessionStep::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("step_index")] = stepIndex;
    obj[QStringLiteral("type")] = type;
    obj[QStringLiteral("content")] = content;
    if (!thinking.isEmpty()) obj[QStringLiteral("thinking")] = thinking;
    if (!toolCalls.isEmpty()) obj[QStringLiteral("tool_calls")] = toolCalls;
    if (!toolName.isEmpty()) obj[QStringLiteral("tool_name")] = toolName;
    if (!toolArgsSummary.isEmpty()) obj[QStringLiteral("tool_args_summary")] = toolArgsSummary;
    obj[QStringLiteral("tool_success")] = toolSuccess;
    if (!toolError.isEmpty()) obj[QStringLiteral("tool_error")] = toolError;
    obj[QStringLiteral("created_at")] = createdAt;
    if (inputTokens > 0) obj[QStringLiteral("input_tokens")] = inputTokens;
    if (outputTokens > 0) obj[QStringLiteral("output_tokens")] = outputTokens;
    if (!model.isEmpty()) obj[QStringLiteral("model")] = model;
    return obj;
}

AISessionStep AISessionStep::fromJson(const QJsonObject &obj)
{
    AISessionStep step;
    step.stepIndex = obj[QStringLiteral("step_index")].toInt();
    step.type = obj[QStringLiteral("type")].toString();
    step.content = obj[QStringLiteral("content")].toString();
    step.thinking = obj[QStringLiteral("thinking")].toString();
    step.toolCalls = obj[QStringLiteral("tool_calls")].toArray();
    step.toolName = obj[QStringLiteral("tool_name")].toString();
    step.toolArgsSummary = obj[QStringLiteral("tool_args_summary")].toString();
    step.toolSuccess = obj[QStringLiteral("tool_success")].toBool(true);
    step.toolError = obj[QStringLiteral("tool_error")].toString();
    step.createdAt = obj[QStringLiteral("created_at")].toVariant().toLongLong();
    step.inputTokens = obj[QStringLiteral("input_tokens")].toInt();
    step.outputTokens = obj[QStringLiteral("output_tokens")].toInt();
    step.model = obj[QStringLiteral("model")].toString();
    return step;
}

// ════════════════════════════════════════════════════════════════════════════
// AISessionManager Implementation
// ════════════════════════════════════════════════════════════════════════════

AISessionManager *AISessionManager::instance()
{
    static AISessionManager s_instance;
    return &s_instance;
}

AISessionManager::AISessionManager(QObject *parent)
    : QObject(parent)
{
    QDir dir(sessionsDirectory());
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    loadIndex();
}

QString AISessionManager::sessionsDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/ai_sessions");
}

QString AISessionManager::sessionDirPath(const QString &sessionId) const
{
    return QStringLiteral("%1/%2").arg(sessionsDirectory(), sessionId);
}

void AISessionManager::loadIndex()
{
    QMutexLocker locker(&m_mutex);
    m_sessionIndex.clear();

    QString indexPath = QStringLiteral("%1/sessions_index.json").arg(sessionsDirectory());
    QFile file(indexPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        for (const auto &val : arr) {
            m_sessionIndex.append(AISessionMetadata::fromJson(val.toObject()));
        }
    }
}

void AISessionManager::saveIndex()
{
    QString indexPath = QStringLiteral("%1/sessions_index.json").arg(sessionsDirectory());
    QFile file(indexPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[AISessionManager] Failed to write sessions_index.json";
        return;
    }

    QJsonArray arr;
    for (const auto &meta : m_sessionIndex) {
        arr.append(meta.toJson());
    }

    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.close();
}

QString AISessionManager::createNewSession(const QString &projectPath, const QString &title)
{
    QMutexLocker locker(&m_mutex);

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    AISessionMetadata meta;
    meta.id = id;
    meta.title = title.isEmpty() ? QStringLiteral("New Edit Session") : title;
    meta.projectPath = projectPath;
    meta.createdAt = now;
    meta.updatedAt = now;
    meta.messageCount = 0;
    meta.toolCallCount = 0;
    meta.totalTokens = 0;

    // Create session directory
    QDir().mkpath(sessionDirPath(id));

    // Save initial session.json
    QString metaPath = QStringLiteral("%1/session.json").arg(sessionDirPath(id));
    QFile f(metaPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(QJsonDocument(meta.toJson()).toJson(QJsonDocument::Indented));
        f.close();
    }

    // Touch empty transcript.jsonl & context.json
    QFile(QStringLiteral("%1/transcript.jsonl").arg(sessionDirPath(id))).open(QIODevice::WriteOnly);
    QFile ctxF(QStringLiteral("%1/context.json").arg(sessionDirPath(id)));
    if (ctxF.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonObject root;
        root[QStringLiteral("messages")] = QJsonArray();
        ctxF.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        ctxF.close();
    }

    m_activeSessionId = id;
    m_activeMeta = meta;
    m_currentStepCounter = 0;

    m_sessionIndex.prepend(meta);
    saveIndex();

    locker.unlock();
    Q_EMIT sessionChanged(id);
    Q_EMIT sessionListUpdated();

    return id;
}

bool AISessionManager::loadSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);
    QString dir = sessionDirPath(sessionId);
    if (!QDir(dir).exists()) {
        qWarning() << "[AISessionManager] Session directory does not exist:" << dir;
        return false;
    }

    // Load session.json
    QString metaPath = QStringLiteral("%1/session.json").arg(dir);
    QFile f(metaPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (doc.isObject()) {
            m_activeMeta = AISessionMetadata::fromJson(doc.object());
        }
    } else {
        m_activeMeta.id = sessionId;
        m_activeMeta.title = QStringLiteral("Session %1").arg(sessionId.left(8));
    }

    m_activeSessionId = sessionId;

    // Load transcript and LLM context
    locker.unlock();
    QVector<AISessionStep> steps = loadTranscript(sessionId);
    QJsonArray llmContext = loadLlmContext(sessionId);

    locker.relock();
    m_currentStepCounter = steps.size();
    m_activeMeta.messageCount = steps.size();

    // Touch updated timestamp
    m_activeMeta.updatedAt = QDateTime::currentMSecsSinceEpoch();
    saveActiveSession();

    locker.unlock();
    Q_EMIT sessionChanged(sessionId);
    Q_EMIT sessionLoaded(m_activeMeta, steps, llmContext);
    Q_EMIT sessionListUpdated();

    return true;
}

bool AISessionManager::saveActiveSession()
{
    if (m_activeSessionId.isEmpty()) return false;

    QString metaPath = QStringLiteral("%1/session.json").arg(sessionDirPath(m_activeSessionId));
    QFile f(metaPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_activeMeta.updatedAt = QDateTime::currentMSecsSinceEpoch();
        f.write(QJsonDocument(m_activeMeta.toJson()).toJson(QJsonDocument::Indented));
        f.close();
    }

    // Update in memory index
    for (int i = 0; i < m_sessionIndex.size(); ++i) {
        if (m_sessionIndex[i].id == m_activeSessionId) {
            m_sessionIndex[i] = m_activeMeta;
            // Move to front (most recently active)
            if (i > 0) {
                m_sessionIndex.move(i, 0);
            }
            break;
        }
    }
    saveIndex();
    return true;
}

void AISessionManager::closeCurrentSession()
{
    saveActiveSession();
    m_activeSessionId.clear();
    m_activeMeta = AISessionMetadata();
    m_currentStepCounter = 0;
}

QString AISessionManager::resumeLatestForProject(const QString &projectPath)
{
    QMutexLocker locker(&m_mutex);
    if (m_sessionIndex.isEmpty()) return QString();

    if (!projectPath.isEmpty()) {
        for (const auto &meta : m_sessionIndex) {
            if (meta.projectPath == projectPath) {
                return meta.id;
            }
        }
    }

    // If no project-specific match, return most recent overall session
    return m_sessionIndex.first().id;
}

QString AISessionManager::forkSession(const QString &sourceSessionId, int fromStep)
{
    QMutexLocker locker(&m_mutex);
    if (sourceSessionId.isEmpty()) return QString();

    QVector<AISessionStep> sourceSteps = loadTranscript(sourceSessionId);
    if (sourceSteps.isEmpty() && fromStep < 0) return QString();

    QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Find source metadata
    AISessionMetadata srcMeta;
    for (const auto &m : m_sessionIndex) {
        if (m.id == sourceSessionId) {
            srcMeta = m;
            break;
        }
    }

    AISessionMetadata newMeta = srcMeta;
    newMeta.id = newId;
    newMeta.title = QStringLiteral("[Branch] %1").arg(srcMeta.title.isEmpty() ? QStringLiteral("Edit Thread") : srcMeta.title);
    newMeta.createdAt = now;
    newMeta.updatedAt = now;

    QDir().mkpath(sessionDirPath(newId));

    // Filter steps up to fromStep if specified
    QVector<AISessionStep> branchedSteps;
    int limit = (fromStep >= 0) ? qMin(fromStep + 1, sourceSteps.size()) : sourceSteps.size();
    for (int i = 0; i < limit; ++i) {
        branchedSteps.append(sourceSteps[i]);
    }

    newMeta.messageCount = branchedSteps.size();

    // Write branched transcript.jsonl
    QString jsonlPath = QStringLiteral("%1/transcript.jsonl").arg(sessionDirPath(newId));
    QFile jsonlFile(jsonlPath);
    if (jsonlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&jsonlFile);
        for (const auto &step : branchedSteps) {
            out << QJsonDocument(step.toJson()).toJson(QJsonDocument::Compact) << "\n";
        }
        jsonlFile.close();
    }

    // Reconstruct and save context.json
    QJsonArray ctxMessages;
    for (const auto &st : branchedSteps) {
        if (st.type == QStringLiteral("USER_INPUT")) {
            ctxMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), st.content}});
        } else if (st.type == QStringLiteral("ASSISTANT")) {
            QJsonObject mObj{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), st.content}};
            if (!st.toolCalls.isEmpty()) mObj[QStringLiteral("tool_calls")] = st.toolCalls;
            ctxMessages.append(mObj);
        } else if (st.type == QStringLiteral("TOOL_RESULT")) {
            QJsonObject mObj{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("name"), st.toolName}, {QStringLiteral("content"), st.content}};
            ctxMessages.append(mObj);
        }
    }

    QFile ctxF(QStringLiteral("%1/context.json").arg(sessionDirPath(newId)));
    if (ctxF.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonObject root;
        root[QStringLiteral("messages")] = ctxMessages;
        ctxF.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        ctxF.close();
    }

    // Save session.json
    QFile metaF(QStringLiteral("%1/session.json").arg(sessionDirPath(newId)));
    if (metaF.open(QIODevice::WriteOnly | QIODevice::Text)) {
        metaF.write(QJsonDocument(newMeta.toJson()).toJson(QJsonDocument::Indented));
        metaF.close();
    }

    m_sessionIndex.prepend(newMeta);
    saveIndex();

    locker.unlock();
    Q_EMIT sessionListUpdated();

    // Automatically load the newly branched session
    loadSession(newId);
    return newId;
}

bool AISessionManager::rollbackSession(const QString &sessionId, int targetStep)
{
    if (sessionId.isEmpty() || targetStep < 0) return false;

    QVector<AISessionStep> steps = loadTranscript(sessionId);
    if (targetStep >= steps.size()) return false;

    QVector<AISessionStep> retainedSteps;
    for (int i = 0; i <= targetStep; ++i) {
        retainedSteps.append(steps[i]);
    }

    // Rewrite transcript.jsonl
    QString jsonlPath = QStringLiteral("%1/transcript.jsonl").arg(sessionDirPath(sessionId));
    QFile jsonlFile(jsonlPath);
    if (jsonlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&jsonlFile);
        for (const auto &step : retainedSteps) {
            out << QJsonDocument(step.toJson()).toJson(QJsonDocument::Compact) << "\n";
        }
        jsonlFile.close();
    }

    // Rewrite context.json
    QJsonArray ctxMessages;
    for (const auto &st : retainedSteps) {
        if (st.type == QStringLiteral("USER_INPUT")) {
            ctxMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), st.content}});
        } else if (st.type == QStringLiteral("ASSISTANT")) {
            QJsonObject mObj{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), st.content}};
            if (!st.toolCalls.isEmpty()) mObj[QStringLiteral("tool_calls")] = st.toolCalls;
            ctxMessages.append(mObj);
        } else if (st.type == QStringLiteral("TOOL_RESULT")) {
            QJsonObject mObj{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("name"), st.toolName}, {QStringLiteral("content"), st.content}};
            ctxMessages.append(mObj);
        }
    }

    QFile ctxF(QStringLiteral("%1/context.json").arg(sessionDirPath(sessionId)));
    if (ctxF.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonObject root;
        root[QStringLiteral("messages")] = ctxMessages;
        ctxF.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        ctxF.close();
    }

    // Reload active session
    if (m_activeSessionId == sessionId) {
        loadSession(sessionId);
    }
    return true;
}

bool AISessionManager::deleteSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);
    QString dir = sessionDirPath(sessionId);
    if (QDir(dir).exists()) {
        QDir(dir).removeRecursively();
    }

    for (int i = 0; i < m_sessionIndex.size(); ++i) {
        if (m_sessionIndex[i].id == sessionId) {
            m_sessionIndex.removeAt(i);
            break;
        }
    }
    saveIndex();

    bool wasActive = (m_activeSessionId == sessionId);
    if (wasActive) {
        m_activeSessionId.clear();
        m_activeMeta = AISessionMetadata();
    }

    locker.unlock();
    Q_EMIT sessionListUpdated();

    if (wasActive && !m_sessionIndex.isEmpty()) {
        loadSession(m_sessionIndex.first().id);
    }
    return true;
}

bool AISessionManager::renameSession(const QString &sessionId, const QString &newTitle)
{
    if (sessionId.isEmpty() || newTitle.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_sessionIndex.size(); ++i) {
        if (m_sessionIndex[i].id == sessionId) {
            m_sessionIndex[i].title = newTitle;
            m_sessionIndex[i].updatedAt = QDateTime::currentMSecsSinceEpoch();
            if (m_activeSessionId == sessionId) {
                m_activeMeta.title = newTitle;
            }
            break;
        }
    }
    saveIndex();

    // Update session.json
    QString metaPath = QStringLiteral("%1/session.json").arg(sessionDirPath(sessionId));
    QFile f(metaPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        root[QStringLiteral("title")] = newTitle;
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            f.close();
        }
    }

    locker.unlock();
    Q_EMIT sessionListUpdated();
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// Real-time Turn Appending
// ════════════════════════════════════════════════════════════════════════════

void AISessionManager::appendStepToJsonl(const QString &sessionId, const AISessionStep &step)
{
    QString jsonlPath = QStringLiteral("%1/transcript.jsonl").arg(sessionDirPath(sessionId));
    QFile f(jsonlPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out << QJsonDocument(step.toJson()).toJson(QJsonDocument::Compact) << "\n";
        f.close();
    }
}

void AISessionManager::appendUserStep(const QString &text)
{
    if (m_activeSessionId.isEmpty()) {
        createNewSession();
    }

    AISessionStep step;
    step.stepIndex = m_currentStepCounter++;
    step.type = QStringLiteral("USER_INPUT");
    step.content = text;
    step.createdAt = QDateTime::currentMSecsSinceEpoch();

    appendStepToJsonl(m_activeSessionId, step);

    // Auto-title session from first user prompt if still default
    if (m_activeMeta.messageCount == 0 || m_activeMeta.title == QStringLiteral("New Edit Session")) {
        QString firstLine = text.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (!firstLine.isEmpty()) {
            if (firstLine.length() > 36) firstLine = firstLine.left(33) + QStringLiteral("...");
            m_activeMeta.title = firstLine;
        }
    }

    m_activeMeta.messageCount++;
    saveActiveSession();
}

void AISessionManager::appendAssistantStep(const QString &text, const QString &thinking, const QJsonArray &toolCalls,
                                           int inTokens, int outTokens, const QString &model)
{
    if (m_activeSessionId.isEmpty()) return;

    AISessionStep step;
    step.stepIndex = m_currentStepCounter++;
    step.type = QStringLiteral("ASSISTANT");
    step.content = text;
    step.thinking = thinking;
    step.toolCalls = toolCalls;
    step.inputTokens = inTokens;
    step.outputTokens = outTokens;
    step.model = model;
    step.createdAt = QDateTime::currentMSecsSinceEpoch();

    appendStepToJsonl(m_activeSessionId, step);

    m_activeMeta.messageCount++;
    m_activeMeta.toolCallCount += toolCalls.size();
    m_activeMeta.totalTokens += (inTokens + outTokens);
    if (!model.isEmpty()) m_activeMeta.lastModel = model;

    saveActiveSession();
}

void AISessionManager::appendToolResultStep(const QString &toolName, const QString &argsSummary, bool success, const QString &resultOrError)
{
    if (m_activeSessionId.isEmpty()) return;

    AISessionStep step;
    step.stepIndex = m_currentStepCounter++;
    step.type = QStringLiteral("TOOL_RESULT");
    step.toolName = toolName;
    step.toolArgsSummary = argsSummary;
    step.toolSuccess = success;
    if (success) {
        step.content = resultOrError;
    } else {
        step.toolError = resultOrError;
    }
    step.createdAt = QDateTime::currentMSecsSinceEpoch();

    appendStepToJsonl(m_activeSessionId, step);
    saveActiveSession();
}

void AISessionManager::saveLlmContext(const QJsonArray &messages)
{
    if (m_activeSessionId.isEmpty()) return;

    QString ctxPath = QStringLiteral("%1/context.json").arg(sessionDirPath(m_activeSessionId));
    QFile f(ctxPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonObject root;
        root[QStringLiteral("session_id")] = m_activeSessionId;
        root[QStringLiteral("messages")] = messages;
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.close();
    }
}

QJsonArray AISessionManager::loadLlmContext(const QString &sessionId) const
{
    QString ctxPath = QStringLiteral("%1/context.json").arg(sessionDirPath(sessionId));
    QFile f(ctxPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (doc.isObject()) {
            return doc.object()[QStringLiteral("messages")].toArray();
        }
    }
    return QJsonArray();
}

QVector<AISessionStep> AISessionManager::loadTranscript(const QString &sessionId) const
{
    QVector<AISessionStep> steps;
    QString jsonlPath = QStringLiteral("%1/transcript.jsonl").arg(sessionDirPath(sessionId));
    QFile f(jsonlPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return steps;
    }

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (doc.isObject()) {
            steps.append(AISessionStep::fromJson(doc.object()));
        }
    }
    f.close();
    return steps;
}

QList<AISessionMetadata> AISessionManager::listSessions(const QString &filterProjectPath) const
{
    QMutexLocker locker(&m_mutex);
    if (filterProjectPath.isEmpty()) {
        return m_sessionIndex;
    }

    QList<AISessionMetadata> filtered;
    for (const auto &meta : m_sessionIndex) {
        if (meta.projectPath == filterProjectPath) {
            filtered.append(meta);
        }
    }
    return filtered;
}
