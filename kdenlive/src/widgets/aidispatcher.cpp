/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * AI Network Dispatcher — Sends structured prompts to OpenAI-compatible endpoints,
 * parses responses (chat-completions or function-calling), and falls back to local
 * keyword-based intent parsing when offline.
 *
 * Includes Velo-style <editor_state> timeline snapshot and professional video
 * editing system prompt with the full tool registry.
 */

#include "aidispatcher.h"
#include "aitoolregistry.h"
#include "aimemorystore.h"
#include "authmanager.h"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/view/timelinewidget.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/model/timelinemodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QDebug>
#include <KSharedConfig>
#include <KConfigGroup>

AIDispatcher::AIDispatcher(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_toolRegistry(new AIToolRegistry())
{
    loadEnvConfig();
    loadSettings();
}

void AIDispatcher::loadSettings()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup grp(config, "AIAgentSettings");
    if (grp.exists()) {
        m_settings.mode = grp.readEntry("mode", QStringLiteral("yolo"));
        m_settings.editingModelId = grp.readEntry("editingModelId", QStringLiteral("auto"));
        m_settings.provider = grp.readEntry("provider", QStringLiteral("all"));
        m_settings.imageModel = grp.readEntry("imageModel", QStringLiteral("auto"));
        m_settings.videoModel = grp.readEntry("videoModel", QStringLiteral("auto"));
        m_settings.musicModel = grp.readEntry("musicModel", QStringLiteral("auto"));
        m_settings.voiceModel = grp.readEntry("voiceModel", QStringLiteral("auto"));
        m_settings.soundModel = grp.readEntry("soundModel", QStringLiteral("auto"));
        m_settings.mgTier = grp.readEntry("mgTier", QStringLiteral("balance"));
        m_settings.cacheMode = grp.readEntry("cacheMode", QStringLiteral("short"));
        m_settings.cloudAssetsAccess = grp.readEntry("cloudAssetsAccess", true);
        m_settings.planMode = grp.readEntry("planMode", false);
    }
    if (m_settings.editingModelId != QStringLiteral("auto") && !m_settings.editingModelId.isEmpty()) {
        m_model = m_settings.editingModelId;
    } else {
        m_model = QStringLiteral("groq/openai/gpt-oss-120b");
    }
    qDebug() << "[AIDispatcher] Loaded persisted agent settings. Active model:" << m_model;
}

void AIDispatcher::saveSettings()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup grp(config, "AIAgentSettings");
    grp.writeEntry("mode", m_settings.mode);
    grp.writeEntry("editingModelId", m_settings.editingModelId);
    grp.writeEntry("provider", m_settings.provider);
    grp.writeEntry("imageModel", m_settings.imageModel);
    grp.writeEntry("videoModel", m_settings.videoModel);
    grp.writeEntry("musicModel", m_settings.musicModel);
    grp.writeEntry("voiceModel", m_settings.voiceModel);
    grp.writeEntry("soundModel", m_settings.soundModel);
    grp.writeEntry("mgTier", m_settings.mgTier);
    grp.writeEntry("cacheMode", m_settings.cacheMode);
    grp.writeEntry("cloudAssetsAccess", m_settings.cloudAssetsAccess);
    grp.writeEntry("planMode", m_settings.planMode);
    grp.sync();
    qDebug() << "[AIDispatcher] Saved agent settings to KConfig. Active model:" << m_model;
}

