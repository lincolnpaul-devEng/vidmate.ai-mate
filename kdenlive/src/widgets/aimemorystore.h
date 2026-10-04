/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QMutex>

/**
 * @class AIMemoryStore
 * @brief Persistent memory store for the Video AI Agent (inspired by agent.cpp MemoryStore).
 *
 * Stores and retrieves persistent user preferences, editing guidelines, project styles,
 * and creative constraints across sessions in JSON format.
 */
class AIMemoryStore : public QObject
{
    Q_OBJECT

public:
    explicit AIMemoryStore(QObject *parent = nullptr);
    ~AIMemoryStore() override = default;

    static AIMemoryStore *instance();

    /** @brief Reads a memory by key */
    QString read(const QString &key, const QString &defaultValue = QString()) const;

    /** @brief Writes or updates a memory key-value pair */
    void write(const QString &key, const QString &value);

    /** @brief Deletes a memory by key */
    void remove(const QString &key);

    /** @brief Checks if a key exists */
    bool hasKey(const QString &key) const;

    /** @brief Returns list of all memory keys */
    QStringList listKeys() const;

    /** @brief Returns all memories as a JSON object */
    QJsonObject allMemories() const;

    /** @brief Formats active memories into a compact markdown block for system prompt injection */
    QString formattedForPrompt() const;

    /** @brief Clears all stored memories */
    void clear();

Q_SIGNALS:
    void memoryChanged(const QString &key, const QString &value);
    void memoryRemoved(const QString &key);

private:
    void loadFromFile();
    void saveToFile();

    QString m_filePath;
    QJsonObject m_memories;
    mutable QMutex m_mutex;
    static AIMemoryStore *s_instance;
};
