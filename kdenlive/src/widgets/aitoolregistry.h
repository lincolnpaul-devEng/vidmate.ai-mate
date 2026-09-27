/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * AI Tool Registry — Structured tool schema definitions for the Velo-style
 * autonomous video editing agent harness. Each tool maps to a Kdenlive or
 * Natron operation and exposes a JSON parameter schema for the LLM.
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/**
 * @class AIToolRegistry
 * @brief Central registry of all AI-callable tool schemas.
 *
 * Generates the OpenAI-compatible "tools" array for function calling,
 * plus human-readable descriptions for the system prompt.
 */
class AIToolRegistry
{
public:
    AIToolRegistry();
    ~AIToolRegistry() = default;

    /** @brief Returns OpenAI function-calling tools array for API request */
    QJsonArray toolSchemas() const;

    /** @brief Returns formatted tool descriptions for system prompt injection */
    QString toolDescriptionsForPrompt() const;

    /** @brief Returns list of all registered tool names */
    QStringList toolNames() const;

    /** @brief Returns schema for a single tool by name */
    QJsonObject toolSchema(const QString &name) const;

private:
    void registerAllTools();
    void addTool(const QString &name, const QString &description,
                 const QJsonObject &parameters, const QString &targetEngine = QStringLiteral("kdenlive"));

    struct ToolDef {
        QString name;
        QString description;
        QJsonObject parameters;
        QString targetEngine;
    };

    QList<ToolDef> m_tools;
};
