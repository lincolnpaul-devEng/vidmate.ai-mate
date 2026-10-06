/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Velo Stock Media & AI Voiceover Studio Widget
 * Unified visual access to Pexels, Pixabay, Giphy, Freesound, and ElevenLabs
 * text-to-speech with live thumbnail grids and preview player.
 */

#include "veloassetwidget.h"
#include "core.h"
#include "bin/bin.h"
#include "bin/projectitemmodel.h"
#include "bin/clipcreator.hpp"
#include "timeline2/view/timelinewidget.h"
#include "timeline2/view/timelinecontroller.h"
#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTextStream>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QSplitter>
#include <QProcessEnvironment>
#include <KLocalizedString>
#include "authmanager.h"

VeloAssetWidget::VeloAssetWidget(QWidget *parent)
    : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_audioPlayer(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
{
    m_audioPlayer->setAudioOutput(m_audioOutput);
    connect(m_audioPlayer, &QMediaPlayer::positionChanged, this, &VeloAssetWidget::slotAudioPositionChanged);
    connect(m_audioPlayer, &QMediaPlayer::durationChanged, this, &VeloAssetWidget::slotAudioDurationChanged);
    connect(m_audioPlayer, &QMediaPlayer::playbackStateChanged, this, &VeloAssetWidget::slotAudioStateChanged);

    loadEnvCredentials();
    setupUi();
    fetchAvailableVoices();
    // Auto-load popular stock video assets on startup
    searchStock(QStringLiteral("videos"), QString());
}

VeloAssetWidget::~VeloAssetWidget()
{
    stopAudioPreview();
}

void VeloAssetWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    m_tabs = new QTabWidget(this);

    // ── Tab 1: Stock Media Browser ──────────────────────────────────────────
    auto *stockTab = new QWidget(this);
    auto *stockLayout = new QVBoxLayout(stockTab);
    stockLayout->setContentsMargins(4, 4, 4, 4);
    stockLayout->setSpacing(6);

    // Search header
    auto *searchRow = new QHBoxLayout();
    m_categoryCombo = new QComboBox(stockTab);
    m_categoryCombo->addItem(i18n("Videos"), QStringLiteral("videos"));
    m_categoryCombo->addItem(i18n("Photos"), QStringLiteral("images"));
    m_categoryCombo->addItem(i18n("Sound Effects"), QStringLiteral("sfx"));
    m_categoryCombo->addItem(i18n("GIFs"), QStringLiteral("gifs"));
    m_categoryCombo->addItem(i18n("Stickers"), QStringLiteral("stickers"));

    m_searchEdit = new QLineEdit(stockTab);
    m_searchEdit->setPlaceholderText(i18n("Search stock assets (e.g. drone, cinematic, whoosh, neon)..."));
    m_searchEdit->setClearButtonEnabled(true);

    m_searchBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("system-search")), i18n("Search"), stockTab);

    searchRow->addWidget(m_categoryCombo);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchBtn);
    stockLayout->addLayout(searchRow);

    // Splitter between Grid and Preview Panel
    auto *splitter = new QSplitter(Qt::Vertical, stockTab);

    // Results Visual Grid
    m_resultsList = new QListWidget(splitter);
    m_resultsList->setViewMode(QListView::IconMode);
    m_resultsList->setIconSize(QSize(130, 80));
    m_resultsList->setGridSize(QSize(146, 115));
    m_resultsList->setResizeMode(QListView::Adjust);
    m_resultsList->setMovement(QListView::Static);
    m_resultsList->setSpacing(6);
    m_resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsList->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: palette(base); border: 1px solid palette(mid); border-radius: 4px; }"
        "QListWidget::item { border-radius: 4px; padding: 4px; font-size: 10.5px; }"
        "QListWidget::item:selected { background-color: palette(highlight); color: palette(highlighted-text); }"
    ));
    splitter->addWidget(m_resultsList);

    // Asset Preview & Ingestion Panel
    m_previewPanel = new QFrame(splitter);
    m_previewPanel->setObjectName(QStringLiteral("assetPreviewPanel"));
    m_previewPanel->setStyleSheet(QStringLiteral(
        "QFrame#assetPreviewPanel { background-color: palette(alternate-base); border: 1px solid palette(mid); border-radius: 4px; padding: 6px; }"
    ));
    auto *prevLayout = new QHBoxLayout(m_previewPanel);
    prevLayout->setContentsMargins(6, 6, 6, 6);
    prevLayout->setSpacing(12);

    m_previewImageLabel = new QLabel(m_previewPanel);
    m_previewImageLabel->setFixedSize(160, 95);
    m_previewImageLabel->setAlignment(Qt::AlignCenter);
    m_previewImageLabel->setStyleSheet(QStringLiteral("background-color: #1a1d24; border-radius: 4px; border: 1px solid #2d3340;"));
    m_previewImageLabel->setText(i18n("Select an asset to preview"));
    prevLayout->addWidget(m_previewImageLabel);

    auto *detailsLayout = new QVBoxLayout();
    detailsLayout->setSpacing(4);
    m_previewTitleLabel = new QLabel(i18n("No asset selected"), m_previewPanel);
    m_previewTitleLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 12px; color: palette(text);"));
    m_previewTitleLabel->setWordWrap(true);

    m_previewDetailsLabel = new QLabel(i18n("Resolution: -- | Duration: -- | Provider: --"), m_previewPanel);
    m_previewDetailsLabel->setStyleSheet(QStringLiteral("color: palette(text-muted); font-size: 11px;"));

    // Audio Audition Controls
    m_audioControlsWidget = new QWidget(m_previewPanel);
    auto *audioControlsLayout = new QHBoxLayout(m_audioControlsWidget);
    audioControlsLayout->setContentsMargins(0, 2, 0, 2);
    audioControlsLayout->setSpacing(8);

    m_audioPlayPauseBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Audition"), m_audioControlsWidget);
    m_audioPlayPauseBtn->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 11px; padding: 2px 8px;"));
    connect(m_audioPlayPauseBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotToggleAudioPreview);

    m_audioProgressSlider = new QSlider(Qt::Horizontal, m_audioControlsWidget);
    m_audioProgressSlider->setRange(0, 0);
    connect(m_audioProgressSlider, &QSlider::sliderMoved, this, &VeloAssetWidget::slotAudioSeek);
    connect(m_audioProgressSlider, &QSlider::sliderPressed, this, [this]() { m_isSliderSeeking = true; });
    connect(m_audioProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_isSliderSeeking = false;
        slotAudioSeek(m_audioProgressSlider->value());
    });

    m_audioTimeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), m_audioControlsWidget);
    m_audioTimeLabel->setStyleSheet(QStringLiteral("color: palette(text-muted); font-size: 11px; font-family: monospace;"));

    audioControlsLayout->addWidget(m_audioPlayPauseBtn);
    audioControlsLayout->addWidget(m_audioProgressSlider, 1);
    audioControlsLayout->addWidget(m_audioTimeLabel);
    m_audioControlsWidget->setVisible(false);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);
    m_addToBinBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Add to Project Bin"), m_previewPanel);
    m_addToBinBtn->setEnabled(false);
    m_insertTimelineBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("timeline-insert")), i18n("Insert to Timeline"), m_previewPanel);
    m_insertTimelineBtn->setEnabled(false);
    m_insertTimelineBtn->setStyleSheet(QStringLiteral("font-weight: 600;"));

    btnRow->addWidget(m_addToBinBtn);
    btnRow->addWidget(m_insertTimelineBtn);
    btnRow->addStretch(1);

    detailsLayout->addWidget(m_previewTitleLabel);
    detailsLayout->addWidget(m_previewDetailsLabel);
    detailsLayout->addWidget(m_audioControlsWidget);
    detailsLayout->addLayout(btnRow);
    detailsLayout->addStretch(1);
    prevLayout->addLayout(detailsLayout, 1);

    splitter->addWidget(m_previewPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    stockLayout->addWidget(splitter, 1);

    // Status label at bottom
    m_stockStatusLabel = new QLabel(i18n("Ready"), stockTab);
    m_stockStatusLabel->setStyleSheet(QStringLiteral("color: palette(text-muted); font-size: 11px;"));
    stockLayout->addWidget(m_stockStatusLabel);

    m_tabs->addTab(stockTab, i18n("Stock Assets"));

    // ── Tab 2: AI Voiceover Studio (ElevenLabs) ─────────────────────────────
    auto *voiceTab = new QWidget(this);
    auto *voiceLayout = new QVBoxLayout(voiceTab);
    voiceLayout->setContentsMargins(8, 8, 8, 8);
    voiceLayout->setSpacing(8);

    auto *voiceForm = new QFormLayout();
    m_voiceCombo = new QComboBox(voiceTab);
    m_voiceCombo->addItem(QStringLiteral("Sarah (Mature, Reassuring, Confident)"), QStringLiteral("EXAVITQu4vr4xnSDxMaL"));
    m_voiceCombo->addItem(QStringLiteral("Adam (Deep & Authoritative)"), QStringLiteral("pNInz6obpgDQGcFmaJgB"));
    m_voiceCombo->addItem(QStringLiteral("Roger (Laid-Back, Casual, Resonant)"), QStringLiteral("CwhRBWXzGAHq8TQ4Fs17"));
    m_voiceCombo->addItem(QStringLiteral("George (Warm Storyteller)"), QStringLiteral("JBFqnCBsd6RMkjVDRZzb"));
    m_voiceCombo->addItem(QStringLiteral("Alice (Clear, Engaging Educator)"), QStringLiteral("Xb7hH8MSUJpSbSDYk0k2"));
    m_voiceCombo->addItem(QStringLiteral("Brian (Deep, Resonant & Comforting)"), QStringLiteral("nPczCjzI2devNBz1zQrb"));
    m_voiceCombo->addItem(QStringLiteral("Jessica (Playful, Bright & Warm)"), QStringLiteral("cgSgspJ2msm6clMCkdW9"));
    m_voiceCombo->addItem(QStringLiteral("Laura (Enthusiast, Quirky Attitude)"), QStringLiteral("FGY2WhTYpPnrIDTdsKH5"));
    voiceForm->addRow(i18n("Speaker Voice:"), m_voiceCombo);

    m_voiceTextEdit = new QPlainTextEdit(voiceTab);
    m_voiceTextEdit->setPlaceholderText(i18n("Enter script or dialogue to synthesize with ElevenLabs high-fidelity neural voice..."));
    voiceForm->addRow(i18n("Voiceover Text:"), m_voiceTextEdit);

    auto *sliderRow = new QHBoxLayout();
    m_speedSlider = new QSlider(Qt::Horizontal, voiceTab);
    m_speedSlider->setRange(50, 150);
    m_speedSlider->setValue(100);
    m_speedLabel = new QLabel(QStringLiteral("1.0x"), voiceTab);

    m_stabilitySlider = new QSlider(Qt::Horizontal, voiceTab);
    m_stabilitySlider->setRange(0, 100);
    m_stabilitySlider->setValue(50);
    m_stabilityLabel = new QLabel(QStringLiteral("0.50"), voiceTab);

    sliderRow->addWidget(new QLabel(i18n("Speed:")));
    sliderRow->addWidget(m_speedSlider);
    sliderRow->addWidget(m_speedLabel);
    sliderRow->addSpacing(12);
    sliderRow->addWidget(new QLabel(i18n("Stability:")));
    sliderRow->addWidget(m_stabilitySlider);
    sliderRow->addWidget(m_stabilityLabel);
    voiceLayout->addLayout(voiceForm);
    voiceLayout->addLayout(sliderRow);

    m_generateVoiceBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("audio-input-microphone")), i18n("Generate Voiceover"), voiceTab);
    m_generateVoiceBtn->setStyleSheet(QStringLiteral("font-weight: 600; padding: 6px;"));
    m_voiceProgressBar = new QProgressBar(voiceTab);
    m_voiceProgressBar->setVisible(false);
    m_voiceStatusLabel = new QLabel(i18n("Ready to generate speech."), voiceTab);
    m_voiceStatusLabel->setStyleSheet(QStringLiteral("color: palette(text-muted); font-size: 11px;"));

    voiceLayout->addWidget(m_generateVoiceBtn);
    voiceLayout->addWidget(m_voiceProgressBar);
    voiceLayout->addWidget(m_voiceStatusLabel);

    // Synthesized Result Card
    m_voiceResultBox = new QFrame(voiceTab);
    m_voiceResultBox->setObjectName(QStringLiteral("voiceResultBox"));
    m_voiceResultBox->setStyleSheet(QStringLiteral(
        "QFrame#voiceResultBox { background-color: palette(alternate-base); border: 1px solid palette(mid); border-radius: 4px; padding: 8px; }"
    ));
    auto *resultLayout = new QVBoxLayout(m_voiceResultBox);
    resultLayout->setContentsMargins(8, 8, 8, 8);
    resultLayout->setSpacing(6);

    m_voiceResultTitle = new QLabel(i18n("No voiceover generated yet"), m_voiceResultBox);
    m_voiceResultTitle->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 12px; color: palette(text);"));
    m_voiceResultTitle->setWordWrap(true);

    m_voiceResultDetails = new QLabel(m_voiceResultBox);
    m_voiceResultDetails->setStyleSheet(QStringLiteral("color: palette(text-muted); font-size: 11px;"));

    // Audition Row
    auto *voiceAuditionRow = new QHBoxLayout();
    voiceAuditionRow->setSpacing(8);
    m_voicePlayPauseBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Audition"), m_voiceResultBox);
    m_voicePlayPauseBtn->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 11px; padding: 2px 8px;"));
    connect(m_voicePlayPauseBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotToggleVoiceResultAudio);

    m_voiceProgressSlider = new QSlider(Qt::Horizontal, m_voiceResultBox);
    m_voiceProgressSlider->setRange(0, 0);
    connect(m_voiceProgressSlider, &QSlider::sliderMoved, this, &VeloAssetWidget::slotAudioSeek);
    connect(m_voiceProgressSlider, &QSlider::sliderPressed, this, [this]() { m_isSliderSeeking = true; });
    connect(m_voiceProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_isSliderSeeking = false;
        slotAudioSeek(m_voiceProgressSlider->value());
    });

    m_voiceTimeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), m_voiceResultBox);
    m_voiceTimeLabel->setStyleSheet(QStringLiteral("color: palette(text-muted); font-size: 11px; font-family: monospace;"));

    voiceAuditionRow->addWidget(m_voicePlayPauseBtn);
    voiceAuditionRow->addWidget(m_voiceProgressSlider, 1);
    voiceAuditionRow->addWidget(m_voiceTimeLabel);

    // Action Row: Add to Project Bin & Insert to Timeline
    auto *voiceActionRow = new QHBoxLayout();
    voiceActionRow->setSpacing(6);
    m_voiceAddToBinBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Add to Project Bin"), m_voiceResultBox);
    m_voiceInsertTimelineBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("timeline-insert")), i18n("Insert to Timeline"), m_voiceResultBox);
    m_voiceInsertTimelineBtn->setStyleSheet(QStringLiteral("font-weight: 600;"));

    connect(m_voiceAddToBinBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotAddVoiceResultToBin);
    connect(m_voiceInsertTimelineBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotInsertVoiceResultToTimeline);

    voiceActionRow->addWidget(m_voiceAddToBinBtn);
    voiceActionRow->addWidget(m_voiceInsertTimelineBtn);
    voiceActionRow->addStretch(1);

    resultLayout->addWidget(m_voiceResultTitle);
    resultLayout->addWidget(m_voiceResultDetails);
    resultLayout->addLayout(voiceAuditionRow);
    resultLayout->addLayout(voiceActionRow);

    m_voiceResultBox->setVisible(false);
    voiceLayout->addWidget(m_voiceResultBox);
    voiceLayout->addStretch();

    m_tabs->addTab(voiceTab, i18n("Voice Studio"));

    mainLayout->addWidget(m_tabs);

    // Connections
    connect(m_searchBtn, &QPushButton::clicked, this, &VeloAssetWidget::slotSearchClicked);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &VeloAssetWidget::slotSearchClicked);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VeloAssetWidget::slotCategoryChanged);
    connect(m_resultsList, &QListWidget::currentItemChanged, this, &VeloAssetWidget::slotAssetSelected);
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
    // 1. Check system environment variables first
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains(QStringLiteral("VITE_SUPABASE_URL"))) m_supabaseUrl = env.value(QStringLiteral("VITE_SUPABASE_URL"));
    else if (env.contains(QStringLiteral("SUPABASE_URL"))) m_supabaseUrl = env.value(QStringLiteral("SUPABASE_URL"));

    if (env.contains(QStringLiteral("VITE_SUPABASE_ANON_KEY"))) m_supabaseAnonKey = env.value(QStringLiteral("VITE_SUPABASE_ANON_KEY"));
    else if (env.contains(QStringLiteral("SUPABASE_ANON_KEY"))) m_supabaseAnonKey = env.value(QStringLiteral("SUPABASE_ANON_KEY"));

    if (env.contains(QStringLiteral("SUPABASE_SERVICE_ROLE_KEY"))) m_supabaseServiceKey = env.value(QStringLiteral("SUPABASE_SERVICE_ROLE_KEY"));

    // 2. Search candidate .env and .env.local files in priority order
    QStringList searchPaths;

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
                qDebug() << "[VeloAssetWidget] Loaded environment credentials from:" << path;
                break;
            }
        }
    }

    if (m_supabaseUrl.isEmpty() && AuthManager::instance() && !AuthManager::instance()->supabaseUrl().isEmpty()) {
        m_supabaseUrl = AuthManager::instance()->supabaseUrl();
        m_supabaseAnonKey = AuthManager::instance()->supabaseAnonKey();
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
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        loadEnvCredentials();
    }
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
    stopAudioPreview();
    slotSearchClicked();
}

