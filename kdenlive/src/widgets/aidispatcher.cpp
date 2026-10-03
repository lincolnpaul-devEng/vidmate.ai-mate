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

AIDispatcher::AIDispatcher(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_toolRegistry(new AIToolRegistry())
{
    loadEnvConfig();
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
                } else if (key == QStringLiteral("LLM_MODEL") || key == QStringLiteral("LLM_ANTHROPIC_MODEL") || key == QStringLiteral("LLM_OPENAI_MODEL")) {
                    if (!val.isEmpty()) m_model = val;
                }
            }
            break;
        }
    }

    // Configure default endpoint using Supabase ai-proxy if configured
    if (!m_supabaseUrl.isEmpty() && !m_supabaseAnonKey.isEmpty()) {
        m_apiUrl = QStringLiteral("%1/functions/v1/ai-proxy").arg(m_supabaseUrl);
        m_apiKey = m_supabaseAnonKey;
        qDebug() << "[AIDispatcher] Configured Supabase AI proxy endpoint:" << m_apiUrl;
    }
}

void AIDispatcher::setApiEndpoint(const QString &url) { m_apiUrl = url; }
void AIDispatcher::setApiKey(const QString &key) { m_apiKey = key; }
void AIDispatcher::setModel(const QString &model) { m_model = model; }

void AIDispatcher::setAgentSettings(const AIAgentSettings &settings)
{
    m_settings = settings;
    if (settings.editingModelId != QStringLiteral("auto") && !settings.editingModelId.isEmpty()) {
        m_model = settings.editingModelId;
    }
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

    QString settingsDirective;
    if (m_settings.planMode) {
        settingsDirective += QStringLiteral("- Plan Mode is ACTIVE: Generate a numbered step-by-step execution plan first and wait for confirmation.\n");
    }
    if (m_settings.mode == QStringLiteral("ask")) {
        settingsDirective += QStringLiteral("- Ask Mode is ACTIVE: Present proposed timeline changes for review.\n");
    } else {
        settingsDirective += QStringLiteral("- YOLO Mode is ACTIVE: Autonomous direct tool execution on the timeline.\n");
    }
    if (m_settings.cloudAssetsAccess) {
        settingsDirective += QStringLiteral("- Autonomous Cloud Assets: You may search & insert Pexels/Pixabay and Freesound assets.\n");
    }
    settingsDirective += QStringLiteral("- Motion Graphics Tier: %1\n").arg(m_settings.mgTier);
    if (m_settings.voiceModel != QStringLiteral("auto")) {
        settingsDirective += QStringLiteral("- Preferred Voiceover Model: %1\n").arg(m_settings.voiceModel);
    }

    return QStringLiteral(
        "You are Velo AI: an autonomous master writer-director and video-editing AI running natively "
        "inside Kdenlive (non-linear editor) with Natron VFX compositor as a sub-processor.\n"
        "Users can direct edits using plain English or any other language. You handle the technical "
        "complexity; the user just describes what they want.\n\n"

        "# Professional Video Editing Framework (9-Phase Lifecycle)\n"
        "1. **Intake & Analysis**: Understand source footage structure, audio tracks, pacing.\n"
        "2. **Story Structure**: Hook the viewer in first 3-5 seconds. Establish clear narrative arc.\n"
        "3. **Pacing & Cuts**: Trim dead-air pauses (>150ms). Match cut rhythm to content energy.\n"
        "4. **B-Roll & Coverage**: Insert supplementary footage for visual variety.\n"
        "5. **Graphics & VFX**: Kdenlive for overlays/filters; Natron for node-based compositing "
        "(rotoscoping, chroma-key, planar tracking, particles, light wraps).\n"
        "6. **Typography & Titles**: Clean lower-thirds, intro titles, end cards with consistent style.\n"
        "7. **Sound Design**: SFX on transitions, J/L-cuts for smooth dialogue, background music "
        "auto-ducked to -24dB during speech, voice isolation.\n"
        "8. **Subtitles & Captions**: Accurate subtitle timing synced to speech transcript.\n"
        "9. **Render QA**: Visual verification (frame snapshots), audio level check, quality probe.\n\n"

        "# Active Agent Configuration\n"
        "%1\n\n"

        "# Available Tools\n"
        "%2\n\n"

        "# Rules\n"
        "- Always explain the creative reason FIRST, then return the tool call.\n"
        "- After any destructive edit, use `get_timeline_state` to verify.\n"
        "- After visual changes, use `view_timeline_frames` to visually confirm.\n"
        "- For complex VFX (roto, tracking, chroma key, particles), delegate to `natron_vfx`.\n"
        "- For simple filters/effects (blur, glow, color), use `add_effect` (Kdenlive frei0r/MLT).\n"
        "- Group multi-step edits logically (e.g., cut + delete + move = 3 sequential actions).\n"
        "- Use `undo_last` if you detect a mistake.\n\n"

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
        "%3"
    ).arg(settingsDirective, toolDescriptions, editorState);
}

// ── Send Prompt ─────────────────────────────────────────────────────────────

void AIDispatcher::sendPrompt(const QString &prompt, const QString &targetEngine)
{
    m_lastEngine = targetEngine;
    m_lastPrompt = prompt;

    m_requestTimer.start();
    Q_EMIT requestStarted();

    // Check authentication
    QString authToken = m_apiKey;
    if (authToken.isEmpty() && AuthManager::instance()->isLoggedIn()) {
        authToken = AuthManager::instance()->accessToken();
    }

    if (authToken.isEmpty()) {
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

    QJsonObject systemMsg;
    systemMsg[QStringLiteral("role")] = QStringLiteral("system");
    systemMsg[QStringLiteral("content")] = buildSystemPrompt();

    QJsonObject userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = QStringLiteral("[Mode: %1] %2").arg(targetEngine, prompt);

    QJsonArray messages;
    messages.append(systemMsg);
    messages.append(userMsg);

    QJsonObject rootObj;
    rootObj[QStringLiteral("action")] = QStringLiteral("chat");
    rootObj[QStringLiteral("model")] = m_model;
    rootObj[QStringLiteral("messages")] = messages;
    rootObj[QStringLiteral("temperature")] = 0.2;

    // Add function-calling tools if supported
    QJsonArray tools = m_toolRegistry->toolSchemas();
    if (!tools.isEmpty()) {
        rootObj[QStringLiteral("tools")] = tools;
        rootObj[QStringLiteral("tool_choice")] = QStringLiteral("auto");
    }

    QJsonDocument doc(rootObj);
    QNetworkReply *reply = m_nam->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        slotReplyFinished(reply);
    });
}

// ── Response Processing ─────────────────────────────────────────────────────

void AIDispatcher::slotReplyFinished(QNetworkReply *reply)
{
    Q_EMIT requestFinished();

    if (!reply) {
        Q_EMIT errorOccurred(QStringLiteral("Network error: No reply received from server."));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QByteArray respData = reply->readAll();
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
            errMsg = reply->errorString();
        }

        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 401) {
            errMsg = QStringLiteral("Authentication expired or invalid (401). Please click 'Account' to sign in again.");
            AuthManager::instance()->refreshSession();
        }

        reply->deleteLater();
        Q_EMIT errorOccurred(errMsg);
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    processAiResponse(data);
}

void AIDispatcher::processAiResponse(const QByteArray &data)
{
    qint64 latencyMs = m_requestTimer.isValid() ? m_requestTimer.elapsed() : 0;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
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

    Q_EMIT responseReceived(aiText, actionObj);
}
