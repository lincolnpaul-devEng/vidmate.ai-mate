/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Velo Stock Media & AI Voiceover Studio Widget Implementation
 */

#include "veloassetwidget.h"
#include "core.h"
#include "mainwindow.h"
#include "bin/bin.h"
#include "timeline2/view/timelinewidget.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/model/timelinemodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QCoreApplication>
#include <QUrlQuery>
#include <QDebug>
#include <QUuid>

VeloAssetWidget::VeloAssetWidget(QWidget *parent)
    : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    setupUi();
    loadEnvCredentials();
    fetchAvailableVoices();
}

void VeloAssetWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    m_tabs = new QTabWidget(this);

    // ── Tab 1: Stock Media Browser ──────────────────────────────────────────
    auto *stockTab = new QWidget(this);
    auto *stockLayout = new QVBoxLayout(stockTab);
    stockLayout->setContentsMargins(4, 4, 4, 4);
    stockLayout->setSpacing(4);

    // Search header
    auto *searchRow = new QHBoxLayout();
    m_categoryCombo = new QComboBox(stockTab);
    m_categoryCombo->addItem(QStringLiteral("🎥 Videos"), QStringLiteral("videos"));
    m_categoryCombo->addItem(QStringLiteral("🖼️ Photos"), QStringLiteral("images"));
    m_categoryCombo->addItem(QStringLiteral("🔊 SFX (Audio)"), QStringLiteral("sfx"));
    m_categoryCombo->addItem(QStringLiteral("🎭 GIFs"), QStringLiteral("gifs"));
    m_categoryCombo->addItem(QStringLiteral("✨ Stickers"), QStringLiteral("stickers"));

    m_searchEdit = new QLineEdit(stockTab);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search stock media (e.g. drone, cinematic, whoosh, neon)..."));
    m_searchEdit->setClearButtonEnabled(true);

    m_searchBtn = new QPushButton(QStringLiteral("🔍 Search"), stockTab);

    searchRow->addWidget(m_categoryCombo);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchBtn);
    stockLayout->addLayout(searchRow);

    // Results list
    m_resultsList = new QListWidget(stockTab);
    m_resultsList->setIconSize(QSize(96, 64));
    m_resultsList->setSpacing(2);
    m_resultsList->setAlternatingRowColors(true);
    stockLayout->addWidget(m_resultsList, 1);

    // Actions row
    auto *actionRow = new QHBoxLayout();
    m_addToBinBtn = new QPushButton(QStringLiteral("📥 Add to Project Bin"), stockTab);
    m_insertTimelineBtn = new QPushButton(QStringLiteral("➕ Insert to Timeline"), stockTab);
    m_stockStatusLabel = new QLabel(QStringLiteral("Ready"), stockTab);
    m_stockStatusLabel->setStyleSheet(QStringLiteral("color: #888888;"));

    actionRow->addWidget(m_addToBinBtn);
    actionRow->addWidget(m_insertTimelineBtn);
    actionRow->addStretch();
    actionRow->addWidget(m_stockStatusLabel);
    stockLayout->addLayout(actionRow);

    m_tabs->addTab(stockTab, QStringLiteral("📦 Stock Assets"));

    // ── Tab 2: AI Voiceover Studio (ElevenLabs) ─────────────────────────────
    auto *voiceTab = new QWidget(this);
    auto *voiceLayout = new QVBoxLayout(voiceTab);
    voiceLayout->setContentsMargins(6, 6, 6, 6);
    voiceLayout->setSpacing(6);

    auto *voiceForm = new QFormLayout();
    m_voiceCombo = new QComboBox(voiceTab);
    m_voiceCombo->addItem(QStringLiteral("Rachel (Warm & Natural)"), QStringLiteral("21m00Tcm4TlvDq8ikWAM"));
    m_voiceCombo->addItem(QStringLiteral("Adam (Deep & Authoritative)"), QStringLiteral("pNInz6obpgDQGcFmaJgB"));
    m_voiceCombo->addItem(QStringLiteral("Antoni (Warm Storyteller)"), QStringLiteral("ErXwobaYiN019PkySvjV"));
    m_voiceCombo->addItem(QStringLiteral("Bella (Narrative Expressive)"), QStringLiteral("EXAVITQu4vr4xnSDxMaL"));
    m_voiceCombo->addItem(QStringLiteral("Arnold (Crisp Video Host)"), QStringLiteral("VR6AewLTigWG4xSOukaG"));
    voiceForm->addRow(QStringLiteral("Speaker Voice:"), m_voiceCombo);

    m_voiceTextEdit = new QPlainTextEdit(voiceTab);
    m_voiceTextEdit->setPlaceholderText(QStringLiteral("Enter script or dialogue to synthesize with ElevenLabs high-fidelity neural voice..."));
    voiceForm->addRow(QStringLiteral("Voiceover Text:"), m_voiceTextEdit);

    auto *sliderRow = new QHBoxLayout();
    m_speedSlider = new QSlider(Qt::Horizontal, voiceTab);
    m_speedSlider->setRange(50, 150);
    m_speedSlider->setValue(100);
    m_speedLabel = new QLabel(QStringLiteral("1.0x"), voiceTab);

    m_stabilitySlider = new QSlider(Qt::Horizontal, voiceTab);
    m_stabilitySlider->setRange(0, 100);
    m_stabilitySlider->setValue(50);
    m_stabilityLabel = new QLabel(QStringLiteral("0.50"), voiceTab);

    sliderRow->addWidget(new QLabel(QStringLiteral("Speed:")));
    sliderRow->addWidget(m_speedSlider);
    sliderRow->addWidget(m_speedLabel);
    sliderRow->addSpacing(12);
    sliderRow->addWidget(new QLabel(QStringLiteral("Stability:")));
    sliderRow->addWidget(m_stabilitySlider);
    sliderRow->addWidget(m_stabilityLabel);
    voiceLayout->addLayout(voiceForm);
    voiceLayout->addLayout(sliderRow);

    m_generateVoiceBtn = new QPushButton(QStringLiteral("🎙️ Generate Voiceover & Add to Timeline"), voiceTab);
    m_generateVoiceBtn->setStyleSheet(QStringLiteral("font-weight: bold; padding: 6px;"));
    m_voiceProgressBar = new QProgressBar(voiceTab);
    m_voiceProgressBar->setVisible(false);
    m_voiceStatusLabel = new QLabel(QStringLiteral("Ready to generate speech."), voiceTab);
    m_voiceStatusLabel->setStyleSheet(QStringLiteral("color: #888888;"));

    voiceLayout->addWidget(m_generateVoiceBtn);
    voiceLayout->addWidget(m_voiceProgressBar);
    voiceLayout->addWidget(m_voiceStatusLabel);
    voiceLayout->addStretch();

    m_tabs->addTab(voiceTab, QStringLiteral("🎙️ Voice Studio"));

    mainLayout->addWidget(m_tabs);

    // Connections
    connect(m_searchBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotSearchClicked);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &VeloAssetWidget::slotSearchClicked);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VeloAssetWidget::slotCategoryChanged);
    connect(m_resultsList, &QListWidget::itemDoubleClicked, this, &VeloAssetWidget::slotAssetDoubleClicked);
    connect(m_addToBinBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotAddSelectedToBin);
    connect(m_insertTimelineBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotInsertSelectedToTimeline);

    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int val) {
        m_speedLabel->setText(QStringLiteral("%1x").arg(val / 100.0, 0, 'f', 2));
    });
    connect(m_stabilitySlider, &QSlider::valueChanged, this, [this](int val) {
        m_stabilityLabel->setText(QStringLiteral("%1").arg(val / 100.0, 0, 'f', 2));
    });
    connect(m_generateVoiceBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotGenerateVoiceClicked);
}