void VeloAssetWidget::searchStock(const QString &category, const QString &query, int page)
{
    stopAudioPreview();
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        loadEnvCredentials();
    }
    if (m_supabaseUrl.isEmpty()) {
        m_stockStatusLabel->setText(i18n("Supabase URL not configured in .env.local"));
        return;
    }

    m_stockStatusLabel->setText(i18n("Searching %1...", category));
    m_resultsList->clear();
    m_currentAssets.clear();

    if (category == QStringLiteral("videos")) {
        QString path = query.isEmpty()
            ? QStringLiteral("pexels-proxy?type=videos&page=%1&per_page=24").arg(page)
            : QStringLiteral("pexels-proxy?type=videos&query=%1&page=%2&per_page=24").arg(QUrl::toPercentEncoding(query)).arg(page);
        
        QNetworkRequest req = createSupabaseRequest(path);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(i18n("Error: %1", reply->errorString()));
            }
        });
    } else if (category == QStringLiteral("images")) {
        QString path = query.isEmpty()
            ? QStringLiteral("pexels-proxy?type=images&page=%1&per_page=24").arg(page)
            : QStringLiteral("pexels-proxy?type=images&query=%1&page=%2&per_page=24").arg(QUrl::toPercentEncoding(query)).arg(page);
        
        QNetworkRequest req = createSupabaseRequest(path);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(i18n("Error: %1", reply->errorString()));
            }
        });
    } else if (category == QStringLiteral("sfx")) {
        QString qStr = query.isEmpty() ? QStringLiteral("*") : query;
        QString path = QStringLiteral("freesound-proxy?query=%1&page=%2")
            .arg(QUrl::toPercentEncoding(qStr))
            .arg(page);
        
        QNetworkRequest req = createSupabaseRequest(path);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(i18n("Error: %1", reply->errorString()));
            }
        });
    } else if (category == QStringLiteral("gifs")) {
        QString path = query.isEmpty()
            ? QStringLiteral("giphy-proxy?action=trending&limit=24")
            : QStringLiteral("giphy-proxy?action=search&q=%1&limit=24").arg(QUrl::toPercentEncoding(query));
        
        QNetworkRequest req = createSupabaseRequest(path);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(i18n("Error: %1", reply->errorString()));
            }
        });
    } else if (category == QStringLiteral("stickers")) {
        QNetworkRequest req = createSupabaseRequest(QStringLiteral("pixabay-vault"));
        QJsonObject body;
        body[QStringLiteral("query")] = query.isEmpty() ? QStringLiteral("popular") : query;
        body[QStringLiteral("assetType")] = QStringLiteral("images");
        body[QStringLiteral("perPage")] = 24;

        QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply, category]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                parseAndDisplaySearchResults(reply->readAll(), category);
            } else {
                m_stockStatusLabel->setText(i18n("Error: %1", reply->errorString()));
            }
        });
    }
}