void AIDispatcher::loadEnvConfig(const QString &customPath)
{
    QStringList searchPaths;
    if (!customPath.isEmpty()) searchPaths << customPath;

    // Search locations in priority order
    searchPaths << QDir::current().filePath(QStringLiteral(".env.local"))
                << QDir::current().filePath(QStringLiteral(".env"))
                << QCoreApplication::applicationDirPath() + QStringLiteral("/.env.local")
                << QCoreApplication::applicationDirPath() + QStringLiteral("/.env")
                << QStringLiteral("/home/lincoln/vidmate.ai-mate/kdenlive/.env.local")
                << QStringLiteral("/home/lincoln/vidmate.ai-mate/kdenlive/.env")
                << QStringLiteral("/home/lincoln/vidmate.ai-mate/.env.local")
                << QStringLiteral("/home/lincoln/vidmate.ai-mate/.env");

    for (const QString &path : searchPaths) {
        QFile file(path);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qDebug() << "[AIDispatcher] Loaded environment credentials from:" << path;
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

                int eqIdx = line.indexOf(QLatin1Char('='));
                if (eqIdx <= 0) continue;

                QString key = line.left(eqIdx).trimmed();
                QString val = line.mid(eqIdx + 1).trimmed();
                if ((val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"'))) ||
                    (val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\'')))) {
                    val = val.mid(1, val.length() - 2).trimmed();
                }

                if (key == QStringLiteral("VITE_SUPABASE_URL") || key == QStringLiteral("SUPABASE_URL")) {
                    m_supabaseUrl = val;
                } else if (key == QStringLiteral("VITE_SUPABASE_ANON_KEY") || key == QStringLiteral("SUPABASE_ANON_KEY")) {
                    m_supabaseAnonKey = val;
                } else if (key == QStringLiteral("SUPABASE_SERVICE_ROLE_KEY")) {
                    m_supabaseServiceKey = val;
                } else if (key == QStringLiteral("LLM_OPENAI_API_KEY") || key == QStringLiteral("OPENAI_API_KEY")) {
                    if (m_apiKey.isEmpty()) m_apiKey = val;
                } else if (key == QStringLiteral("LLM_ANTHROPIC_API_KEY") || key == QStringLiteral("ANTHROPIC_API_KEY")) {
                    if (m_apiKey.isEmpty()) m_apiKey = val;
                } else if (key == QStringLiteral("LLM_MODEL")) {
                    if (!val.isEmpty()) m_model = val;
                }
            }
            break;
        }
    }

    // Configure default endpoint using Supabase ai-proxy if configured
    if (!m_supabaseUrl.isEmpty() && !m_supabaseAnonKey.isEmpty()) {
        m_apiUrl = QStringLiteral("%1/functions/v1/ai-proxy").arg(m_supabaseUrl);
        qDebug() << "[AIDispatcher] Configured Supabase AI proxy endpoint:" << m_apiUrl;
    }
}

void AIDispatcher::setApiEndpoint(const QString &url) { m_apiUrl = url; }
void AIDispatcher::setApiKey(const QString &key) { m_apiKey = key; }

void AIDispatcher::setModel(const QString &model)
{
    QString trimmed = model.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("auto")) {
        m_settings.editingModelId = QStringLiteral("auto");
        m_model = QStringLiteral("groq/openai/gpt-oss-120b");
    } else {
        m_settings.editingModelId = trimmed;
        m_model = trimmed;
    }
    saveSettings();
    Q_EMIT agentSettingsChanged(m_settings);
}

void AIDispatcher::setAgentSettings(const AIAgentSettings &settings)
{
    m_settings = settings;
    if (settings.editingModelId != QStringLiteral("auto") && !settings.editingModelId.isEmpty()) {
        m_model = settings.editingModelId;
    } else {
        m_model = QStringLiteral("groq/openai/gpt-oss-120b");
    }
    saveSettings();
    Q_EMIT agentSettingsChanged(m_settings);
}