void VeloAssetWidget::loadEnvCredentials()
{
    QStringList searchPaths;
    searchPaths << QDir::current().filePath(QStringLiteral(".env.local"))
                << QDir::current().filePath(QStringLiteral(".env"))
                << QCoreApplication::applicationDirPath() + QStringLiteral("/.env.local")
                << QCoreApplication::applicationDirPath() + QStringLiteral("/.env")
                << QStringLiteral("/home/lincoln/vidmate.ai-mate/kdenlive/.env.local")
                << QStringLiteral("/home/lincoln/vidmate.ai-mate/kdenlive/.env");

    for (const QString &path : searchPaths) {
        QFile file(path);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
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
                }
            }
            break;
        }
    }
}

QNetworkRequest VeloAssetWidget::createSupabaseRequest(const QString &functionPath) const
{
    QString urlStr = QStringLiteral("%1/functions/v1/%2").arg(m_supabaseUrl, functionPath);
    QUrl targetUrl(urlStr);
    QNetworkRequest request(targetUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_supabaseAnonKey.isEmpty()) {
        request.setRawHeader("apikey", m_supabaseAnonKey.toUtf8());
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_supabaseAnonKey).toUtf8());
    }
    return request;
}

void VeloAssetWidget::fetchAvailableVoices()
{
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) return;

    QNetworkRequest req = createSupabaseRequest(QStringLiteral("get-voices"));
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            parseAndDisplayVoices(reply->readAll());
        }
    });
}