void VeloAssetWidget::fetchThumbnailAsync(const QString &assetId, const QString &thumbUrl, QListWidgetItem *item)
{
    if (thumbUrl.isEmpty() || !item) return;

    QNetworkRequest req((QUrl(thumbUrl)));
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, item, assetId]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray imgBytes = reply->readAll();
            QPixmap pixmap;
            if (pixmap.loadFromData(imgBytes)) {
                QPixmap scaled = pixmap.scaled(QSize(130, 80), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                item->setIcon(QIcon(scaled));

                // If this item is currently selected in preview, update preview image as well
                int currentRow = m_resultsList->currentRow();
                if (currentRow >= 0 && currentRow < m_currentAssets.size()) {
                    if (m_currentAssets[currentRow].id == assetId) {
                        m_previewImageLabel->setPixmap(pixmap.scaled(m_previewImageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    }
                }
            }
        }
    });
}

void VeloAssetWidget::parseAndDisplaySearchResults(const QByteArray &data, const QString &category)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();

    m_currentAssets.clear();
    m_resultsList->clear();

    if (category == QStringLiteral("videos")) {
        QJsonArray vids;
        if (root.contains(QStringLiteral("data")) && root[QStringLiteral("data")].isArray()) {
            vids = root[QStringLiteral("data")].toArray();
        } else if (root.contains(QStringLiteral("videos")) && root[QStringLiteral("videos")].isArray()) {
            vids = root[QStringLiteral("videos")].toArray();
        }

        for (const auto &vVal : vids) {
            QJsonObject v = vVal.toObject();
            StockAssetItem item;

            if (v[QStringLiteral("id")].isDouble()) {
                item.id = QString::number(v[QStringLiteral("id")].toVariant().toLongLong());
            } else {
                item.id = v[QStringLiteral("id")].toString();
            }

            if (v.contains(QStringLiteral("title")) && !v[QStringLiteral("title")].toString().isEmpty()) {
                item.title = v[QStringLiteral("title")].toString();
            } else if (v.contains(QStringLiteral("metadata"))) {
                QJsonObject meta = v[QStringLiteral("metadata")].toObject();
                QString userName = meta[QStringLiteral("user")].toObject()[QStringLiteral("name")].toString();
                item.title = userName.isEmpty() ? QStringLiteral("Pexels Video #%1").arg(item.id) : QStringLiteral("Video by %1").arg(userName);
            } else {
                item.title = QStringLiteral("Pexels Video #%1").arg(item.id);
            }

            item.kind = QStringLiteral("video");
            item.provider = QStringLiteral("Pexels");

            if (v.contains(QStringLiteral("preview")) && !v[QStringLiteral("preview")].toString().isEmpty()) {
                item.previewUrl = v[QStringLiteral("preview")].toString();
            } else if (v.contains(QStringLiteral("image"))) {
                item.previewUrl = v[QStringLiteral("image")].toString();
            } else if (v.contains(QStringLiteral("thumbnailUrl"))) {
                item.previewUrl = v[QStringLiteral("thumbnailUrl")].toString();
            }

            if (v.contains(QStringLiteral("details"))) {
                QJsonObject details = v[QStringLiteral("details")].toObject();
                item.downloadUrl = details[QStringLiteral("src")].toString();
                item.width = details[QStringLiteral("width")].toInt(1920);
                item.height = details[QStringLiteral("height")].toInt(1080);
                item.duration = details[QStringLiteral("duration")].toDouble(10.0);
            } else {
                item.width = v[QStringLiteral("width")].toInt(1920);
                item.height = v[QStringLiteral("height")].toInt(1080);
                item.duration = v[QStringLiteral("duration")].toDouble(10.0);

                QJsonArray files = v[QStringLiteral("video_files")].toArray();
                if (files.isEmpty() && v.contains(QStringLiteral("metadata"))) {
                    files = v[QStringLiteral("metadata")].toObject()[QStringLiteral("video_files")].toArray();
                }
                for (const auto &fVal : files) {
                    QJsonObject f = fVal.toObject();
                    QString link = f[QStringLiteral("link")].toString();
                    if (link.contains(QStringLiteral(".mp4")) || f[QStringLiteral("file_type")].toString() == QStringLiteral("video/mp4")) {
                        item.downloadUrl = link;
                        if (f[QStringLiteral("quality")].toString() == QStringLiteral("hd")) break;
                    }
                }
            }

            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                auto *lItem = new QListWidgetItem(QStringLiteral("%1s\n%2x%3").arg(int(item.duration)).arg(item.width).arg(item.height), m_resultsList);
                lItem->setToolTip(QStringLiteral("%1 (%2x%3, %4s)").arg(item.title).arg(item.width).arg(item.height).arg(int(item.duration)));
                fetchThumbnailAsync(item.id, item.previewUrl, lItem);
            }
        }
    } else if (category == QStringLiteral("images")) {
        QJsonArray photos;
        if (root.contains(QStringLiteral("data")) && root[QStringLiteral("data")].isArray()) {
            photos = root[QStringLiteral("data")].toArray();
        } else if (root.contains(QStringLiteral("photos")) && root[QStringLiteral("photos")].isArray()) {
            photos = root[QStringLiteral("photos")].toArray();
        } else if (root.contains(QStringLiteral("hits")) && root[QStringLiteral("hits")].isArray()) {
            photos = root[QStringLiteral("hits")].toArray();
        }

        for (const auto &pVal : photos) {
            QJsonObject p = pVal.toObject();
            StockAssetItem item;

            if (p[QStringLiteral("id")].isDouble()) {
                item.id = QString::number(p[QStringLiteral("id")].toVariant().toLongLong());
            } else {
                item.id = p[QStringLiteral("id")].toString();
            }

            if (p.contains(QStringLiteral("alt")) && !p[QStringLiteral("alt")].toString().isEmpty()) {
                item.title = p[QStringLiteral("alt")].toString();
            } else if (p.contains(QStringLiteral("title")) && !p[QStringLiteral("title")].toString().isEmpty()) {
                item.title = p[QStringLiteral("title")].toString();
            } else if (p.contains(QStringLiteral("tags")) && !p[QStringLiteral("tags")].toString().isEmpty()) {
                item.title = p[QStringLiteral("tags")].toString();
            } else {
                item.title = QStringLiteral("Photo #%1").arg(item.id);
            }

            item.kind = QStringLiteral("image");
            item.provider = QStringLiteral("Pexels");

            if (p.contains(QStringLiteral("details"))) {
                QJsonObject details = p[QStringLiteral("details")].toObject();
                item.downloadUrl = details[QStringLiteral("src")].toString();
                item.width = details[QStringLiteral("width")].toInt(1920);
                item.height = details[QStringLiteral("height")].toInt(1080);
                item.previewUrl = p[QStringLiteral("preview")].toString();
            } else if (p.contains(QStringLiteral("src"))) {
                QJsonObject src = p[QStringLiteral("src")].toObject();
                item.previewUrl = src[QStringLiteral("medium")].toString();
                item.downloadUrl = src[QStringLiteral("large2x")].toString();
                if (item.downloadUrl.isEmpty()) item.downloadUrl = src[QStringLiteral("original")].toString();
                item.width = p[QStringLiteral("width")].toInt();
                item.height = p[QStringLiteral("height")].toInt();
            } else if (p.contains(QStringLiteral("previewUrl"))) {
                item.previewUrl = p[QStringLiteral("previewUrl")].toString();
                item.downloadUrl = p[QStringLiteral("downloadUrl")].toString();
                QJsonObject dims = p[QStringLiteral("dimensions")].toObject();
                item.width = dims[QStringLiteral("width")].toInt();
                item.height = dims[QStringLiteral("height")].toInt();
            }

            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                auto *lItem = new QListWidgetItem(QStringLiteral("%1x%2").arg(item.width).arg(item.height), m_resultsList);
                lItem->setToolTip(item.title);
                fetchThumbnailAsync(item.id, item.previewUrl, lItem);
            }
        }
    } else if (category == QStringLiteral("sfx")) {
        QJsonArray sfxList;
        if (root.contains(QStringLiteral("soundEffects")) && root[QStringLiteral("soundEffects")].isArray()) {
            sfxList = root[QStringLiteral("soundEffects")].toArray();
        } else if (root.contains(QStringLiteral("data")) && root[QStringLiteral("data")].isArray()) {
            sfxList = root[QStringLiteral("data")].toArray();
        }

        for (const auto &sVal : sfxList) {
            QJsonObject s = sVal.toObject();
            StockAssetItem item;

            if (s[QStringLiteral("id")].isDouble()) {
                item.id = QString::number(s[QStringLiteral("id")].toVariant().toLongLong());
            } else {
                item.id = s[QStringLiteral("id")].toString();
            }

            item.title = s[QStringLiteral("name")].toString();
            if (item.title.isEmpty()) item.title = s[QStringLiteral("title")].toString();
            if (item.title.isEmpty()) item.title = QStringLiteral("SFX #%1").arg(item.id);

            item.kind = QStringLiteral("audio");
            item.provider = QStringLiteral("Freesound");

            if (s.contains(QStringLiteral("details"))) {
                item.downloadUrl = s[QStringLiteral("details")].toObject()[QStringLiteral("src")].toString();
            } else if (s.contains(QStringLiteral("downloadUrl"))) {
                item.downloadUrl = s[QStringLiteral("downloadUrl")].toString();
            } else if (s.contains(QStringLiteral("previews"))) {
                item.downloadUrl = s[QStringLiteral("previews")].toObject()[QStringLiteral("preview-hq-mp3")].toString();
            }

            if (s.contains(QStringLiteral("metadata"))) {
                item.duration = s[QStringLiteral("metadata")].toObject()[QStringLiteral("duration")].toDouble(1.0);
            } else {
                item.duration = s[QStringLiteral("duration")].toDouble(1.0);
            }

            item.previewUrl = QString();

            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                auto *lItem = new QListWidgetItem(QStringLiteral("%1\n(%2s)").arg(item.title.left(18)).arg(item.duration, 0, 'f', 1), m_resultsList);
                lItem->setIcon(QIcon::fromTheme(QStringLiteral("audio-x-generic")));
                lItem->setToolTip(QStringLiteral("%1 (%2s)").arg(item.title).arg(item.duration, 0, 'f', 1));
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
            item.provider = QStringLiteral("Giphy");

            QJsonObject images = g[QStringLiteral("images")].toObject();
            QJsonObject original = images[QStringLiteral("original")].toObject();
            item.previewUrl = images[QStringLiteral("fixed_height_small")].toObject()[QStringLiteral("url")].toString();
            item.downloadUrl = original[QStringLiteral("url")].toString();
            item.width = original[QStringLiteral("width")].toString().toInt();
            item.height = original[QStringLiteral("height")].toString().toInt();

            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                auto *lItem = new QListWidgetItem(item.title.left(16), m_resultsList);
                lItem->setToolTip(item.title);
                fetchThumbnailAsync(item.id, item.previewUrl, lItem);
            }
        }
    } else if (category == QStringLiteral("stickers")) {
        QJsonArray stickers = root[QStringLiteral("data")].toArray();
        for (const auto &stVal : stickers) {
            QJsonObject st = stVal.toObject();
            StockAssetItem item;
            item.id = st[QStringLiteral("id")].toString();
            item.title = st[QStringLiteral("tags")].toString();
            if (item.title.isEmpty()) item.title = QStringLiteral("Sticker #%1").arg(item.id);
            item.kind = QStringLiteral("image");
            item.provider = QStringLiteral("Pixabay");
            item.previewUrl = st[QStringLiteral("previewUrl")].toString();
            item.downloadUrl = st[QStringLiteral("downloadUrl")].toString();
            QJsonObject dims = st[QStringLiteral("dimensions")].toObject();
            item.width = dims[QStringLiteral("width")].toInt();
            item.height = dims[QStringLiteral("height")].toInt();

            if (!item.downloadUrl.isEmpty()) {
                m_currentAssets.append(item);
                auto *lItem = new QListWidgetItem(item.title.left(16), m_resultsList);
                lItem->setToolTip(item.title);
                fetchThumbnailAsync(item.id, item.previewUrl, lItem);
            }
        }
    }

    m_stockStatusLabel->setText(i18n("Found %1 assets", m_currentAssets.count()));
    if (m_resultsList->count() > 0) {
        m_resultsList->setCurrentRow(0);
    }
}

