/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>

class TimelineController;
class TimelineItemModel;

/**
 * @class AICommandRouter
 * @brief Translates AI JSON actions into native Kdenlive mutations or headless Natron VFX runs.
 *
 * This is the Velo-equivalent "tool executor" — each tool from the AIToolRegistry
 * has a corresponding handler method here.
 */
class AICommandRouter : public QObject
{
    Q_OBJECT

public:
    explicit AICommandRouter(QObject *parent = nullptr);
    ~AICommandRouter() override = default;

public Q_SLOTS:
    void executeAction(const QJsonObject &actionPayload);

Q_SIGNALS:
    void executionFinished(const QString &resultMessage, bool success);
    /** @brief Emitted when tool produces data output (frame images, state info) */
    void dataOutput(const QString &toolName, const QJsonObject &data);

private:
    // Core timeline helpers
    TimelineController *getTimelineController();
    std::shared_ptr<TimelineItemModel> getTimelineModel();

    // Kdenlive tool handlers
    void handleCutAtPlayhead(const QJsonObject &params);
    void handleDeleteClips(const QJsonObject &params);
    void handleTrimClip(const QJsonObject &params);
    void handleMoveClip(const QJsonObject &params);
    void handleSetClipSpeed(const QJsonObject &params);
    void handleInsertClip(const QJsonObject &params);
    void handleAddEffect(const QJsonObject &params);
    void handleRemoveEffect(const QJsonObject &params);
    void handleAddTrack(const QJsonObject &params);
    void handleAddTransition(const QJsonObject &params);
    void handleAddMix(const QJsonObject &params);
    void handleSetVolume(const QJsonObject &params);
    void handleAudioDucking(const QJsonObject &params);
    void handleRemoveSilence(const QJsonObject &params);
    void handleAddSubtitle(const QJsonObject &params);
    void handleInsertTitle(const QJsonObject &params);
    void handleViewTimelineFrames(const QJsonObject &params);
    void handleGetTimelineState(const QJsonObject &params);
    void handleProbeQuality(const QJsonObject &params);
    void handleSeekTo(const QJsonObject &params);
    void handleSetZone(const QJsonObject &params);
    void handleFindTranscript(const QJsonObject &params);
    void handleGenerateTranscript(const QJsonObject &params);
    void handleRenderProject(const QJsonObject &params);
    void handleUndoLast(const QJsonObject &params);
    void handleSearchStockMedia(const QJsonObject &params);
    void handleGenerateVoiceover(const QJsonObject &params);
    void handleInsertMediaUrl(const QJsonObject &params);

    // Natron VFX handler
    void executeNatronVfxJob(const QJsonObject &params);

    QProcess *m_natronProcess{nullptr};
    QString m_lastVfxOutputPath;
};