void AIDispatcher::fetchAvailableModels(std::function<void(const QJsonArray &models)> callback)
{
    auto parseOpenRouterDirect = [this, callback]() {
        QUrl orUrl(QStringLiteral("https://openrouter.ai/api/v1/models"));
        QNetworkRequest orReq(orUrl);
        orReq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VidMate-AI-Agent/2.0"));

        QNetworkReply *orReply = m_nam->get(orReq);
        connect(orReply, &QNetworkReply::finished, this, [this, orReply, callback]() {
            orReply->deleteLater();
            if (orReply->error() == QNetworkReply::NoError) {
                QByteArray respData = orReply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(respData);
                if (doc.isObject()) {
                    QJsonArray rawArr = doc.object()[QStringLiteral("data")].toArray();
                    if (!rawArr.isEmpty()) {
                        QJsonArray normalized;
                        for (const auto &val : rawArr) {
                            QJsonObject m = val.toObject();
                            QString id = m[QStringLiteral("id")].toString();
                            if (id.isEmpty() || id.endsWith(QStringLiteral(":batch"))) continue;

                            QJsonObject modelObj;
                            modelObj[QStringLiteral("id")] = id;
                            modelObj[QStringLiteral("name")] = m[QStringLiteral("name")].toString().isEmpty() ? id : m[QStringLiteral("name")].toString();
                            modelObj[QStringLiteral("provider")] = QStringLiteral("openrouter");
                            modelObj[QStringLiteral("contextLength")] = m[QStringLiteral("context_length")].toInt(0);
                            normalized.append(modelObj);
                        }
                        if (!normalized.isEmpty()) {
                            qDebug() << "[AIDispatcher] Loaded" << normalized.size() << "live models directly from OpenRouter.";
                            Q_EMIT modelsLoaded(normalized);
                            if (callback) callback(normalized);
                            return;
                        }
                    }
                }
            }
            qWarning() << "[AIDispatcher] Failed to load live models:" << orReply->errorString();
            Q_EMIT modelsLoaded(QJsonArray());
            if (callback) callback(QJsonArray());
        });
    };

    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        loadEnvConfig();
    }

    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        parseOpenRouterDirect();
        return;
    }

    QUrl url(QStringLiteral("%1/functions/v1/ai-proxy").arg(m_supabaseUrl));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("apikey", m_supabaseAnonKey.toUtf8());
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_supabaseAnonKey).toUtf8());

    QJsonObject body;
    body[QStringLiteral("action")] = QStringLiteral("models");

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, parseOpenRouterDirect]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray respData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(respData);
            if (doc.isObject()) {
                QJsonArray modelsArr = doc.object()[QStringLiteral("data")].toArray();
                if (!modelsArr.isEmpty()) {
                    qDebug() << "[AIDispatcher] Loaded" << modelsArr.size() << "live models from Supabase ai-proxy.";
                    Q_EMIT modelsLoaded(modelsArr);
                    if (callback) callback(modelsArr);
                    return;
                }
            }
        }
        // If Supabase ai-proxy returned error, query OpenRouter live endpoint directly
        parseOpenRouterDirect();
    });
}

// ── Editor State Snapshot ───────────────────────────────────────────────────

QString AIDispatcher::buildEditorStateSnapshot()
{
    QString state = QStringLiteral("<editor_state>\n");
    if (pCore && pCore->window() && pCore->window()->getCurrentTimeline()) {
        auto *tw = pCore->window()->getCurrentTimeline();
        auto *tc = tw->controller();
        auto tm = tw->model();
        if (tc && tm) {
            state += QStringLiteral("playhead_frame=%1 duration=%2 clips=%3\n")
                .arg(pCore->getMonitorPosition(Kdenlive::ProjectMonitor))
                .arg(tc->duration())
                .arg(tm->getClipsCount());

            state += QStringLiteral("zone_in=%1 zone_out=%2\n")
                .arg(tc->zoneIn())
                .arg(tc->zoneOut());

            QList<int> sel = tc->selection();
            QStringList selStrs;
            for (int id : sel) selStrs << QString::number(id);
            state += QStringLiteral("selected=[%1]\n").arg(selStrs.join(QStringLiteral(",")));

            // Track summary
            int trackCount = tm->getTracksCount();
            state += QStringLiteral("tracks=%1\n").arg(trackCount);
            for (int i = 0; i < trackCount && i < 20; i++) {
                int tid = tm->getTrackIndexFromPosition(i);
                state += QStringLiteral("  track[%1] id=%2 name=\"%3\" audio=%4\n")
                    .arg(i).arg(tid)
                    .arg(tm->getTrackFullName(tid))
                    .arg(tm->isAudioTrack(tid) ? QStringLiteral("yes") : QStringLiteral("no"));
            }
        }
    }
    state += QStringLiteral("</editor_state>\n");
    return state;
}

// ── System Prompt ───────────────────────────────────────────────────────────