void VeloAssetWidget::slotAssetSelected(QListWidgetItem *current, QListWidgetItem *)
{
    stopAudioPreview();

    if (!current) {
        m_previewTitleLabel->setText(i18n("No asset selected"));
        m_previewDetailsLabel->setText(i18n("Resolution: -- | Duration: -- | Provider: --"));
        m_previewImageLabel->setText(i18n("Select an asset to preview"));
        m_audioControlsWidget->setVisible(false);
        m_addToBinBtn->setEnabled(false);
        m_insertTimelineBtn->setEnabled(false);
        return;
    }

    int row = m_resultsList->row(current);
    if (row < 0 || row >= m_currentAssets.size()) return;

    const auto &asset = m_currentAssets[row];
    m_previewTitleLabel->setText(asset.title);

    QString durationStr = asset.duration > 0 ? QStringLiteral("%1s").arg(asset.duration, 0, 'f', 1) : QStringLiteral("--");
    QString resStr = (asset.width > 0 && asset.height > 0) ? QStringLiteral("%1x%2").arg(asset.width).arg(asset.height) : QStringLiteral("--");
    m_previewDetailsLabel->setText(i18n("Kind: %1 | Resolution: %2 | Duration: %3 | Provider: %4",
                                        asset.kind.toUpper(), resStr, durationStr, asset.provider));

    if (asset.kind == QStringLiteral("audio")) {
        m_previewImageLabel->setPixmap(QIcon::fromTheme(QStringLiteral("audio-x-generic")).pixmap(96, 96));
        m_currentAudioUrl = asset.downloadUrl;
        m_audioControlsWidget->setVisible(true);
        m_audioProgressSlider->setRange(0, static_cast<int>(asset.duration * 1000));
        m_audioProgressSlider->setValue(0);
        qint64 totalSec = static_cast<qint64>(asset.duration);
        m_audioTimeLabel->setText(QStringLiteral("00:00 / %1:%2")
            .arg(totalSec / 60, 2, 10, QLatin1Char('0'))
            .arg(totalSec % 60, 2, 10, QLatin1Char('0')));
    } else {
        m_audioControlsWidget->setVisible(false);
        if (!current->icon().isNull()) {
            m_previewImageLabel->setPixmap(current->icon().pixmap(160, 95));
        } else {
            m_previewImageLabel->setText(i18n("Loading preview..."));
        }
    }

    m_addToBinBtn->setEnabled(true);
    m_insertTimelineBtn->setEnabled(true);
}