void VeloAssetWidget::parseAndDisplayVoices(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonArray voices = root[QStringLiteral("voices")].toArray();
    if (voices.isEmpty()) return;

    m_voiceCombo->clear();
    m_voiceMap.clear();

    for (const auto &vVal : voices) {
        QJsonObject vObj = vVal.toObject();
        QString id = vObj[QStringLiteral("voice_id")].toString();
        QString name = vObj[QStringLiteral("name")].toString();
        QString category = vObj[QStringLiteral("category")].toString();
        QString desc = vObj[QStringLiteral("description")].toString();

        QString display = name;
        if (!desc.isEmpty()) display += QStringLiteral(" (%1)").arg(desc.left(30));
        else if (!category.isEmpty()) display += QStringLiteral(" [%1]").arg(category);

        m_voiceCombo->addItem(display, id);
        m_voiceMap.insert(name.toLower(), id);
    }
}

void VeloAssetWidget::slotSearchClicked()
{
    QString category = m_categoryCombo->currentData().toString();
    QString query = m_searchEdit->text().trimmed();
    searchStock(category, query);
}

void VeloAssetWidget::slotCategoryChanged(int)
{
    slotSearchClicked();
}

void VeloAssetWidget::searchStock(const QString &category, const QString &query, int page)
{
    if (m_supabaseUrl.isEmpty()) {
        m_stockStatusLabel->setText(QStringLiteral("❌ Supabase URL not configured"));
        return;
    }

    m_stockStatusLabel->setText(QStringLiteral("Searching %1...").arg(category));
    m_resultsList->clear();
    m_currentAssets.clear();

    if (category == QStringLiteral("videos")) {
        QString path = QStringLiteral("pexels-proxy?type=videos&query=%1&page=%2&per_page=20")
            .arg(QUrl::toPercentEncoding(query.isEmpty() ? QStringLiteral("cinematic") : query))
            .arg(page);
        QNetworkRequest req = createSupabaseRequest(path);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(QStringLiteral("Error: %1").arg(reply->errorString()));
            }
        });
    } else if (category == QStringLiteral("images")) {
        QString path = QStringLiteral("pexels-proxy?type=images&query=%1&page=%2&per_page=20")
            .arg(QUrl::toPercentEncoding(query.isEmpty() ? QStringLiteral("wallpaper") : query))
            .arg(page);
        QNetworkRequest req = createSupabaseRequest(path);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(QStringLiteral("Error: %1").arg(reply->errorString()));
            }
        });
    } else if (category == QStringLiteral("sfx")) {
        QString path = QStringLiteral("freesound-proxy?query=%1&page=%2")
            .arg(QUrl::toPercentEncoding(query.isEmpty() ? QStringLiteral("whoosh") : query))
            .arg(page);
        QNetworkRequest req = createSupabaseRequest(path);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(QStringLiteral("Error: %1").arg(reply->errorString()));
            }
        });
    } else if (category == QStringLiteral("gifs")) {
        QString action = query.isEmpty() ? QStringLiteral("trending") : QStringLiteral("search");
        QString path = QStringLiteral("giphy-proxy?action=%1&q=%2&limit=24")
            .arg(action, QUrl::toPercentEncoding(query));
        QNetworkRequest req = createSupabaseRequest(path);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(QStringLiteral("Error: %1").arg(reply->errorString()));
            }
        });
    }
}

