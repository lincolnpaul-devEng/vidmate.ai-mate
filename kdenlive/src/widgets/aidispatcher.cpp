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
    connect(m_nam, &QNetworkAccessManager::finished, this, &AIDispatcher::slotReplyFinished);
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
                << QStringLiteral("/home/lincoln/vidmate.ai-mate/kdenlive/.env");

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
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        if (callback) callback(QJsonArray());
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray respData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(respData);
            if (doc.isObject()) {
                QJsonArray modelsArr = doc.object()[QStringLiteral("data")].toArray();
                Q_EMIT modelsLoaded(modelsArr);
                if (callback) callback(modelsArr);
                return;
            }
        }
        if (callback) callback(QJsonArray());
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

    Q_EMIT requestStarted();

    QUrl url(m_apiUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8());
    }
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
    m_nam->post(request, doc.toJson());
}

// ── Response Processing ─────────────────────────────────────────────────────

void AIDispatcher::slotReplyFinished(QNetworkReply *reply)
{
    Q_EMIT requestFinished();

    if (!reply) {
        processFallbackLocalIntent(m_lastPrompt, m_lastEngine);
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        processFallbackLocalIntent(m_lastPrompt, m_lastEngine);
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    processAiResponse(data);
}

void AIDispatcher::processAiResponse(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        processFallbackLocalIntent(m_lastPrompt, m_lastEngine);
        return;
    }

    QJsonObject root = doc.object();
    QString aiText;
    QJsonObject actionObj;

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

                    Q_EMIT responseReceived(aiText.isEmpty() ? fnName : aiText, actionObj);
                    return;
                }
            }
        }
    } else if (root.contains(QStringLiteral("response"))) {
        aiText = root[QStringLiteral("response")].toString();
    }

    if (aiText.isEmpty()) {
        processFallbackLocalIntent(m_lastPrompt, m_lastEngine);
        return;
    }

    // Extract JSON block from markdown code fence
    static const QRegularExpression jsonRegex(QStringLiteral("```json\\s*([\\s\\S]*?)\\s*```"));
    auto match = jsonRegex.match(aiText);
    if (match.hasMatch()) {
        QString jsonStr = match.captured(1).trimmed();
        QJsonDocument actionDoc = QJsonDocument::fromJson(jsonStr.toUtf8());

        if (actionDoc.isObject()) {
            actionObj = actionDoc.object();
        } else if (actionDoc.isArray()) {
            // Multiple actions — take the first one, queue the rest
            QJsonArray actions = actionDoc.array();
            if (!actions.isEmpty()) {
                actionObj = actions[0].toObject();
                // TODO: queue remaining actions for sequential execution
            }
        }
    }

    Q_EMIT responseReceived(aiText, actionObj);
}

// ── Offline Fallback Intent Parser ──────────────────────────────────────────