void VeloAssetWidget::slotToggleAudioPreview()
{
    if (!m_audioPlayer) return;

    if (m_audioPlayer->playbackState() == QMediaPlayer::PlayingState) {
        m_audioPlayer->pause();
    } else {
        if (m_currentAudioUrl.isEmpty()) return;
        if (m_audioPlayer->source().toString() != m_currentAudioUrl) {
            m_audioPlayer->setSource(QUrl(m_currentAudioUrl));
        }
        m_audioPlayer->play();
    }
}

void VeloAssetWidget::slotAudioStateChanged(QMediaPlayer::PlaybackState state)
{
    if (state == QMediaPlayer::PlayingState) {
        if (m_isVoiceResultPlaying) {
            m_voicePlayPauseBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-pause")));
            m_voicePlayPauseBtn->setText(i18n("Pause"));
        } else {
            m_audioPlayPauseBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-pause")));
            m_audioPlayPauseBtn->setText(i18n("Pause"));
        }
    } else {
        m_audioPlayPauseBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
        m_audioPlayPauseBtn->setText(i18n("Audition"));
        m_voicePlayPauseBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
        m_voicePlayPauseBtn->setText(i18n("Audition"));
        m_isVoiceResultPlaying = false;
    }
}

void VeloAssetWidget::slotAudioPositionChanged(qint64 position)
{
    if (!m_isSliderSeeking) {
        if (m_isVoiceResultPlaying) {
            m_voiceProgressSlider->setValue(static_cast<int>(position));
        } else {
            m_audioProgressSlider->setValue(static_cast<int>(position));
        }
    }
    qint64 duration = m_audioPlayer->duration();
    if (duration <= 0) {
        if (m_isVoiceResultPlaying && m_lastVoiceDuration > 0) {
            duration = static_cast<qint64>(m_lastVoiceDuration * 1000);
        } else if (m_resultsList->currentRow() >= 0 && m_resultsList->currentRow() < m_currentAssets.size()) {
            duration = static_cast<qint64>(m_currentAssets[m_resultsList->currentRow()].duration * 1000);
        }
    }
    auto formatTime = [](qint64 ms) -> QString {
        qint64 totalSec = ms / 1000;
        qint64 m = totalSec / 60;
        qint64 s = totalSec % 60;
        return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    };
    QString timeStr = QStringLiteral("%1 / %2").arg(formatTime(position), formatTime(duration));
    if (m_isVoiceResultPlaying) {
        m_voiceTimeLabel->setText(timeStr);
    } else {
        m_audioTimeLabel->setText(timeStr);
    }
}

