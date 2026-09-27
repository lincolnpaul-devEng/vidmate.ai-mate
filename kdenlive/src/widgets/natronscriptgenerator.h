/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Natron Script Generator — Generates complete, executable Python scripts
 * that construct and wire OpenFX node graphs in Natron (headless or GUI).
 */

#pragma once

#include <QString>
#include <QJsonObject>
#include <QColor>

/**
 * @class NatronScriptGenerator
 * @brief Procedural generator for headless Natron Python compositing scripts.
 */
class NatronScriptGenerator
{
public:
    NatronScriptGenerator() = default;
    ~NatronScriptGenerator() = default;

    /** @brief Generates chroma keying node graph script (ReadOIIO -> KeyerPlugin / ChromaKeyer -> Despill -> WriteOIIO) */
    static QString generateChromaKeyScript(const QString &inputClip,
                                           const QString &outputPath,
                                           const QColor &keyColor = QColor(0, 255, 0),
                                           double despill = 0.5,
                                           double feather = 0.0,
                                           int startFrame = 1,
                                           int endFrame = -1);

    /** @brief Generates rotoscoping / matte composite script (ReadOIIO -> Roto -> MatteMerge -> WriteOIIO) */
    static QString generateRotoscopeScript(const QString &inputClip,
                                           const QString &outputPath,
                                           const QString &backgroundClip = QString(),
                                           double feather = 2.0,
                                           int startFrame = 1,
                                           int endFrame = -1);

    /** @brief Generates planar / point tracker stabilization or match-move script */
    static QString generateTrackerTransformScript(const QString &inputClip,
                                                  const QString &overlayClip,
                                                  const QString &outputPath,
                                                  const QJsonObject &trackingKeyframes,
                                                  int startFrame = 1,
                                                  int endFrame = -1);

    /** @brief Generates primary & secondary color grading node graph script */
    static QString generateColorGradeScript(const QString &inputClip,
                                            const QString &outputPath,
                                            double lift = 0.0,
                                            double gamma = 1.0,
                                            double gain = 1.0,
                                            double saturation = 1.0,
                                            int startFrame = 1,
                                            int endFrame = -1);

    /** @brief Generates complex multi-layer glitch / chromatic aberration composite script */
    static QString generateGlitchCompositeScript(const QString &inputClip,
                                                 const QString &outputPath,
                                                 double intensity = 0.5,
                                                 int startFrame = 1,
                                                 int endFrame = -1);

    /** @brief Generates arbitrary node graph script from structured JSON specifications */
    static QString generateFromSpec(const QJsonObject &vfxSpec,
                                    const QString &inputClip,
                                    const QString &outputPath,
                                    int startFrame = 1,
                                    int endFrame = -1);
};
