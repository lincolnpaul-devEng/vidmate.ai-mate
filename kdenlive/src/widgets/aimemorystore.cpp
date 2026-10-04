/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#include "aimemorystore.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDebug>

AIMemoryStore *AIMemoryStore::s_instance = nullptr;

AIMemoryStore *AIMemoryStore::instance()
{
    if (!s_instance) {
        s_instance = new AIMemoryStore(qApp);
    }
    return s_instance;
}

AIMemoryStore::AIMemoryStore(QObject *parent)
    : QObject(parent)
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/VidMate");
    QDir().mkpath(configDir);
    m_filePath = configDir + QStringLiteral("/agent_memory.json");
    loadFromFile();
}

void AIMemoryStore::loadFromFile()
{
    QMutexLocker locker(&m_mutex);
    QFile file(m_filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            m_memories = doc.object();
        }
        file.close();
    }
}

void AIMemoryStore::saveToFile()
{
    QFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(m_memories);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

QString AIMemoryStore::read(const QString &key, const QString &defaultValue) const
{
    QMutexLocker locker(&m_mutex);
    if (m_memories.contains(key)) {
        return m_memories.value(key).toString();
    }
    return defaultValue;
}

void AIMemoryStore::write(const QString &key, const QString &value)
{
    {
        QMutexLocker locker(&m_mutex);
        m_memories[key] = value;
        saveToFile();
    }
    qDebug() << "[AIMemoryStore] Saved memory:" << key << "->" << value;
    Q_EMIT memoryChanged(key, value);
}

void AIMemoryStore::remove(const QString &key)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_memories.contains(key)) {
            m_memories.remove(key);
            saveToFile();
        }
    }
    Q_EMIT memoryRemoved(key);
}

bool AIMemoryStore::hasKey(const QString &key) const
{
    QMutexLocker locker(&m_mutex);
    return m_memories.contains(key);
}

QStringList AIMemoryStore::listKeys() const
{
    QMutexLocker locker(&m_mutex);
    return m_memories.keys();
}

QJsonObject AIMemoryStore::allMemories() const
{
    QMutexLocker locker(&m_mutex);
    return m_memories;
}

QString AIMemoryStore::formattedForPrompt() const
{
    QMutexLocker locker(&m_mutex);
    if (m_memories.isEmpty()) {
        return QStringLiteral("None recorded yet.");
    }

    QStringList lines;
    for (auto it = m_memories.begin(); it != m_memories.end(); ++it) {
        lines << QStringLiteral("- **%1**: %2").arg(it.key(), it.value().toString());
    }
    return lines.join(QLatin1Char('\n'));
}

void AIMemoryStore::clear()
{
    {
        QMutexLocker locker(&m_mutex);
        m_memories = QJsonObject();
        saveToFile();
    }
}