void VeloAssetWidget::slotAudioDurationChanged(qint64 duration)
{
    if (duration > 0) {
        if (m_isVoiceResultPlaying) {
            m_voiceProgressSlider->setRange(0, static_cast<int>(duration));
        } else {
            m_audioProgressSlider->setRange(0, static_cast<int>(duration));
        }
        slotAudioPositionChanged(m_audioPlayer->position());
    }
}

void VeloAssetWidget::slotAudioSeek(int position)
{
    if (m_audioPlayer) {
        m_audioPlayer->setPosition(position);
    }
}

void VeloAssetWidget::stopAudioPreview()
{
    if (m_audioPlayer) {
        m_audioPlayer->stop();
    }
    if (m_audioPlayPauseBtn) {
        m_audioPlayPauseBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
        m_audioPlayPauseBtn->setText(i18n("Audition"));
    }
    if (m_voicePlayPauseBtn) {
        m_voicePlayPauseBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
        m_voicePlayPauseBtn->setText(i18n("Audition"));
    }
    if (m_audioProgressSlider) {
        m_audioProgressSlider->setValue(0);
    }
    if (m_voiceProgressSlider) {
        m_voiceProgressSlider->setValue(0);
    }
    m_isVoiceResultPlaying = false;
}

void VeloAssetWidget::slotToggleVoiceResultAudio()
{
    if (!m_audioPlayer || m_lastVoicePath.isEmpty()) return;

    if (m_audioPlayer->playbackState() == QMediaPlayer::PlayingState && m_isVoiceResultPlaying) {
        m_audioPlayer->pause();
        m_isVoiceResultPlaying = false;
        m_voicePlayPauseBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
        m_voicePlayPauseBtn->setText(i18n("Audition"));
    } else {
        m_isVoiceResultPlaying = true;
        m_audioPlayer->setSource(QUrl::fromLocalFile(m_lastVoicePath));
        m_audioPlayer->play();
        m_voicePlayPauseBtn->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-pause")));
        m_voicePlayPauseBtn->setText(i18n("Pause"));
    }
}