QString AIDispatcher::buildSystemPrompt()
{
    QString toolDescriptions = m_toolRegistry->toolDescriptionsForPrompt();
    QString editorState = buildEditorStateSnapshot();
    QString memories = AIMemoryStore::instance()->formattedForPrompt();

    QString settingsDirective;
    if (m_settings.planMode) {
        settingsDirective += QStringLiteral("- Plan Mode is ACTIVE: Generate a numbered step-by-step execution plan first and wait for confirmation.\n");
    }
    if (m_settings.mode == QStringLiteral("ask")) {
        settingsDirective += QStringLiteral("- Ask Mode is ACTIVE: Present proposed timeline changes for review.\n");
    } else {
        settingsDirective += QStringLiteral("- YOLO Mode is ACTIVE: Autonomous direct multi-step tool execution on the timeline.\n");
    }
    if (m_settings.cloudAssetsAccess) {
        settingsDirective += QStringLiteral("- Autonomous Cloud Assets: You may search & insert Pexels/Pixabay and Freesound assets.\n");
    }
    settingsDirective += QStringLiteral("- Motion Graphics Tier: %1\n").arg(m_settings.mgTier);
    if (m_settings.voiceModel != QStringLiteral("auto")) {
        settingsDirective += QStringLiteral("- Preferred Voiceover Model: %1\n").arg(m_settings.voiceModel);
    }

    return QStringLiteral(
        "You are Velo AI: an autonomous master writer-director and video-editing AI agent running natively "
        "inside Kdenlive (non-linear editor) with Natron VFX compositor as a sub-processor.\n"
        "You operate in an autonomous iterative multi-step goal loop. When directed by the user, execute "
        "tools sequentially. After each action, you receive the tool execution observation and updated timeline state. "
        "Continue executing actions until the objective is completely achieved, then provide a concluding summary without further tool calls.\n\n"

        "# Professional Video Editing Framework (9-Phase Lifecycle)\n"
        "1. **Intake & Analysis**: Understand source footage structure, audio tracks, pacing.\n"
        "2. **Story Structure**: Hook the viewer in first 3-5 seconds. Establish clear narrative arc.\n"
        "3. **Pacing & Cuts**: Trim dead-air pauses (>150ms). Match cut rhythm to content energy.\n"
        "4. **B-Roll & Coverage**: Insert supplementary footage for visual variety.\n"
        "5. **Graphics & VFX**: Kdenlive for overlays/filters; Natron for node-based compositing; Procedural GLSL for shaders.\n"
        "6. **Typography & Titles**: Clean lower-thirds, intro titles, end cards with consistent style.\n"
        "7. **Sound Design**: SFX on transitions, J/L-cuts for smooth dialogue, background music "
        "auto-ducked to -24dB during speech, voice isolation.\n"
        "8. **Subtitles & Captions**: Accurate subtitle timing synced to speech transcript.\n"
        "9. **Render QA**: Visual verification (frame snapshots), audio level check, quality probe.\n\n"

        "# Persistent Agent Memory & User Rules\n"
        "%1\n\n"

        "# Active Agent Configuration\n"
        "%2\n\n"

        "# Available Tools\n"
        "%3\n\n"

        "# Autonomous Agent Rules\n"
        "- Explain your immediate creative intent concisely, then return the JSON tool action block.\n"
        "- You can execute one tool at a time (or array of sequential tools) to accomplish the goal.\n"
        "- When you have completed the user's overall goal, do NOT return any more tool calls; simply summarize the edits performed.\n"
        "- After any destructive edit, use `get_timeline_state` to verify.\n"
        "- Group multi-step edits logically (e.g., cut + delete + move = 3 sequential actions).\n"
        "- Use `write_memory` / `read_memory` to persist and recall user preferences and styles.\n\n"

        "# Output Format\n"
        "Answer concisely with your creative reasoning, then return a JSON action block:\n"
        "```json\n"
        "{\"action\": \"<tool_name>\", \"target\": \"kdenlive\"|\"natron\", \"params\": {...}}\n"
        "```\n"
        "For multiple sequential actions, return an array:\n"
        "```json\n"
        "[{\"action\": \"...\", ...}, {\"action\": \"...\", ...}]\n"
        "```\n\n"

        "# Current Editor State\n"
        "%4"
    ).arg(memories, settingsDirective, toolDescriptions, editorState);
}

// ── Multi-Step Goal Control ──────────────────────────────────────────────────

void AIDispatcher::clearConversation()
{
    m_conversationMessages = QJsonArray();
    m_currentStep = 0;
    m_goalActive = false;
    m_currentGoal.clear();
}