void VeloAssetWidget::parseAndDisplaySearchResults(const QByteArray &data, const QString &category)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();

    m_currentAssets.clear();
    m_resultsList->clear();

    if (category == QStringLiteral("videos")) {
        QJsonArray vids = root[QStringLiteral("videos")].toArray();
        for (const auto &vVal : vids) {
            QJsonObject v = vVal.toObject();
            StockAssetItem item;
            item.id = QString::number(v[QStringLiteral("id")].toInt());
            item.title = QStringLiteral("Pexels Video #%1 (%2s)").arg(item.id).arg(v[QStringLiteral("duration")].toInt());
            item.previewUrl = v[QStringLiteral("image")].toString();
            item.kind = QStringLiteral("video");
            item.provider = QStringLiteral("pexels");
            item.duration = v[QStringLiteral("duration")].toDouble();
            item.width = v[QStringLiteral("width")].toInt();
            item.height = v[QStringLiteral("height")].toInt();

            // Find best mp4 file link
            QJsonArray files = v[QStringLiteral("video_files")].toArray();
            for (const auto &fVal : files) {
                QJsonObject f = fVal.toObject();
                QString link = f[QStringLiteral("link")].toString();
                if (link.contains(QStringLiteral(".mp4")) || f[QStringLiteral("file_type")].toString() == QStringLiteral("video/mp4")) {
                    item.downloadUrl = link;
                    if (f[QStringLiteral("quality")].toString() == QStringLiteral("hd")) break;
                }
            }
            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                m_resultsList->addItem(QStringLiteral("🎥 %1 — %2x%3").arg(item.title).arg(item.width).arg(item.height));
            }
        }
    } else if (category == QStringLiteral("images")) {
        QJsonArray photos = root[QStringLiteral("photos")].toArray();
        for (const auto &pVal : photos) {
            QJsonObject p = pVal.toObject();
            StockAssetItem item;
            item.id = QString::number(p[QStringLiteral("id")].toInt());
            item.title = p[QStringLiteral("alt")].toString();
            if (item.title.isEmpty()) item.title = QStringLiteral("Photo #%1 by %2").arg(item.id, p[QStringLiteral("photographer")].toString());
            item.kind = QStringLiteral("image");
            item.provider = QStringLiteral("pexels");
            item.width = p[QStringLiteral("width")].toInt();
            item.height = p[QStringLiteral("height")].toInt();

            QJsonObject src = p[QStringLiteral("src")].toObject();
            item.previewUrl = src[QStringLiteral("medium")].toString();
            item.downloadUrl = src[QStringLiteral("large2x")].toString();
            if (item.downloadUrl.isEmpty()) item.downloadUrl = src[QStringLiteral("original")].toString();

            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                m_resultsList->addItem(QStringLiteral("🖼️ %1 (%2x%3)").arg(item.title).arg(item.width).arg(item.height));
            }
        }
    } else if (category == QStringLiteral("sfx")) {
        QJsonArray sfxList = root[QStringLiteral("soundEffects")].toArray();
        for (const auto &sVal : sfxList) {
            QJsonObject s = sVal.toObject();
            StockAssetItem item;
            item.id = QString::number(s[QStringLiteral("id")].toInt());
            item.title = s[QStringLiteral("name")].toString();
            item.kind = QStringLiteral("audio");
            item.provider = QStringLiteral("freesound");
            item.duration = s[QStringLiteral("duration")].toDouble();

            QJsonObject previews = s[QStringLiteral("previews")].toObject();
            item.downloadUrl = previews[QStringLiteral("preview-hq-mp3")].toString();
            item.previewUrl = item.downloadUrl;

            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                m_resultsList->addItem(QStringLiteral("🔊 %1 (%2s)").arg(item.title).arg(item.duration, 0, 'f', 1));
            }
        }
    } else if (category == QStringLiteral("gifs")) {
        QJsonArray gifs = root[QStringLiteral("data")].toArray();
        for (const auto &gVal : gifs) {
            QJsonObject g = gVal.toObject();
            StockAssetItem item;
            item.id = g[QStringLiteral("id")].toString();
            item.title = g[QStringLiteral("title")].toString();
            if (item.title.isEmpty()) item.title = QStringLiteral("GIF #%1").arg(item.id);
            item.kind = QStringLiteral("gif");
            item.provider = QStringLiteral("giphy");

            QJsonObject images = g[QStringLiteral("images")].toObject();
            QJsonObject original = images[QStringLiteral("original")].toObject();
            item.previewUrl = images[QStringLiteral("fixed_height_small")].toObject()[QStringLiteral("url")].toString();
            item.downloadUrl = original[QStringLiteral("url")].toString();
            item.width = original[QStringLiteral("width")].toString().toInt();
            item.height = original[QStringLiteral("height")].toString().toInt();

            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                m_resultsList->addItem(QStringLiteral("🎭 %1").arg(item.title));
            }
        }
    }

    m_stockStatusLabel->setText(QStringLiteral("Found %1 assets").arg(m_currentAssets.count()));
}