void VeloAssetWidget::slotAddVoiceResultToBin()
{
    if (m_lastVoicePath.isEmpty()) return;

    if (!m_lastVoiceBinId.isEmpty()) {
        m_voiceStatusLabel->setText(i18n("Voiceover is already in Project Bin (ID: %1).", m_lastVoiceBinId));
        return;
    }

    if (pCore && pCore->bin() && pCore->projectItemModel()) {
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        QString createdId = ClipCreator::createClipFromFile(m_lastVoicePath, pCore->bin()->rootFolderId(), pCore->projectItemModel(), undo, redo,
            [this](const QString &binId) {
                if (!binId.isEmpty() && binId != QStringLiteral("-1")) {
                    m_lastVoiceBinId = binId;
                    m_voiceStatusLabel->setText(i18n("Added voiceover to Project Bin (ID: %1).", binId));
                    Q_EMIT assetAddedToBin(binId, m_lastVoicePath);
                }
            });
        if (createdId != QStringLiteral("-1")) {
            m_lastVoiceBinId = createdId;
            pCore->pushUndo(undo, redo, i18nc("@action", "Add clip"));
            m_voiceStatusLabel->setText(i18n("Added voiceover to Project Bin."));
        }
    }
}

void VeloAssetWidget::slotInsertVoiceResultToTimeline()
{
    if (m_lastVoicePath.isEmpty()) return;

    auto doTimelineInsert = [this](const QString &binId) {
        if (binId.isEmpty() || binId == QStringLiteral("-1")) return;
        m_lastVoiceBinId = binId;

        if (pCore && pCore->window() && pCore->window()->getCurrentTimeline()) {
            auto *tc = pCore->window()->getCurrentTimeline()->controller();
            auto tm = pCore->window()->getCurrentTimeline()->model();
            if (tc && tm) {
                int pos = pCore->getMonitorPosition(Kdenlive::ProjectMonitor);
                int tid = -1;
                int active = tc->activeTrack();
                if (active >= 0 && tm->isAudioTrack(active)) {
                    tid = active;
                } else {
                    for (int t : tm->getAllTracksIds()) {
                        if (tm->isAudioTrack(t)) {
                            tid = t;
                            break;
                        }
                    }
                }
                if (tid >= 0) {
                    int newClipId = -1;
                    bool ok = tm->requestClipInsertion(binId, tid, pos, newClipId, true, true, false);
                    if (ok) {
                        m_voiceStatusLabel->setText(i18n("Inserted voiceover onto audio track %1 at frame %2.", tid, pos));
                        Q_EMIT assetInsertedToTimeline(binId, tid, pos);
                    } else {
                        m_voiceStatusLabel->setText(i18n("Could not insert voiceover onto audio track %1 at frame %2.", tid, pos));
                    }
                }
            }
        }
    };

    if (!m_lastVoiceBinId.isEmpty()) {
        doTimelineInsert(m_lastVoiceBinId);
    } else if (pCore && pCore->bin() && pCore->projectItemModel()) {
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        QString createdId = ClipCreator::createClipFromFile(m_lastVoicePath, pCore->bin()->rootFolderId(), pCore->projectItemModel(), undo, redo, doTimelineInsert);
        if (createdId != QStringLiteral("-1")) {
            m_lastVoiceBinId = createdId;
            pCore->pushUndo(undo, redo, i18nc("@action", "Add clip"));
            doTimelineInsert(createdId);
        }
    }
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
        m_voiceStatusLabel->setText(i18n("Please enter text to synthesize speech."));
        return;
    }

    QString voiceId = m_voiceCombo->currentData().toString();
    QString voiceName = m_voiceCombo->currentText();
    double speed = m_speedSlider->value() / 100.0;
    double stability = m_stabilitySlider->value() / 100.0;

    m_generateVoiceBtn->setEnabled(false);
    m_voiceProgressBar->setVisible(true);
    m_voiceProgressBar->setRange(0, 0); // Indeterminate
    m_voiceStatusLabel->setText(i18n("Generating ElevenLabs neural voiceover..."));

    generateVoiceover(text, voiceId, speed, stability, [this, voiceName, text](const QString &localPath, double duration) {
        m_generateVoiceBtn->setEnabled(true);
        m_voiceProgressBar->setVisible(false);
        if (!localPath.isEmpty()) {
            m_lastVoicePath = localPath;
            m_lastVoiceBinId.clear();
            m_lastVoiceDuration = duration;
            m_lastVoiceTitle = QStringLiteral("%1: \"%2\"").arg(voiceName.section(QLatin1Char('('), 0, 0).trimmed(), text.left(45));

            m_voiceResultTitle->setText(m_lastVoiceTitle);
            m_voiceResultDetails->setText(i18n("Duration: %1s | Speed: %2x | Stability: %3",
                QString::number(duration, 'f', 1),
                QString::number(m_speedSlider->value() / 100.0, 'f', 2),
                QString::number(m_stabilitySlider->value() / 100.0, 'f', 2)));
            m_voiceProgressSlider->setRange(0, static_cast<int>(duration * 1000));
            m_voiceProgressSlider->setValue(0);
            qint64 totalSec = static_cast<qint64>(duration);
            m_voiceTimeLabel->setText(QStringLiteral("00:00 / %1:%2")
                .arg(totalSec / 60, 2, 10, QLatin1Char('0'))
                .arg(totalSec % 60, 2, 10, QLatin1Char('0')));

            m_voiceResultBox->setVisible(true);
            m_voiceStatusLabel->setText(i18n("Voiceover synthesized successfully. Ready to audition or add to timeline."));
        } else {
            m_voiceStatusLabel->setText(i18n("Voiceover synthesis failed. Check Supabase connection."));
        }
    });
}