void AIDispatcher::cancelCurrentGoal()
{
    if (m_goalActive) {
        m_goalActive = false;
        Q_EMIT requestFinished();
        Q_EMIT goalFinished(QStringLiteral("Goal cancelled by user."));
    }
}

void AIDispatcher::sendPrompt(const QString &prompt, const QString &targetEngine)
{
    m_lastEngine = targetEngine;
    m_lastPrompt = prompt;
    m_currentGoal = prompt;
    m_currentStep = 1;
    m_goalActive = true;

    m_conversationMessages = QJsonArray();

    QJsonObject systemMsg;
    systemMsg[QStringLiteral("role")] = QStringLiteral("system");
    systemMsg[QStringLiteral("content")] = buildSystemPrompt();
    m_conversationMessages.append(systemMsg);

    QJsonObject userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = QStringLiteral("[Mode: %1] %2").arg(targetEngine, prompt);
    m_conversationMessages.append(userMsg);

    Q_EMIT goalStarted(prompt);
    sendCurrentMessages();
}

void AIDispatcher::feedObservationAndContinue(const QString &actionName, const QString &observationResult, bool success)
{
    if (!m_goalActive) {
        return;
    }

    Q_EMIT goalStepFinished(m_currentStep, m_maxSteps, actionName, success);

    if (m_currentStep >= m_maxSteps) {
        m_goalActive = false;
        Q_EMIT goalFinished(QStringLiteral("Goal step budget completed (%1 steps executed).").arg(m_maxSteps));
        return;
    }

    m_currentStep++;

    // Prune oldest messages if conversation exceeds 20 items to prevent context window overflow
    if (m_conversationMessages.size() > 20) {
        QJsonArray pruned;
        pruned.append(m_conversationMessages.at(0)); // keep system prompt
        for (int i = m_conversationMessages.size() - 10; i < m_conversationMessages.size(); ++i) {
            pruned.append(m_conversationMessages.at(i));
        }
        m_conversationMessages = pruned;
    }

    // Append observation message with live timeline snapshot
    QJsonObject obsMsg;
    obsMsg[QStringLiteral("role")] = QStringLiteral("user");
    obsMsg[QStringLiteral("content")] = QStringLiteral(
        "[Tool Observation: '%1'] Status: %2\nResult: %3\n\nUpdated Timeline State:\n%4\n\n"
        "Continue towards achieving user goal: \"%5\". Take the next step or conclude if complete."
    ).arg(actionName, success ? QStringLiteral("Success") : QStringLiteral("Failed"), observationResult, buildEditorStateSnapshot(), m_currentGoal);
    m_conversationMessages.append(obsMsg);

    sendCurrentMessages();
}

void AIDispatcher::sendCurrentMessages()
{
    m_requestTimer.start();
    Q_EMIT requestStarted();

    // Check authentication
    QString authToken;
    if (AuthManager::instance()->isLoggedIn() && !AuthManager::instance()->accessToken().isEmpty()) {
        authToken = AuthManager::instance()->accessToken();
    } else if (!m_supabaseAnonKey.isEmpty()) {
        authToken = m_supabaseAnonKey;
    } else if (!m_apiKey.isEmpty()) {
        authToken = m_apiKey;
    }

    if (authToken.isEmpty()) {
        m_goalActive = false;
        Q_EMIT requestFinished();
        Q_EMIT errorOccurred(QStringLiteral("Authentication required. Please click 'Account' to sign in to your Velo/VidMate account."));
        return;
    }

    QUrl url(m_apiUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(authToken).toUtf8());

    if (!m_supabaseAnonKey.isEmpty()) {
        request.setRawHeader("apikey", m_supabaseAnonKey.toUtf8());
    }

    QString modelToSend = m_model;
    if (modelToSend.isEmpty() || modelToSend == QStringLiteral("auto")) {
        modelToSend = QStringLiteral("groq/openai/gpt-oss-120b");
    }
    qDebug() << "[AIDispatcher] Iteration step" << m_currentStep << "using model:" << modelToSend;

    QJsonObject rootObj;
    rootObj[QStringLiteral("action")] = QStringLiteral("chat");
    rootObj[QStringLiteral("model")] = modelToSend;
    rootObj[QStringLiteral("messages")] = m_conversationMessages;
    rootObj[QStringLiteral("temperature")] = 0.2;

    // Add function-calling tools if supported
    QJsonArray tools = m_toolRegistry->toolSchemas();
    if (!tools.isEmpty()) {
        rootObj[QStringLiteral("tools")] = tools;
        rootObj[QStringLiteral("tool_choice")] = QStringLiteral("auto");
    }

    QJsonDocument doc(rootObj);
    QNetworkReply *reply = m_nam->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, modelToSend]() {
        slotReplyFinished(reply, modelToSend);
    });
}

