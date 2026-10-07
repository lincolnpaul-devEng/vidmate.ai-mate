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
#include "bin/model/subtitlemodel.hpp"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "bin/abstractprojectitem.h"
#include "bin/projectfolder.h"
#include "core.h"
#include "mainwindow.h"
#include "timeline2/view/timelinewidget.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/model/timelinemodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/model/trackmodel.hpp"
#include <algorithm>
#include <QTextDocument>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
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
    // 1. Check system environment variables first
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains(QStringLiteral("VITE_SUPABASE_URL"))) m_supabaseUrl = env.value(QStringLiteral("VITE_SUPABASE_URL"));
    else if (env.contains(QStringLiteral("SUPABASE_URL"))) m_supabaseUrl = env.value(QStringLiteral("SUPABASE_URL"));

    if (env.contains(QStringLiteral("VITE_SUPABASE_ANON_KEY"))) m_supabaseAnonKey = env.value(QStringLiteral("VITE_SUPABASE_ANON_KEY"));
    else if (env.contains(QStringLiteral("SUPABASE_ANON_KEY"))) m_supabaseAnonKey = env.value(QStringLiteral("SUPABASE_ANON_KEY"));

    if (env.contains(QStringLiteral("SUPABASE_SERVICE_ROLE_KEY"))) m_supabaseServiceKey = env.value(QStringLiteral("SUPABASE_SERVICE_ROLE_KEY"));

    // 2. Search candidate .env and .env.local files in priority order
    QStringList searchPaths;
    if (!customPath.isEmpty()) searchPaths << customPath;

    // Search current working directory and ancestors up to 6 levels
    QDir currDir = QDir::current();
    for (int i = 0; i < 6; ++i) {
        searchPaths << currDir.filePath(QStringLiteral(".env.local"))
                    << currDir.filePath(QStringLiteral(".env"))
                    << currDir.filePath(QStringLiteral("kdenlive/.env.local"))
                    << currDir.filePath(QStringLiteral("kdenlive/.env"));
        if (!currDir.cdUp()) break;
    }

    // Search application binary directory and ancestors up to 6 levels (e.g. build/bin -> build -> kdenlive -> repo root)
    QDir appDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        searchPaths << appDir.filePath(QStringLiteral(".env.local"))
                    << appDir.filePath(QStringLiteral(".env"))
                    << appDir.filePath(QStringLiteral("kdenlive/.env.local"))
                    << appDir.filePath(QStringLiteral("kdenlive/.env"));
        if (!appDir.cdUp()) break;
    }

    // User standard config & data paths
    searchPaths << QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral(".env.local"))
                << QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral(".env"))
                << QStandardPaths::locate(QStandardPaths::ConfigLocation, QStringLiteral("kdenlive/.env.local"))
                << QStandardPaths::locate(QStandardPaths::ConfigLocation, QStringLiteral("kdenlive/.env"))
                << QDir::home().filePath(QStringLiteral(".config/kdenlive/.env.local"))
                << QDir::home().filePath(QStringLiteral(".config/kdenlive/.env"))
                << QDir::home().filePath(QStringLiteral(".env.local"))
                << QDir::home().filePath(QStringLiteral(".env"));

    searchPaths.removeDuplicates();
    searchPaths.removeAll(QString());

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

                if ((key == QStringLiteral("VITE_SUPABASE_URL") || key == QStringLiteral("SUPABASE_URL")) && m_supabaseUrl.isEmpty()) {
                    m_supabaseUrl = val;
                } else if ((key == QStringLiteral("VITE_SUPABASE_ANON_KEY") || key == QStringLiteral("SUPABASE_ANON_KEY")) && m_supabaseAnonKey.isEmpty()) {
                    m_supabaseAnonKey = val;
                } else if (key == QStringLiteral("SUPABASE_SERVICE_ROLE_KEY") && m_supabaseServiceKey.isEmpty()) {
                    m_supabaseServiceKey = val;
                }
            }
            if (!m_supabaseUrl.isEmpty() && !m_supabaseAnonKey.isEmpty()) {
                break;
            }
        }
    }

    if (!m_supabaseUrl.isEmpty()) {
        m_apiUrl = QStringLiteral("%1/functions/v1/ai-proxy").arg(m_supabaseUrl);
        qDebug() << "[AIDispatcher] Configured Supabase AI proxy endpoint:" << m_apiUrl;
    } else {
        qDebug() << "[AIDispatcher] No Supabase credentials loaded from environment search paths.";
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
        if (!m_openRouterKey.isEmpty()) {
            orReq.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_openRouterKey).toUtf8());
        }

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
    if (!m_openRouterKey.isEmpty()) {
        req.setRawHeader("x-openrouter-key", m_openRouterKey.toUtf8());
    } else if (!m_apiKey.isEmpty()) {
        req.setRawHeader("x-openrouter-key", m_apiKey.toUtf8());
    }
    if (!m_groqKey.isEmpty()) {
        req.setRawHeader("x-groq-key", m_groqKey.toUtf8());
    }

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

            // Timeline clips enumeration with exact frame coordinates
            int clipCount = tm->getClipsCount();
            state += QStringLiteral("<timeline_clips count=%1>\n").arg(clipCount);
            struct ClipSummary {
                int id;
                int trackId;
                QString trackName;
                QString clipName;
                int start;
                int duration;
                int in;
                int out;
                QString binId;
            };
            std::vector<ClipSummary> allClips;
            for (int i = 0; i < trackCount; i++) {
                int tid = tm->getTrackIndexFromPosition(i);
                auto cids = tm->getTrackClips(tid);
                QString trackName = tm->getTrackFullName(tid);
                for (int cid : cids) {
                    auto inOut = tm->getClipInOut(cid);
                    allClips.push_back({
                        cid,
                        tid,
                        trackName,
                        tm->getClipName(cid),
                        tm->getClipPosition(cid),
                        tm->getClipPlaytime(cid),
                        inOut.first,
                        inOut.second,
                        tm->getClipBinId(cid)
                    });
                }
            }
            std::sort(allClips.begin(), allClips.end(), [](const ClipSummary &a, const ClipSummary &b) {
                if (a.start != b.start) return a.start < b.start;
                return a.trackId < b.trackId;
            });
            for (const auto &c : allClips) {
                state += QStringLiteral("  [clip %1] track=%2 (\"%3\") \"%4\" @%5 +%6f (in=%7 out=%8, bin=%9)\n")
                    .arg(c.id)
                    .arg(c.trackId)
                    .arg(c.trackName)
                    .arg(c.clipName)
                    .arg(c.start)
                    .arg(c.duration)
                    .arg(c.in)
                    .arg(c.out)
                    .arg(c.binId);
            }
            state += QStringLiteral("</timeline_clips>\n");

            // Timeline Subtitles / Captions (Full Text & Timestamps for NLP analysis)
            auto subModel = tm->getSubtitleModel();
            if (subModel && subModel->count() > 0) {
                const auto allSubs = subModel->getAllSubtitles();
                state += QStringLiteral("<timeline_subtitles count=%1>\n").arg(allSubs.size());
                int count = 0;
                for (const auto &s : allSubs) {
                    double startSec = s.first.second.seconds();
                    double endSec = s.second.endTime().seconds();
                    QString txt = s.second.text().simplified();
                    if (!txt.isEmpty()) {
                        state += QStringLiteral("  [%1s - %2s] %3\n")
                            .arg(startSec, 0, 'f', 2)
                            .arg(endSec, 0, 'f', 2)
                            .arg(txt);
                        count++;
                        if (count >= 50) {
                            state += QStringLiteral("  ... (%1 more subtitles)\n").arg(allSubs.size() - count);
                            break;
                        }
                    }
                }
                state += QStringLiteral("</timeline_subtitles>\n");
            }

            // Project Bin Folders & Clips structure
            if (pCore->projectItemModel()) {
                auto binModel = pCore->projectItemModel();
                auto root = binModel->getRootFolder();
                if (root) {
                    QStringList folderSummaries;
                    std::function<void(const std::shared_ptr<TreeItem>&, const QString&)> collectFolders =
                        [&](const std::shared_ptr<TreeItem> &item, const QString &indent) {
                            for (int i = 0; i < item->childCount(); ++i) {
                                auto child = item->child(i);
                                auto projItem = std::dynamic_pointer_cast<AbstractProjectItem>(child);
                                if (projItem && projItem->itemType() == AbstractProjectItem::FolderItem) {
                                    int clipCount = 0;
                                    QStringList clipNames;
                                    for (int j = 0; j < child->childCount(); ++j) {
                                        auto subChild = child->child(j);
                                        auto subItem = std::dynamic_pointer_cast<AbstractProjectItem>(subChild);
                                        if (subItem && subItem->itemType() == AbstractProjectItem::ClipItem) {
                                            clipCount++;
                                            if (clipNames.size() < 4) {
                                                clipNames << QStringLiteral("\"%1\"").arg(subItem->name());
                                            }
                                        }
                                    }
                                    QString preview = clipNames.isEmpty() ? QStringLiteral("empty") : clipNames.join(QStringLiteral(", "));
                                    if (clipCount > 4) preview += QStringLiteral(" (+%1 more)").arg(clipCount - 4);
                                    folderSummaries << QStringLiteral("  %1[folder %2] \"%3\" (%4 clips: %5)")
                                        .arg(indent, projItem->clipId(), projItem->name()).arg(clipCount).arg(preview);
                                    collectFolders(child, indent + QStringLiteral("  "));
                                }
                            }
                        };
                    collectFolders(root, QString());
                    if (!folderSummaries.isEmpty()) {
                        state += QStringLiteral("<project_bin_folders count=%1>\n%2\n</project_bin_folders>\n")
                            .arg(folderSummaries.size())
                            .arg(folderSummaries.join(QStringLiteral("\n")));
                    }
                }

                // Transcribed Clips (kdenlive:speech)
                QStringList transcriptSummaries;
                const auto clipIds = pCore->projectItemModel()->getAllClipIds();
                for (const auto &id : clipIds) {
                    auto pClip = pCore->projectItemModel()->getClipByBinID(id);
                    if (pClip) {
                        QString speechHtml = pClip->getProducerProperty(QStringLiteral("kdenlive:speech"));
                        if (!speechHtml.isEmpty()) {
                            QTextDocument doc;
                            doc.setHtml(speechHtml);
                            QString plain = doc.toPlainText().simplified();
                            if (!plain.isEmpty()) {
                                if (plain.length() > 300) {
                                    plain = plain.left(300) + QStringLiteral("...");
                                }
                                transcriptSummaries << QStringLiteral("  clip \"%1\" (id=%2): \"%3\"")
                                    .arg(pClip->clipName(), pClip->binId(), plain);
                            }
                        }
                    }
                }
                if (!transcriptSummaries.isEmpty()) {
                    state += QStringLiteral("<transcribed_clips>\n") + transcriptSummaries.join(QStringLiteral("\n")) + QStringLiteral("\n</transcribed_clips>\n");
                }
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
        "- **Visual Verification of Edits**: Whenever you apply an edit (such as background removal, rotoscoping, color grading, title placement, transition, or Natron VFX), call `verify_edit_visually` or `view_timeline_frames` on the modified frame(s). Check the returned metrics (luminance, black frames, alpha edge quality, overexposure) and visual data. If flaws or edge artifacts occur, autonomously adjust parameters and re-verify before finalizing.\n"
        "- Use `write_memory` / `read_memory` to persist and recall user preferences and styles.\n"
        "- For `track_object` / `natron_vfx`: When asked to track an object, blur an element/face/plate, rotoscope a subject, or perform advanced VFX compositing, use `track_object` or `natron_vfx` targeting `natron`. This automatically updates and synchronizes Natron's Node Graph, Curve Editor, and Dope Sheet directly in Kdenlive's workspace and renders the result.\n"
        "- For `generate_glsl_shader`: Always supply valid GLSL fragment code using ShaderToy signature `void mainImage(out vec4 fragColor, in vec2 fragCoord)` with uniforms `iResolution` (vec3) and `iTime` (float), or choose a preset ('neon_grid', 'plasma_energy', 'gradient_flow', 'starfield_warp', 'cyber_matrix'). Ensure proper GLSL type safety (e.g. float constants like 0.0 vs int 0, matching function signatures) and specify `track_id` and `playhead_frame` for B-roll placement.\n\n"

        "# Effects & Compositions Quick-Reference (Kdenlive Native Controls)\n"
        "- **Transform / Position / Zoom / Motion**: `add_effect(clip_id, \"transform\")` or `set_effect_parameter(clip_id, \"transform\", \"rect\", \"x y w h opacity\")`.\n"
        "  - Controls: `rect` (format: \"x y width height opacity\", e.g. \"0 0 1920 1080 1.0\" or object `{\"x\":0,\"y\":0,\"width\":1920,\"height\":1080,\"opacity\":1.0}`), `rotation` (angle in 10ths of degree: 900 = 90°), `opacity` (0.0 to 1.0), `compositing` (blend mode).\n"
        "- **Color Grading**: `add_effect(clip_id, \"lift_gamma_gain\")`.\n"
        "  - Controls: `lift_r`, `lift_g`, `lift_b` (shadows), `gamma_r`, `gamma_g`, `gamma_b` (midtones), `gain_r`, `gain_g`, `gain_b` (highlights).\n"
        "- **Blur & Glow**: `add_effect(clip_id, \"boxblur\")` (params: `boxblur_hori`, `boxblur_vert`), `frei0r.glow` (param: `blur`), `vignette` (`radius`, `smooth`).\n"
        "- **Audio & Ducking**: `set_volume(clip_id, volume_db)`, `audio_ducking(speech_track, music_track, duck_level_db)`.\n"
        "- **Transitions & Compositions**: `add_transition(track_id, position, \"dissolve\"|\"wipe\"|\"slide\"|\"composite\", duration)` or `add_mix`.\n"
        "  - Tweak transitions via `set_effect_parameter(composition_id=..., param_name=\"...\", value=\"...\")`.\n"
        "- **Inspection & Verification Tools**:\n"
        "  - `get_available_effects(query=\"...\")` to query available MLT, Frei0r, and Kdenlive effects.\n"
        "  - `get_available_compositions(query=\"...\")` to query transitions and wipe types.\n"
        "  - `get_effect_parameters(clip_id=..., effect_id=\"...\")` or `get_effect_parameters(composition_id=...)` to inspect live parameter schemas, limits, and current values before tweaking.\n"
        "  - `probe_media(source=\"...\")` to inspect duration, resolution, codecs, and quality risks on remote stock media before downloading.\n\n"

        "# SFX Intelligence & Beat-Synchronized Editing (Beat This! + Local Audio Generation)\n"
        "- **Beat & Transient Intelligence (`detect_beats`)**: Use `detect_beats` on the background music track or audio clip to detect BPM tempo, musical downbeats (bars/drops), and rhythmic pulses using the native Beat This transformer neural network. Pass `generate_guides: true` to instantly place timeline guide markers at every beat or downbeat on the timeline ruler for snapping.\n"
        "- **On-Device Cinematic SFX Generation (`generate_local_sfx`)**: Synthesize impacts, whooshes, risers, sub drops, and glitches locally without external servers. Specify `prompt` (e.g. 'cinematic impact sub boom', 'fast whoosh transition swoosh', 'tension riser build-up', 'sub drop 808', 'cyber digital glitch') and `target_beat_frame`. The engine calculates the sound's peak transient offset (`peak_offset_seconds`) and automatically shifts the clip's start frame so its peak lands EXACTLY on `target_beat_frame` (e.g. aligned with a beat marker or cut).\n"
        "- **Cinematic Fast Cuts & Audio-Visual Sync**: When creating fast cuts or cinematic sequences, first run `detect_beats` to retrieve downbeat/beat timestamps, cut video clips on the beat frames, place transitions (whooshes/swishes) peaking at the cut, and place heavy impacts on the downbeats.\n\n"

        "# Project Organization & Bin Folder Hierarchy (Production Standard)\n"
        "- **Real Bin Folders (`create_bin_folder`, `list_bin_folders`, `move_bin_clip_to_folder`)**: For production asset management, ALWAYS create and use real Project Bin folders in Kdenlive instead of writing memory placeholders. When asked to create folders (e.g. SFX, BGM, Voice, Footage, Titles, VFX, Exports), call `create_bin_folder` passing comma-separated names (or a JSON array of names) in one atomic tool call. Use `move_bin_clip_to_folder` to organize clips into their respective bins as assets are imported or generated. Inspect `<project_bin_folders>` in `<editor_state>` to observe current bins and clip allocations.\n\n"

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

void AIDispatcher::setConversationMessages(const QJsonArray &messages)
{
    m_conversationMessages = messages;
    m_goalActive = false;
    m_currentGoal.clear();
    m_currentStep = 0;
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

    QString runId = QStringLiteral("run_%1").arg(QDateTime::currentMSecsSinceEpoch());
    m_agentRunLedger = agent_cpp::RunLedger(runId.toStdString(), prompt.toStdString());

    // Maintain persistent multi-turn conversation history across prompt submissions
    if (m_conversationMessages.isEmpty()) {
        QJsonObject systemMsg;
        systemMsg[QStringLiteral("role")] = QStringLiteral("system");
        systemMsg[QStringLiteral("content")] = buildSystemPrompt();
        m_conversationMessages.append(systemMsg);
    } else {
        // Refresh the system prompt (index 0) with latest editor state & memories
        QJsonObject systemMsg;
        systemMsg[QStringLiteral("role")] = QStringLiteral("system");
        systemMsg[QStringLiteral("content")] = buildSystemPrompt();
        m_conversationMessages[0] = systemMsg;
    }

    QJsonObject userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = QStringLiteral("[Mode: %1] %2").arg(targetEngine, prompt);
    m_conversationMessages.append(userMsg);

    // Item B: Dynamic context compaction when approaching threshold
    maybeCompactConversation();

    Q_EMIT goalStarted(prompt);
    sendCurrentMessages();
}

void AIDispatcher::feedObservationAndContinue(const QString &actionName, const QString &observationResult, bool success)
{
    if (!m_goalActive) {
        return;
    }

    // Item C: Two-pass tool result compaction directly from agent_cpp
    std::string rawOutputStd = observationResult.toStdString();
    std::string compactedStd = agent_cpp::compact_tool_result(rawOutputStd, m_contextBudget.max_tool_result_chars);
    QString compactedObservation = QString::fromStdString(compactedStd);

    // Item D: Record action into agent_cpp persistent Run Ledger
    m_agentRunLedger.record_tool_action(
        QStringLiteral("call_%1").arg(m_currentStep).toStdString(),
        actionName.toStdString(),
        std::string(),
        compactedStd,
        success ? "success" : "error",
        m_currentStep
    );

    QString ledgerDir = QDir::homePath() + QStringLiteral("/.cache/kdenlive/agent_ledgers");
    QDir().mkpath(ledgerDir);
    QString ledgerPath = ledgerDir + QStringLiteral("/%1.json").arg(QString::fromStdString(m_agentRunLedger.get_run_id()));
    m_agentRunLedger.save_to_file(ledgerPath.toStdString());

    Q_EMIT goalStepFinished(m_currentStep, m_maxSteps, actionName, success);

    if (m_currentStep >= m_maxSteps) {
        m_goalActive = false;
        Q_EMIT goalFinished(QStringLiteral("Goal step budget completed (%1 steps executed).").arg(m_maxSteps));
        return;
    }

    m_currentStep++;

    // Item B: Dynamic context compaction check before inserting observation
    maybeCompactConversation();

    // Append observation message with live timeline snapshot
    QJsonObject obsMsg;
    obsMsg[QStringLiteral("role")] = QStringLiteral("user");
    obsMsg[QStringLiteral("content")] = QStringLiteral(
        "[Tool Observation: '%1'] Status: %2\nResult: %3\n\nUpdated Timeline State:\n%4\n\n"
        "Continue towards achieving user goal: \"%5\". Take the next step or conclude if complete."
    ).arg(actionName, success ? QStringLiteral("Success") : QStringLiteral("Failed"), compactedObservation, buildEditorStateSnapshot(), m_currentGoal);
    m_conversationMessages.append(obsMsg);

    sendCurrentMessages();
}

void AIDispatcher::sendCurrentMessages()
{
    m_requestTimer.start();
    Q_EMIT requestStarted();

    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        loadEnvConfig();
    }

    QUrl url(m_apiUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    // Check if request is targeting Supabase Edge Functions (e.g. ai-proxy) or a direct provider
    bool isSupabaseProxy = m_apiUrl.contains(QStringLiteral("functions/v1/ai-proxy")) || 
                           m_apiUrl.contains(QStringLiteral("supabase.co"));

    if (isSupabaseProxy) {
        if (m_supabaseAnonKey.isEmpty()) {
            m_goalActive = false;
            Q_EMIT errorOccurred(QStringLiteral("Supabase Anon Key is not configured. Please define SUPABASE_ANON_KEY and SUPABASE_URL in your .env.local file."));
            return;
        }
        request.setRawHeader("apikey", m_supabaseAnonKey.toUtf8());
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_supabaseAnonKey).toUtf8());

        // Pass authenticated user metadata as custom headers for audit/session context
        if (AuthManager::instance()->isLoggedIn() && !AuthManager::instance()->accessToken().isEmpty()) {
            request.setRawHeader("x-user-jwt", AuthManager::instance()->accessToken().toUtf8());
            request.setRawHeader("x-user-id", AuthManager::instance()->userId().toUtf8());
            request.setRawHeader("x-user-email", AuthManager::instance()->userEmail().toUtf8());
        }

        // Provider-specific API key injection (Velo pattern):
        if (!m_openRouterKey.isEmpty()) {
            request.setRawHeader("x-openrouter-key", m_openRouterKey.toUtf8());
        } else if (!m_apiKey.isEmpty()) {
            request.setRawHeader("x-openrouter-key", m_apiKey.toUtf8());
        }

        if (!m_groqKey.isEmpty()) {
            request.setRawHeader("x-groq-key", m_groqKey.toUtf8());
        }
    } else {
        // Direct non-proxy LLM endpoint (e.g. localhost Ollama or direct OpenAI)
        QString directKey = !m_apiKey.isEmpty() ? m_apiKey : (!m_openRouterKey.isEmpty() ? m_openRouterKey : m_supabaseAnonKey);
        if (!directKey.isEmpty()) {
            request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(directKey).toUtf8());
        }
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
                    if (errMsg.isEmpty()) errMsg = errVal.toObject()[QStringLiteral("code")].toString();
                } else {
                    errMsg = errVal.toString();
                }
            } else if (obj.contains(QStringLiteral("message"))) {
                errMsg = obj[QStringLiteral("message")].toString();
            } else if (obj.contains(QStringLiteral("msg"))) {
                errMsg = obj[QStringLiteral("msg")].toString();
            }
        }

        if (errMsg.isEmpty()) {
            errMsg = reply->errorString();
        }

        // Diagnostic formatting mirroring Velo's llmErrorMessage (server/plugins/llm-proxy.ts)
        if (statusCode == 401 || statusCode == 403) {
            if (modelUsed.startsWith(QStringLiteral("openrouter/")) || !modelUsed.startsWith(QStringLiteral("groq/"))) {
                if (m_openRouterKey.isEmpty() && m_apiKey.isEmpty()) {
                    errMsg = QStringLiteral("OpenRouter authentication failed (HTTP %1): %2. Please configure OPENROUTER_API_KEY in .env.local or Settings.").arg(QString::number(statusCode), errMsg);
                } else {
                    errMsg = QStringLiteral("Authentication failed (HTTP %1): %2. Please check your API key.").arg(QString::number(statusCode), errMsg);
                }
            } else {
                errMsg = QStringLiteral("Authentication failed (HTTP %1): %2.").arg(QString::number(statusCode), errMsg);
            }
        } else if (statusCode == 402 || statusCode == 429) {
            errMsg = QStringLiteral("AI service quota exceeded or rate limited (HTTP %1): %2. Check balance or try again.").arg(QString::number(statusCode), errMsg);
        } else if (statusCode == 404) {
            errMsg = QStringLiteral("Model '%1' or endpoint not found on AI Proxy (HTTP 404).").arg(modelUsed);
        } else if (statusCode >= 500) {
            errMsg = QStringLiteral("AI service error (HTTP %1): %2").arg(QString::number(statusCode), errMsg);
        }

        qWarning() << "[AIDispatcher] Request failed for model" << modelUsed << "with status" << statusCode << ":" << errMsg;
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

            // Extract from content, reasoning_content, reasoning, or text
            if (aiText.isEmpty() && message.contains(QStringLiteral("reasoning"))) {
                aiText = message[QStringLiteral("reasoning")].toString();
            }
            if (aiText.isEmpty() && message.contains(QStringLiteral("reasoning_content"))) {
                aiText = message[QStringLiteral("reasoning_content")].toString();
            }
            if (aiText.isEmpty() && message.contains(QStringLiteral("text"))) {
                aiText = message[QStringLiteral("text")].toString();
            }
            if (aiText.isEmpty() && choice0.contains(QStringLiteral("text"))) {
                aiText = choice0[QStringLiteral("text")].toString();
            }

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

    // 1. Extract JSON block from standard markdown code fence ```json ... ```
    static const QRegularExpression jsonRegex(QStringLiteral("```(?:json)?\\s*([\\s\\S]*?)\\s*```"));
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

    // 2. If no complete code fence matched, check for unclosed ```json or raw embedded JSON object
    if (actionObj.isEmpty()) {
        int firstBrace = aiText.indexOf(QLatin1Char('{'));
        int lastBrace = aiText.lastIndexOf(QLatin1Char('}'));
        if (firstBrace >= 0 && lastBrace > firstBrace) {
            QString rawJson = aiText.mid(firstBrace, lastBrace - firstBrace + 1).trimmed();
            QJsonDocument actionDoc = QJsonDocument::fromJson(rawJson.toUtf8());
            if (actionDoc.isObject() && actionDoc.object().contains(QStringLiteral("action"))) {
                actionObj = actionDoc.object();
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
        // If the model spoke about intent/actions without outputting the JSON action block,
        // do NOT quit early! Auto-reprompt with the active goal to enforce continuous execution.
        bool seemsLikePlanWithoutAction = aiText.contains(QStringLiteral("I will"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("I'll"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Now I"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Next,"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Generating"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Implementing"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Creating"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Searching"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Inserting"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Cutting"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Applying"), Qt::CaseInsensitive) ||
                                          aiText.contains(QStringLiteral("Trimming"), Qt::CaseInsensitive);

        if (m_goalActive && m_currentStep < m_maxSteps && seemsLikePlanWithoutAction) {
            qDebug() << "[AIDispatcher] Model provided intent/reasoning without JSON action block. Auto-prompting for immediate tool execution...";
            QJsonObject reminderMsg;
            reminderMsg[QStringLiteral("role")] = QStringLiteral("user");
            reminderMsg[QStringLiteral("content")] = QStringLiteral(
                "You described your next step, but did not return the executable JSON tool action block.\n"
                "To continue towards user goal: \"%1\", output your executable JSON action block now in the exact format:\n"
                "```json\n"
                "{\"action\": \"<tool_name>\", \"target\": \"kdenlive\", \"params\": {...}}\n"
                "```\n"
                "Output the executable JSON action block now."
            ).arg(m_currentGoal);
            m_conversationMessages.append(reminderMsg);
            m_currentStep++;
            sendCurrentMessages();
            return;
        }

        // Goal achieved or direct textual response
        m_goalActive = false;
        Q_EMIT responseReceived(aiText, actionObj);
        Q_EMIT goalFinished(aiText);
    }
}

void AIDispatcher::maybeCompactConversation()
{
    if (m_conversationMessages.size() < 6) return;

    // Convert Qt conversation messages to agent_cpp common_chat_msg structures
    std::vector<agent_cpp::common_chat_msg> cppMessages;
    for (const QJsonValue &val : m_conversationMessages) {
        QJsonObject obj = val.toObject();
        agent_cpp::common_chat_msg msg;
        msg.role = obj[QStringLiteral("role")].toString().toStdString();
        msg.content = obj[QStringLiteral("content")].toString().toStdString();
        cppMessages.push_back(msg);
    }

    size_t currentTokens = agent_cpp::estimate_context_tokens(cppMessages);
    size_t triggerTokens = static_cast<size_t>(m_contextBudget.context_window_tokens * m_contextBudget.trigger_fraction);

    // If context is above 70% trigger (or above 20 conversation rounds)
    if (currentTokens <= triggerTokens && m_conversationMessages.size() <= 20) {
        return;
    }

    qDebug() << "[AIDispatcher/agent_cpp] Dynamic context compaction triggered (" << currentTokens << "estimated tokens /" << m_conversationMessages.size() << "msgs)";

    bool compacted = agent_cpp::maybe_compact_context(cppMessages, {}, m_contextBudget, nullptr);
    if (compacted) {
        QJsonArray newConversation;
        for (const auto &m : cppMessages) {
            QJsonObject obj;
            obj[QStringLiteral("role")] = QString::fromStdString(m.role);
            obj[QStringLiteral("content")] = QString::fromStdString(m.content);
            newConversation.append(obj);
        }
        m_conversationMessages = newConversation;
        qDebug() << "[AIDispatcher/agent_cpp] Context compacted down to" << m_conversationMessages.size() << "messages with active [CONTEXT CHECKPOINT].";
    }
}