void AIDispatcher::processFallbackLocalIntent(const QString &prompt, const QString &targetEngine)
{
    QString lower = prompt.toLower();
    QJsonObject actionObj;
    QString summary;

    // Timeline editing intents
    if (lower.contains(QStringLiteral("cut")) || lower.contains(QStringLiteral("split")) ||
        lower.contains(QStringLiteral("slice"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("cut_at_playhead");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        summary = QStringLiteral("✂️ Cutting all clips at current playhead position.");
    }
    // Delete intents
    else if (lower.contains(QStringLiteral("delete")) || lower.contains(QStringLiteral("remove clip")) ||
             lower.contains(QStringLiteral("erase"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("delete_clips");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        summary = QStringLiteral("🗑️ Deleting selected clips.");
    }
    // Speed intents
    else if (lower.contains(QStringLiteral("slow")) || lower.contains(QStringLiteral("slow motion"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("set_clip_speed");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("speed")] = 0.5;
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("🐢 Applying 0.5x slow motion to selected clip.");
    }
    else if (lower.contains(QStringLiteral("fast")) || lower.contains(QStringLiteral("speed up"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("set_clip_speed");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("speed")] = 2.0;
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("⚡ Applying 2x speed to selected clip.");
    }
    // Effect intents
    else if (lower.contains(QStringLiteral("glitch"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("add_effect");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("effect_id")] = QStringLiteral("frei0r.glitch0r");
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("🔥 Applying Glitch effect to selected clip.");
    }
    else if (lower.contains(QStringLiteral("glow"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("add_effect");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("effect_id")] = QStringLiteral("frei0r.glow");
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("✨ Applying Glow effect to selected clip.");
    }
    else if (lower.contains(QStringLiteral("blur"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("add_effect");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("effect_id")] = QStringLiteral("boxblur");
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("🌫️ Applying Box Blur effect to selected clip.");
    }
    else if (lower.contains(QStringLiteral("sepia")) || lower.contains(QStringLiteral("vintage"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("add_effect");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("effect_id")] = QStringLiteral("sepia");
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("📷 Applying Sepia/Vintage tone to selected clip.");
    }
    // Subtitle intents
    else if (lower.contains(QStringLiteral("subtitle")) || lower.contains(QStringLiteral("caption"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("add_subtitle");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("text")] = prompt;
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("📝 Adding subtitle at current position.");
    }
    // Title intents
    else if (lower.contains(QStringLiteral("title")) || lower.contains(QStringLiteral("intro"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("insert_title");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("text")] = prompt;
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("🎬 Inserting title card.");
    }
    // Undo intents
    else if (lower.contains(QStringLiteral("undo")) || lower.contains(QStringLiteral("revert"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("undo_last");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        summary = QStringLiteral("↩️ Undoing last action.");
    }
    // Transition intents
    else if (lower.contains(QStringLiteral("transition")) || lower.contains(QStringLiteral("dissolve")) ||
             lower.contains(QStringLiteral("crossfade"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("add_transition");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        QJsonObject params;
        params[QStringLiteral("transition_id")] = QStringLiteral("dissolve");
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("🔄 Adding dissolve transition.");
    }
    // VFX / Natron intents
    else if (targetEngine == QStringLiteral("natron") || lower.contains(QStringLiteral("vfx")) ||
             lower.contains(QStringLiteral("roto")) || lower.contains(QStringLiteral("green screen")) ||
             lower.contains(QStringLiteral("chroma")) || lower.contains(QStringLiteral("track")) ||
             lower.contains(QStringLiteral("particle"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("natron_vfx");
        actionObj[QStringLiteral("target")] = QStringLiteral("natron");
        QJsonObject params;
        if (lower.contains(QStringLiteral("chroma")) || lower.contains(QStringLiteral("green screen")))
            params[QStringLiteral("pipeline")] = QStringLiteral("chroma_key");
        else if (lower.contains(QStringLiteral("roto")))
            params[QStringLiteral("pipeline")] = QStringLiteral("rotoscope");
        else if (lower.contains(QStringLiteral("particle")))
            params[QStringLiteral("pipeline")] = QStringLiteral("particle_system");
        else
            params[QStringLiteral("pipeline")] = QStringLiteral("custom");
        actionObj[QStringLiteral("params")] = params;
        summary = QStringLiteral("🎭 Launching Natron VFX headless pipeline.");
    }
    // Render intents
    else if (lower.contains(QStringLiteral("render")) || lower.contains(QStringLiteral("export")) ||
             lower.contains(QStringLiteral("save video"))) {
        actionObj[QStringLiteral("action")] = QStringLiteral("render_project");
        actionObj[QStringLiteral("target")] = QStringLiteral("kdenlive");
        summary = QStringLiteral("📤 Opening render/export dialog.");
    }
    // Default: acknowledge but no action
    else {
        summary = QStringLiteral("🤖 Understood: \"%1\". Ready on %2 engine. "
                                 "Try: cut, delete, glow, glitch, blur, slow motion, undo, render, "
                                 "or describe any edit you want.").arg(prompt, targetEngine);
    }

    Q_EMIT responseReceived(summary, actionObj);
}