// ── Response Processing ─────────────────────────────────────────────────────

void AIDispatcher::slotReplyFinished(QNetworkReply *reply, const QString &modelUsed)
{
    if (!reply) {
        m_goalActive = false;
        Q_EMIT requestFinished();
        Q_EMIT errorOccurred(QStringLiteral("Network error: No reply received from server."));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray respData = reply->readAll();
        reply->deleteLater();

        // If the selected model failed on ai-proxy (e.g. HTTP 401/404/500), automatically fallback to primary working model
        if (modelUsed != QStringLiteral("groq/openai/gpt-oss-120b") && (statusCode == 401 || statusCode == 404 || statusCode == 400 || statusCode == 500)) {
            qWarning() << "[AIDispatcher] Model" << modelUsed << "returned HTTP" << statusCode << ". Transparently falling back to verified primary model: groq/openai/gpt-oss-120b";
            m_model = QStringLiteral("groq/openai/gpt-oss-120b");
            m_settings.editingModelId = m_model;
            saveSettings();
            Q_EMIT agentSettingsChanged(m_settings);
            sendCurrentMessages();
            return;
        }

        m_goalActive = false;
        Q_EMIT requestFinished();

        QString errMsg;
        QJsonDocument doc = QJsonDocument::fromJson(respData);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains(QStringLiteral("error"))) {
                QJsonValue errVal = obj[QStringLiteral("error")];
                if (errVal.isObject()) {
                    errMsg = errVal.toObject()[QStringLiteral("message")].toString();
                } else {
                    errMsg = errVal.toString();
                }
            } else if (obj.contains(QStringLiteral("message"))) {
                errMsg = obj[QStringLiteral("message")].toString();
            }
        }

        if (errMsg.isEmpty()) {
            if (statusCode == 401) {
                if (!AuthManager::instance()->isLoggedIn()) {
                    errMsg = QStringLiteral("Please click 'Account' to sign in with your Supabase account.");
                } else {
                    errMsg = QStringLiteral("AI Proxy authentication failed (HTTP 401).");
                }
            } else if (statusCode == 402 || statusCode == 429) {
                errMsg = QStringLiteral("AI service quota exceeded or rate limited. Please try again in a moment.");
            } else if (statusCode == 404) {
                errMsg = QStringLiteral("Model not found on AI Proxy (HTTP 404).");
            } else {
                errMsg = reply->errorString();
            }
        } else if (statusCode == 401 && !AuthManager::instance()->isLoggedIn()) {
            errMsg = QStringLiteral("Please click 'Account' to sign in with your Supabase account.");
        }

        Q_EMIT errorOccurred(errMsg);
        return;
    }

    Q_EMIT requestFinished();
    QByteArray data = reply->readAll();
    reply->deleteLater();
    processAiResponse(data);
}