void VeloAssetWidget::slotAssetDoubleClicked(QListWidgetItem *item)
{
    int row = m_resultsList->row(item);
    if (row >= 0 && row < m_currentAssets.count()) {
        const auto &asset = m_currentAssets[row];
        downloadAndIngest(asset.downloadUrl, asset.title, asset.kind, true);
    }
}

void VeloAssetWidget::slotAddSelectedToBin()
{
    int row = m_resultsList->currentRow();
    if (row >= 0 && row < m_currentAssets.count()) {
        const auto &asset = m_currentAssets[row];
        downloadAndIngest(asset.downloadUrl, asset.title, asset.kind, false);
    }
}

void VeloAssetWidget::slotInsertSelectedToTimeline()
{
    int row = m_resultsList->currentRow();
    if (row >= 0 && row < m_currentAssets.count()) {
        const auto &asset = m_currentAssets[row];
        downloadAndIngest(asset.downloadUrl, asset.title, asset.kind, true);
    }
}

void VeloAssetWidget::slotGenerateVoiceClicked()
{
    QString text = m_voiceTextEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_voiceStatusLabel->setText(QStringLiteral("⚠️ Please enter text to speak."));
        return;
    }

    QString voiceId = m_voiceCombo->currentData().toString();
    double speed = m_speedSlider->value() / 100.0;
    double stability = m_stabilitySlider->value() / 100.0;

    m_generateVoiceBtn->setEnabled(false);
    m_voiceProgressBar->setVisible(true);
    m_voiceProgressBar->setRange(0, 0); // Indeterminate
    m_voiceStatusLabel->setText(QStringLiteral("Generating ElevenLabs neural voiceover..."));

    generateVoiceover(text, voiceId, speed, stability, [this](const QString &localPath, double duration) {
        m_generateVoiceBtn->setEnabled(true);
        m_voiceProgressBar->setVisible(false);
        m_voiceStatusLabel->setText(QStringLiteral("✅ Voiceover generated (%1s) and added to project.").arg(duration, 0, 'f', 1));
    });
}