void VeloAssetWidget::generateVoiceover(const QString &text, const QString &voiceNameOrId,
                                       double speed, double stability,
                                       std::function<void(const QString &localPath, double duration)> onComplete)
{
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        loadEnvCredentials();
    }
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        if (onComplete) onComplete(QString(), 0.0);
        return;
    }

    QString voiceId = voiceNameOrId;
    if (m_voiceMap.contains(voiceNameOrId.toLower())) {
        voiceId = m_voiceMap.value(voiceNameOrId.toLower());
    }
    if (voiceId.isEmpty()) {
        voiceId = QStringLiteral("EXAVITQu4vr4xnSDxMaL");
    }

    QNetworkRequest req = createSupabaseRequest(QStringLiteral("generate-voice"));
    QJsonObject body;
    body[QStringLiteral("text")] = text;
    body[QStringLiteral("voiceId")] = voiceId;
    body[QStringLiteral("voice_id")] = voiceId;
    body[QStringLiteral("speed")] = speed;
    body[QStringLiteral("stability")] = stability;
    body[QStringLiteral("similarityBoost")] = 0.75;
    body[QStringLiteral("voice_settings")] = QJsonObject{
        {QStringLiteral("stability"), stability},
        {QStringLiteral("similarity_boost"), 0.75},
        {QStringLiteral("speed"), speed}
    };

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, onComplete]() {
        reply->deleteLater();
        QByteArray respBytes = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(respBytes);

        if (doc.isObject()) {
            QJsonObject root = doc.object();
            bool success = root[QStringLiteral("success")].toBool(false);
            if (!success) {
                QString err = root[QStringLiteral("error")].toString();
                if (err.isEmpty()) err = reply->errorString();
                m_voiceStatusLabel->setText(i18n("Voice synthesis failed: %1", err));
                if (onComplete) onComplete(QString(), 0.0);
                return;
            }

            QString urlStr = root[QStringLiteral("url")].toString();
            double duration = root[QStringLiteral("duration")].toDouble(root[QStringLiteral("durationSeconds")].toDouble(5.0));
            QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            QString outPath = QStringLiteral("%1/voiceover_%2.mp3").arg(tmpDir).arg(QDateTime::currentMSecsSinceEpoch());

            if (urlStr.startsWith(QStringLiteral("data:audio"))) {
                int commaIdx = urlStr.indexOf(QLatin1Char(','));
                QByteArray b64 = urlStr.mid(commaIdx + 1).toUtf8();
                QByteArray audioBytes = QByteArray::fromBase64(b64);
                QFile f(outPath);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(audioBytes);
                    f.close();
                    if (onComplete) onComplete(outPath, duration);
                    return;
                }
            } else if (urlStr.startsWith(QStringLiteral("http"))) {
                QNetworkRequest dlReq((QUrl(urlStr)));
                QNetworkReply *dlReply = m_nam->get(dlReq);
                connect(dlReply, &QNetworkReply::finished, this, [this, dlReply, outPath, duration, onComplete]() {
                    dlReply->deleteLater();
                    if (dlReply->error() == QNetworkReply::NoError) {
                        QFile f(outPath);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(dlReply->readAll());
                            f.close();
                            if (onComplete) onComplete(outPath, duration);
                            return;
                        }
                    }
                    if (onComplete) onComplete(QString(), 0.0);
                });
                return;
            }
        }

        // Fallback for direct binary response
        if (reply->error() == QNetworkReply::NoError && !respBytes.isEmpty()) {
            QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            QString outPath = QStringLiteral("%1/voiceover_%2.mp3").arg(tmpDir).arg(QDateTime::currentMSecsSinceEpoch());
            QFile f(outPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(respBytes);
                f.close();
                if (onComplete) onComplete(outPath, 5.0);
                return;
            }
        }

        m_voiceStatusLabel->setText(i18n("Voice synthesis failed: %1", reply->errorString()));
        if (onComplete) onComplete(QString(), 0.0);
    });
}

void VeloAssetWidget::downloadAndIngest(const QString &url, const QString &name, const QString &kind,
                                       bool insertToTimeline, int trackId, int targetFrame,
                                       std::function<void(const QString &clipId, const QString &localPath)> onComplete)
{
    if (url.isEmpty()) return;

    m_stockStatusLabel->setText(i18n("Downloading %1...", name.left(25)));

    QUrl qurl(url);
    QNetworkRequest req(qurl);
    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, name, kind, insertToTimeline, trackId, targetFrame, onComplete]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_stockStatusLabel->setText(i18n("Download failed: %1", reply->errorString()));
            return;
        }

        QByteArray data = reply->readAll();
        QString ext = QStringLiteral("mp4");
        if (kind == QStringLiteral("image")) ext = QStringLiteral("jpg");
        else if (kind == QStringLiteral("gif")) ext = QStringLiteral("gif");
        else if (kind == QStringLiteral("audio") || kind == QStringLiteral("sfx")) ext = QStringLiteral("mp3");

        QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString safeName = name;
        safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"));
        QString filePath = QStringLiteral("%1/%2_%3.%4")
            .arg(tmpDir, safeName.left(20))
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(ext);

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            m_stockStatusLabel->setText(i18n("Failed to write to %1", filePath));
            return;
        }
        file.write(data);
        file.close();

        // Import to Project Bin and optionally insert into timeline
        if (pCore && pCore->bin() && pCore->projectItemModel()) {
            Fun undo = []() { return true; };
            Fun redo = []() { return true; };

            auto insertCallback = [this, insertToTimeline, targetFrame, trackId, kind, name, filePath, onComplete](const QString &binId) {
                if (binId.isEmpty() || binId == QStringLiteral("-1")) {
                    m_stockStatusLabel->setText(i18n("Failed to create bin clip for %1", name.left(25)));
                    return;
                }

                m_stockStatusLabel->setText(i18n("Imported %1 to Project Bin.", name.left(25)));
                Q_EMIT assetAddedToBin(binId, filePath);

                // Insert to timeline if requested
                if (insertToTimeline && pCore && pCore->window() && pCore->window()->getCurrentTimeline()) {
                    auto *tc = pCore->window()->getCurrentTimeline()->controller();
                    auto tm = pCore->window()->getCurrentTimeline()->model();
                    if (tc && tm) {
                        int pos = targetFrame >= 0 ? targetFrame : pCore->getMonitorPosition(Kdenlive::ProjectMonitor);
                        int tid = trackId;
                        if (tid < 0) {
                            bool wantsAudio = (kind == QStringLiteral("audio") || kind == QStringLiteral("sfx"));
                            int active = tc->activeTrack();
                            if (active >= 0 && tm->isAudioTrack(active) == wantsAudio) {
                                tid = active;
                            } else {
                                for (int t : tm->getAllTracksIds()) {
                                    if (tm->isAudioTrack(t) == wantsAudio) {
                                        tid = t;
                                        break;
                                    }
                                }
                            }
                        }
                        if (tid >= 0) {
                            int newClipId = -1;
                            bool ok = tm->requestClipInsertion(binId, tid, pos, newClipId, true, true, false);
                            if (ok) {
                                m_stockStatusLabel->setText(i18n("Inserted %1 onto timeline track %2 at frame %3.", name.left(20), tid, pos));
                                Q_EMIT assetInsertedToTimeline(binId, tid, pos);
                            } else {
                                m_stockStatusLabel->setText(i18n("Imported %1 to Bin, but could not place on track %2.", name.left(20), tid));
                            }
                        }
                    }
                }

                if (onComplete) {
                    onComplete(binId, filePath);
                }
            };

            QString createdBinId = ClipCreator::createClipFromFile(filePath, pCore->bin()->rootFolderId(), pCore->projectItemModel(), undo, redo, insertCallback);
            if (createdBinId != QStringLiteral("-1")) {
                pCore->pushUndo(undo, redo, i18nc("@action", "Add clip"));
            }
        }
    });
}