void AIDispatcher::processAiResponse(const QByteArray &data)
{
    qint64 latencyMs = m_requestTimer.isValid() ? m_requestTimer.elapsed() : 0;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        m_goalActive = false;
        Q_EMIT errorOccurred(QStringLiteral("Invalid JSON response received from AI model."));
        return;
    }

    QJsonObject root = doc.object();
    QString aiText;
    QJsonObject actionObj;

    int totalTokens = 0;
    if (root.contains(QStringLiteral("usage"))) {
        QJsonObject usage = root[QStringLiteral("usage")].toObject();
        totalTokens = usage[QStringLiteral("total_tokens")].toInt(0);
        if (totalTokens == 0) {
            totalTokens = usage[QStringLiteral("prompt_tokens")].toInt(0) + usage[QStringLiteral("completion_tokens")].toInt(0);
        }
    }

    QString modelUsed = root.contains(QStringLiteral("model")) ? root[QStringLiteral("model")].toString() : m_model;

    if (root.contains(QStringLiteral("choices"))) {
        QJsonArray choices = root[QStringLiteral("choices")].toArray();
        if (!choices.isEmpty()) {
            QJsonObject choice0 = choices[0].toObject();
            QJsonObject message = choice0[QStringLiteral("message")].toObject();
            aiText = message[QStringLiteral("content")].toString();

            // Check for function-calling tool_calls
            if (message.contains(QStringLiteral("tool_calls"))) {
                QJsonArray toolCalls = message[QStringLiteral("tool_calls")].toArray();
                if (!toolCalls.isEmpty()) {
                    QJsonObject toolCall = toolCalls[0].toObject();
                    QJsonObject function = toolCall[QStringLiteral("function")].toObject();
                    QString fnName = function[QStringLiteral("name")].toString();
                    QString fnArgs = function[QStringLiteral("arguments")].toString();
                    QJsonDocument argsDoc = QJsonDocument::fromJson(fnArgs.toUtf8());

                    actionObj[QStringLiteral("action")] = fnName;
                    actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
                    actionObj[QStringLiteral("params")] = argsDoc.isObject() ? argsDoc.object() : QJsonObject();

                    if (totalTokens <= 0) {
                        totalTokens = qMax(1, (m_lastPrompt.length() + fnName.length() + fnArgs.length()) / 4);
                    }
                    Q_EMIT metricsUpdated(totalTokens, latencyMs, modelUsed);

                    QJsonObject assistantMsg;
                    assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
                    assistantMsg[QStringLiteral("content")] = aiText.isEmpty() ? fnName : aiText;
                    m_conversationMessages.append(assistantMsg);

                    Q_EMIT goalStepStarted(m_currentStep, m_maxSteps, fnName);
                    Q_EMIT responseReceived(aiText.isEmpty() ? fnName : aiText, actionObj);
                    return;
                }
            }
        }
    } else if (root.contains(QStringLiteral("response"))) {
        aiText = root[QStringLiteral("response")].toString();
    } else if (root.contains(QStringLiteral("content"))) {
        aiText = root[QStringLiteral("content")].toString();
    }

    if (aiText.isEmpty()) {
        m_goalActive = false;
        if (root.contains(QStringLiteral("error"))) {
            QJsonValue errVal = root[QStringLiteral("error")];
            QString msg = errVal.isObject() ? errVal.toObject()[QStringLiteral("message")].toString() : errVal.toString();
            Q_EMIT errorOccurred(msg);
        } else {
            Q_EMIT errorOccurred(QStringLiteral("Empty content returned from AI model."));
        }
        return;
    }

    if (totalTokens <= 0) {
        totalTokens = qMax(1, (m_lastPrompt.length() + aiText.length()) / 4);
    }
    Q_EMIT metricsUpdated(totalTokens, latencyMs, modelUsed);

    // Extract JSON block from markdown code fence if present
    static const QRegularExpression jsonRegex(QStringLiteral("```json\\s*([\\s\\S]*?)\\s*```"));
    auto match = jsonRegex.match(aiText);
    if (match.hasMatch()) {
        QString jsonStr = match.captured(1).trimmed();
        QJsonDocument actionDoc = QJsonDocument::fromJson(jsonStr.toUtf8());

        if (actionDoc.isObject()) {
            actionObj = actionDoc.object();
        } else if (actionDoc.isArray()) {
            QJsonArray actions = actionDoc.array();
            if (!actions.isEmpty()) {
                actionObj = actions[0].toObject();
            }
        }
    }

    QJsonObject assistantMsg;
    assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
    assistantMsg[QStringLiteral("content")] = aiText;
    m_conversationMessages.append(assistantMsg);

    if (!actionObj.isEmpty()) {
        QString actionName = actionObj[QStringLiteral("action")].toString();
        Q_EMIT goalStepStarted(m_currentStep, m_maxSteps, actionName);
        Q_EMIT responseReceived(aiText, actionObj);
    } else {
        // Goal achieved or direct textual response
        m_goalActive = false;
        Q_EMIT responseReceived(aiText, actionObj);
        Q_EMIT goalFinished(aiText);
    }
}