void VeloAssetWidget::generateVoiceover(const QString &text, const QString &voiceNameOrId,
                                       double speed, double stability,
                                       std::function<void(const QString &localPath, double duration)> onComplete)
{
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        if (onComplete) onComplete(QString(), 0.0);
        return;
    }

    QString voiceId = voiceNameOrId;
    if (m_voiceMap.contains(voiceNameOrId.toLower())) {
        voiceId = m_voiceMap.value(voiceNameOrId.toLower());
    }

    QJsonObject payload;
    payload[QStringLiteral("text")] = text;
    payload[QStringLiteral("voiceId")] = voiceId;
    payload[QStringLiteral("speed")] = speed;
    payload[QStringLiteral("stability")] = stability;
    payload[QStringLiteral("similarityBoost")] = 0.75;
    payload[QStringLiteral("modelId")] = QStringLiteral("eleven_multilingual_v2");

    QNetworkRequest req = createSupabaseRequest(QStringLiteral("generate-voice"));
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(payload).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, text, onComplete]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[VeloAssetWidget] Voice generation error:" << reply->errorString();
            if (onComplete) onComplete(QString(), 0.0);
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            if (onComplete) onComplete(QString(), 0.0);
            return;
        }

        QJsonObject res = doc.object();
        QString publicUrl = res[QStringLiteral("publicUrl")].toString();
        double duration = res[QStringLiteral("durationSeconds")].toDouble(3.0);

        if (!publicUrl.isEmpty()) {
            downloadAndIngest(publicUrl, QStringLiteral("Voiceover: %1").arg(text.left(20)), QStringLiteral("audio"), true, -1, -1,
                [onComplete, duration](const QString &, const QString &localPath) {
                    if (onComplete) onComplete(localPath, duration);
                });
        } else {
            if (onComplete) onComplete(QString(), duration);
        }
    });
}

void VeloAssetWidget::downloadAndIngest(const QString &url, const QString &name, const QString &kind,
                                       bool insertToTimeline, int trackId, int targetFrame,
                                       std::function<void(const QString &clipId, const QString &localPath)> onComplete)
{
    QString cacheDir = QDir::homePath() + QStringLiteral("/.cache/vidmate-assets");
    QDir().mkpath(cacheDir);

    QString ext = QStringLiteral(".mp4");
    if (kind == QStringLiteral("image")) ext = QStringLiteral(".jpg");
    else if (kind == QStringLiteral("audio")) ext = QStringLiteral(".mp3");
    else if (kind == QStringLiteral("gif")) ext = QStringLiteral(".gif");

    QString filename = QStringLiteral("%1_%2%3")
        .arg(name.simplified().replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_]")), QStringLiteral("_")).left(30))
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8))
        .arg(ext);

    QString localPath = cacheDir + QStringLiteral("/") + filename;

    m_stockStatusLabel->setText(QStringLiteral("Downloading %1...").arg(name));

    QUrl reqUrl(url);
    QNetworkRequest req(reqUrl);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, localPath, insertToTimeline, trackId, targetFrame, onComplete]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_stockStatusLabel->setText(QStringLiteral("Download failed: %1").arg(reply->errorString()));
            if (onComplete) onComplete(QString(), QString());
            return;
        }

        QFile outFile(localPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(reply->readAll());
            outFile.close();

            // Import to Kdenlive Project Bin
            QString clipId;
            if (pCore && pCore->activeBin()) {
                clipId = pCore->activeBin()->slotAddClipToProject(QUrl::fromLocalFile(localPath));
                m_stockStatusLabel->setText(QStringLiteral("✅ Added clip to Bin (ID: %1)").arg(clipId));
                Q_EMIT assetAddedToBin(clipId, localPath);
            }

            // Insert to Timeline if requested
            if (insertToTimeline && pCore && pCore->window() && pCore->window()->getCurrentTimeline()) {
                auto *tw = pCore->window()->getCurrentTimeline();
                auto *tc = tw->controller();
                auto tm = tw->model();
                if (tc && tm && !clipId.isEmpty()) {
                    int frame = (targetFrame >= 0) ? targetFrame : pCore->getMonitorPosition(Kdenlive::ProjectMonitor);
                    int tid = (trackId >= 0) ? trackId : tc->activeTrack();
                    int cid = -1;
                    tm->requestClipInsertion(clipId, tid, frame, cid, true, true);
                    m_stockStatusLabel->setText(QStringLiteral("🎬 Inserted clip %1 to track %2 at frame %3").arg(clipId).arg(tid).arg(frame));
                    Q_EMIT assetInsertedToTimeline(clipId, tid, frame);
                }
            }

            if (onComplete) onComplete(clipId, localPath);
        }
    });
}
