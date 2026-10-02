/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#include "aichatwidget.h"
#include <KLocalizedString>
#include <QDateTime>
#include <QScrollBar>
#include <QGroupBox>
#include <QFormLayout>
#include <QIcon>
#include <QFrame>

#include "aidispatcher.h"
#include "aicommandrouter.h"

AIChatWidget::AIChatWidget(QWidget *parent)
    : QWidget(parent)
    , m_dispatcher(new AIDispatcher(this))
    , m_router(new AICommandRouter(this))
    , m_statusTimer(new QTimer(this))
{
    setupUi();

    connect(this, &AIChatWidget::sendPromptRequested, m_dispatcher, &AIDispatcher::sendPrompt);
    connect(m_dispatcher, &AIDispatcher::responseReceived, this, &AIChatWidget::slotResponseReceived);
    connect(m_dispatcher, &AIDispatcher::metricsUpdated, this, &AIChatWidget::slotMetricsUpdated);
    connect(m_dispatcher, &AIDispatcher::errorOccurred, this, [&](const QString &err) {
        appendSystemMessage(i18n("Network error: %1", err));
        slotRequestFinished();
    });
    connect(m_dispatcher, &AIDispatcher::requestStarted, this, &AIChatWidget::slotRequestStarted);
    connect(m_dispatcher, &AIDispatcher::requestFinished, this, &AIChatWidget::slotRequestFinished);
    connect(m_dispatcher, &AIDispatcher::modelsLoaded, this, &AIChatWidget::slotModelsLoaded);

    connect(m_router, &AICommandRouter::executionFinished, this, &AIChatWidget::slotExecutionFinished);
    connect(m_router, &AICommandRouter::dataOutput, this, &AIChatWidget::slotToolDataOutput);

    connect(m_statusTimer, &QTimer::timeout, this, &AIChatWidget::slotUpdateLiveTimer);

    // Initial metrics setup
    updateMetricsDisplay(0, 0, m_dispatcher->currentModel());

    // Initial fetch of live models from ai-proxy
    m_dispatcher->fetchAvailableModels();
}

void AIChatWidget::setupUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_stackedWidget = new QStackedWidget(this);

    m_workspacePage = new QWidget(this);
    setupWorkspacePage(m_workspacePage);
    m_stackedWidget->addWidget(m_workspacePage);

    m_settingsPage = new QWidget(this);
    setupSettingsPage(m_settingsPage);
    m_stackedWidget->addWidget(m_settingsPage);

    rootLayout->addWidget(m_stackedWidget);

    m_stackedWidget->setCurrentIndex(0);
    updateModeBadge();
}

void AIChatWidget::setupWorkspacePage(QWidget *page)
{
    page->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1e1e1e; color: #cccccc; }"
    ));

    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // ── Header Bar ──────────────────────────────────────────────────────────
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(6);

    auto *brandLabel = new QLabel(QStringLiteral("<b>VidMate Agent</b>"), page);
    brandLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #e1e4e8;"));

    m_modeBadge = new QLabel(page);
    m_modeBadge->setStyleSheet(QStringLiteral(
        "background-color: #252526; color: #4ec9b0; border: 1px solid #3c3c3c; "
        "border-radius: 2px; padding: 2px 6px; font-size: 10px; font-weight: 600; font-family: monospace;"
    ));

    m_engineTargetSelector = new QComboBox(page);
    m_engineTargetSelector->addItem(i18n("Auto (Kdenlive & Natron)"), QStringLiteral("auto"));
    m_engineTargetSelector->addItem(i18n("Kdenlive (NLE / Cuts)"), QStringLiteral("kdenlive"));
    m_engineTargetSelector->addItem(i18n("Natron (VFX / Compositing)"), QStringLiteral("natron"));
    m_engineTargetSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_engineTargetSelector->setStyleSheet(QStringLiteral(
        "QComboBox { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 2px; padding: 3px 6px; color: #cccccc; font-size: 11px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #252526; border: 1px solid #3c3c3c; selection-background-color: #0e639c; color: #cccccc; }"
    ));

    const QString headerBtnStyle = QStringLiteral(
        "QPushButton { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 2px; padding: 3px 8px; color: #cccccc; font-size: 11px; }"
        "QPushButton:hover { background-color: #2d2d2d; border-color: #007acc; color: #ffffff; }"
        "QPushButton:pressed { background-color: #0e639c; }"
    );

    m_assetStudioBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-media-playlist")), i18n("Assets"), page);
    m_assetStudioBtn->setToolTip(i18n("Open Stock Media & Voiceover Studio"));
    m_assetStudioBtn->setStyleSheet(headerBtnStyle);
    connect(m_assetStudioBtn, &QPushButton::clicked, this, &AIChatWidget::openAssetStudioRequested);

    m_settingsBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), i18n("Settings"), page);
    m_settingsBtn->setToolTip(i18n("Open Agent Settings"));
    m_settingsBtn->setStyleSheet(headerBtnStyle);
    connect(m_settingsBtn, &QPushButton::clicked, this, &AIChatWidget::slotToggleSettings);

    m_clearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), QString(), page);
    m_clearBtn->setToolTip(i18n("Clear History"));
    m_clearBtn->setStyleSheet(headerBtnStyle);
    connect(m_clearBtn, &QPushButton::clicked, this, &AIChatWidget::slotClearChat);

    headerLayout->addWidget(brandLabel);
    headerLayout->addWidget(m_modeBadge);
    headerLayout->addWidget(m_engineTargetSelector, 1);
    headerLayout->addWidget(m_assetStudioBtn);
    headerLayout->addWidget(m_settingsBtn);
    headerLayout->addWidget(m_clearBtn);
    mainLayout->addLayout(headerLayout);

    // ── Live Run Status Bar (Hidden when idle) ──────────────────────────────
    m_liveStatusBar = new QFrame(page);
    m_liveStatusBar->setObjectName(QStringLiteral("liveStatusBar"));
    m_liveStatusBar->setStyleSheet(QStringLiteral(
        "QFrame#liveStatusBar { background-color: #252526; border: 1px solid #007acc; border-radius: 2px; padding: 4px; }"
    ));
    auto *liveLayout = new QHBoxLayout(m_liveStatusBar);
    liveLayout->setContentsMargins(6, 3, 6, 3);
    liveLayout->setSpacing(8);

    m_liveStatusDot = new QLabel(QStringLiteral("●"), m_liveStatusBar);
    m_liveStatusDot->setStyleSheet(QStringLiteral("color: #007acc; font-size: 11px;"));

    m_liveStatusText = new QLabel(i18n("Processing timeline task..."), m_liveStatusBar);
    m_liveStatusText->setStyleSheet(QStringLiteral("color: #d4d4d4; font-size: 11px;"));

    m_liveStatusTimer = new QLabel(QStringLiteral("0.0s"), m_liveStatusBar);
    m_liveStatusTimer->setStyleSheet(QStringLiteral("color: #9cdcfe; font-family: monospace; font-size: 11px;"));

    liveLayout->addWidget(m_liveStatusDot);
    liveLayout->addWidget(m_liveStatusText, 1);
    liveLayout->addWidget(m_liveStatusTimer);
    m_liveStatusBar->setVisible(false);
    mainLayout->addWidget(m_liveStatusBar);

    // ── Message Stream (Flat borderless stream) ─────────────────────────────
    m_messageStream = new QTextBrowser(page);
    m_messageStream->setOpenExternalLinks(true);
    m_messageStream->setReadOnly(true);
    m_messageStream->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #2d2d2d;"
        "  border-radius: 2px;"
        "  padding: 8px;"
        "  font-size: 12px;"
        "  line-height: 1.5;"
        "  color: #d4d4d4;"
        "}"
    ));
    mainLayout->addWidget(m_messageStream, 1);

    // ── Proposal Review Card (Shown in Ask Mode) ────────────────────────────
    m_proposalCard = new QFrame(page);
    m_proposalCard->setObjectName(QStringLiteral("proposalCard"));
    m_proposalCard->setStyleSheet(QStringLiteral(
        "QFrame#proposalCard {"
        "  background-color: #252526;"
        "  border: 1px solid #3c3c3c;"
        "  border-left: 3px solid #007acc;"
        "  border-radius: 2px;"
        "  padding: 8px;"
        "}"
    ));
    auto *proposalLayout = new QVBoxLayout(m_proposalCard);
    proposalLayout->setContentsMargins(8, 6, 8, 6);
    proposalLayout->setSpacing(4);

    m_proposalTitle = new QLabel(i18n("Proposed Timeline Modification"), m_proposalCard);
    m_proposalTitle->setStyleSheet(QStringLiteral("font-weight: bold; color: #4ec9b0; font-size: 11px;"));

    m_proposalSummary = new QLabel(m_proposalCard);
    m_proposalSummary->setWordWrap(true);
    m_proposalSummary->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 11px;"));

    auto *proposalBtnLayout = new QHBoxLayout();
    proposalBtnLayout->setSpacing(6);
    m_applyProposalBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok")), i18n("Apply Changes"), m_proposalCard);
    m_applyProposalBtn->setStyleSheet(QStringLiteral("background-color: #0e639c; color: white; font-weight: bold; border: none; border-radius: 2px; padding: 4px 12px; font-size: 11px;"));
    connect(m_applyProposalBtn, &QPushButton::clicked, this, &AIChatWidget::slotApplyProposal);

    m_rejectProposalBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-cancel")), i18n("Reject"), m_proposalCard);
    m_rejectProposalBtn->setStyleSheet(QStringLiteral("background-color: #2d2d2d; color: #cccccc; border: 1px solid #3c3c3c; border-radius: 2px; padding: 4px 12px; font-size: 11px;"));
    connect(m_rejectProposalBtn, &QPushButton::clicked, this, &AIChatWidget::slotRejectProposal);

    proposalBtnLayout->addStretch(1);
    proposalBtnLayout->addWidget(m_applyProposalBtn);
    proposalBtnLayout->addWidget(m_rejectProposalBtn);

    proposalLayout->addWidget(m_proposalTitle);
    proposalLayout->addWidget(m_proposalSummary);
    proposalLayout->addLayout(proposalBtnLayout);
    m_proposalCard->setVisible(false);
    mainLayout->addWidget(m_proposalCard);

    // ── Quick Workflow Starters ─────────────────────────────────────────────
    auto *quickScroll = new QScrollArea(page);
    quickScroll->setFixedHeight(32);
    quickScroll->setWidgetResizable(true);
    quickScroll->setFrameShape(QFrame::NoFrame);
    quickScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quickScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quickScroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *quickContainer = new QWidget(quickScroll);
    quickContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *quickLayout = new QHBoxLayout(quickContainer);
    quickLayout->setContentsMargins(0, 0, 0, 0);
    quickLayout->setSpacing(5);

    const struct { QString label; QString prompt; } starters[] = {
        {i18n("Split Scenes"), QStringLiteral("Detect all scene and shot changes using PySceneDetect and split clips on the timeline.")},
        {i18n("Auto-Cut Silence"), QStringLiteral("Analyze audio track and remove silence intervals longer than 500ms.")},
        {i18n("Generate Voiceover"), QStringLiteral("Generate ElevenLabs neural voiceover for the current scene script.")},
        {i18n("Stock B-Roll"), QStringLiteral("Search Pexels for cinematic 4K B-roll footage and insert into Track 2.")},
        {i18n("Natron VFX"), QStringLiteral("Create a Natron node graph for background rotoscoping and chroma key compositing.")},
        {i18n("Subtitles"), QStringLiteral("Generate synchronized subtitles for the speech track.")}
    };

    for (const auto &s : starters) {
        auto *btn = new QPushButton(s.label, quickContainer);
        btn->setProperty("promptText", s.prompt);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: #252526; border: 1px solid #333333; border-radius: 2px; padding: 2px 8px; font-size: 11px; color: #9da5b4; }"
            "QPushButton:hover { background-color: #2d2d2d; border-color: #007acc; color: #ffffff; }"
        ));
        connect(btn, &QPushButton::clicked, this, &AIChatWidget::slotQuickActionTriggered);
        quickLayout->addWidget(btn);
    }
    quickLayout->addStretch(1);
    quickScroll->setWidget(quickContainer);
    mainLayout->addWidget(quickScroll);

    // ── Input & Send Row ────────────────────────────────────────────────────
    auto *inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(6);

    m_promptInput = new QLineEdit(page);
    m_promptInput->setPlaceholderText(i18n("Direct AI edits, cuts, transitions, or ask questions..."));
    m_promptInput->setClearButtonEnabled(true);
    m_promptInput->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background-color: #252526;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 2px;"
        "  padding: 6px 10px;"
        "  font-size: 12px;"
        "  color: #cccccc;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid #007acc;"
        "}"
    ));
    connect(m_promptInput, &QLineEdit::returnPressed, this, &AIChatWidget::slotSendMessage);

    m_sendBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-send")), i18n("Send"), page);
    m_sendBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #0e639c;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 2px;"
        "  padding: 6px 14px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "  background-color: #1177bb;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #094771;"
        "}"
    ));
    connect(m_sendBtn, &QPushButton::clicked, this, &AIChatWidget::slotSendMessage);

    inputLayout->addWidget(m_promptInput, 1);
    inputLayout->addWidget(m_sendBtn);
    mainLayout->addLayout(inputLayout);

    // ── Bottom Metrics Footer (Model, Tokens, Latency) ──────────────────────
    m_metricsFooter = new QWidget(page);
    m_metricsFooter->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *metricsLayout = new QHBoxLayout(m_metricsFooter);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(6);
    metricsLayout->addStretch(1);

    const QString tagStyle = QStringLiteral(
        "QLabel {"
        "  font-family: monospace;"
        "  font-size: 10px;"
        "  color: #9da5b4;"
        "  background-color: #21252b;"
        "  border: 1px solid #333842;"
        "  border-radius: 2px;"
        "  padding: 1px 6px;"
        "}"
    );

    m_modelTag = new QLabel(m_metricsFooter);
    m_modelTag->setStyleSheet(tagStyle);

    m_tokensTag = new QLabel(m_metricsFooter);
    m_tokensTag->setStyleSheet(tagStyle);

    m_latencyTag = new QLabel(m_metricsFooter);
    m_latencyTag->setStyleSheet(tagStyle);

    metricsLayout->addWidget(m_modelTag);
    metricsLayout->addWidget(m_tokensTag);
    metricsLayout->addWidget(m_latencyTag);
    mainLayout->addWidget(m_metricsFooter);

    // Initial greeting
    appendSystemMessage(i18n("VidMate AI Agent ready. Direct edits, camera cuts, or Natron VFX pipelines."));
}

void AIChatWidget::setupSettingsPage(QWidget *page)
{
    page->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1e1e1e; color: #cccccc; }"
        "QGroupBox { font-weight: bold; border: 1px solid #333333; border-radius: 2px; margin-top: 10px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 4px; color: #9cdcfe; }"
        "QComboBox { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 2px; padding: 3px 6px; color: #cccccc; }"
        "QLineEdit { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 2px; padding: 4px 6px; color: #cccccc; }"
        "QLineEdit:focus { border: 1px solid #007acc; }"
    ));

    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(8, 8, 8, 8);
    pageLayout->setSpacing(8);

    // Header with Back Button
    auto *topBar = new QHBoxLayout();
    auto *backBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")), i18n("Back to Workspace"), page);
    backBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 2px; padding: 4px 10px; color: #cccccc; font-size: 11px; }"
        "QPushButton:hover { background-color: #2d2d2d; border-color: #007acc; color: #ffffff; }"
    ));
    connect(backBtn, &QPushButton::clicked, this, &AIChatWidget::slotToggleSettings);

    auto *titleLabel = new QLabel(QStringLiteral("<b>Agent Configuration</b>"), page);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #e1e4e8;"));

    topBar->addWidget(backBtn);
    topBar->addWidget(titleLabel);
    topBar->addStretch(1);
    pageLayout->addLayout(topBar);

    // Scrollable Settings Form
    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *formContainer = new QWidget(scrollArea);
    formContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *formLayout = new QVBoxLayout(formContainer);
    formLayout->setContentsMargins(4, 4, 4, 4);
    formLayout->setSpacing(10);

    // 1. Execution Mode Group
    auto *modeGroup = new QGroupBox(i18n("Execution Mode"), formContainer);
    auto *modeLayout = new QVBoxLayout(modeGroup);
    m_modeGroup = new QButtonGroup(this);
    m_modeAskRadio = new QRadioButton(i18n("Ask Mode (Review Proposals before Applying)"), modeGroup);
    m_modeYoloRadio = new QRadioButton(i18n("YOLO Mode (Execute Timeline Edits Immediately)"), modeGroup);
    m_modeYoloRadio->setChecked(true);
    m_modeGroup->addButton(m_modeAskRadio);
    m_modeGroup->addButton(m_modeYoloRadio);
    modeLayout->addWidget(m_modeAskRadio);
    modeLayout->addWidget(m_modeYoloRadio);
    formLayout->addWidget(modeGroup);

    // 2. Video Editing Model Group (Live dynamic models from ai-proxy)
    auto *modelGroup = new QGroupBox(i18n("Specialized Video Editing Model"), formContainer);
    auto *modelLayout = new QVBoxLayout(modelGroup);

    auto *filterLayout = new QHBoxLayout();
    m_modelSearchInput = new QLineEdit(modelGroup);
    m_modelSearchInput->setPlaceholderText(i18n("Search models (e.g. claude, gpt, llama)..."));
    m_modelSearchInput->setClearButtonEnabled(true);
    connect(m_modelSearchInput, &QLineEdit::textChanged, this, &AIChatWidget::slotFilterModels);

    m_providerFilter = new QComboBox(modelGroup);
    m_providerFilter->addItem(i18n("All Providers"), QStringLiteral("all"));
    m_providerFilter->addItem(QStringLiteral("OpenRouter"), QStringLiteral("openrouter"));
    m_providerFilter->addItem(QStringLiteral("Groq"), QStringLiteral("groq"));
    connect(m_providerFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AIChatWidget::slotFilterModels);

    m_refreshModelsBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Refresh"), modelGroup);
    m_refreshModelsBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 2px; padding: 3px 8px; color: #cccccc; font-size: 11px; }"
        "QPushButton:hover { background-color: #2d2d2d; border-color: #007acc; color: #ffffff; }"
    ));
    connect(m_refreshModelsBtn, &QPushButton::clicked, this, &AIChatWidget::slotRefreshModels);

    filterLayout->addWidget(m_modelSearchInput, 1);
    filterLayout->addWidget(m_providerFilter);
    filterLayout->addWidget(m_refreshModelsBtn);
    modelLayout->addLayout(filterLayout);

    m_editingModelSelector = new QComboBox(modelGroup);
    m_editingModelSelector->addItem(QStringLiteral("Auto (Default AI Proxy Resolution)"), QStringLiteral("auto"));
    modelLayout->addWidget(m_editingModelSelector);

    m_modelsStatusLabel = new QLabel(i18n("Fetching live models from AI proxy..."), modelGroup);
    m_modelsStatusLabel->setStyleSheet(QStringLiteral("color: #858585; font-size: 10.5px;"));
    modelLayout->addWidget(m_modelsStatusLabel);

    formLayout->addWidget(modelGroup);

    // 3. Creative AI Multi-Modal Generation Models
    auto *creativeGroup = new QGroupBox(i18n("Creative Generation Models"), formContainer);
    auto *creativeLayout = new QFormLayout(creativeGroup);
    creativeLayout->setLabelAlignment(Qt::AlignLeft);

    m_imageModelSelector = new QComboBox(creativeGroup);
    m_imageModelSelector->addItem(QStringLiteral("Auto (AI Proxy Default)"), QStringLiteral("auto"));
    creativeLayout->addRow(i18n("Image Model:"), m_imageModelSelector);

    m_videoModelSelector = new QComboBox(creativeGroup);
    m_videoModelSelector->addItem(QStringLiteral("Auto (AI Proxy Default)"), QStringLiteral("auto"));
    creativeLayout->addRow(i18n("Video Model:"), m_videoModelSelector);

    m_musicModelSelector = new QComboBox(creativeGroup);
    m_musicModelSelector->addItem(QStringLiteral("Auto (AI Proxy Default)"), QStringLiteral("auto"));
    creativeLayout->addRow(i18n("Music Model:"), m_musicModelSelector);

    m_voiceModelSelector = new QComboBox(creativeGroup);
    m_voiceModelSelector->addItem(QStringLiteral("Auto (AI Proxy Default)"), QStringLiteral("auto"));
    creativeLayout->addRow(i18n("Voiceover Model:"), m_voiceModelSelector);

    m_soundModelSelector = new QComboBox(creativeGroup);
    m_soundModelSelector->addItem(QStringLiteral("Auto (Freesound + ElevenLabs SFX)"), QStringLiteral("auto"));
    creativeLayout->addRow(i18n("SFX Model:"), m_soundModelSelector);

    formLayout->addWidget(creativeGroup);

    // 4. Motion Graphics & VFX Tier (Natron)
    auto *tierGroup = new QGroupBox(i18n("Motion Graphics & Natron VFX Tier"), formContainer);
    auto *tierLayout = new QHBoxLayout(tierGroup);
    m_mgTierGroup = new QButtonGroup(this);
    m_mgSpeedRadio = new QRadioButton(i18n("Fast (Speed)"), tierGroup);
    m_mgBalanceRadio = new QRadioButton(i18n("Balanced"), tierGroup);
    m_mgQualityRadio = new QRadioButton(i18n("Polish (Quality)"), tierGroup);
    m_mgBalanceRadio->setChecked(true);
    m_mgTierGroup->addButton(m_mgSpeedRadio);
    m_mgTierGroup->addButton(m_mgBalanceRadio);
    m_mgTierGroup->addButton(m_mgQualityRadio);
    tierLayout->addWidget(m_mgSpeedRadio);
    tierLayout->addWidget(m_mgBalanceRadio);
    tierLayout->addWidget(m_mgQualityRadio);
    formLayout->addWidget(tierGroup);

    // 5. Cache Duration
    auto *cacheGroup = new QGroupBox(i18n("Prompt Cache Duration"), formContainer);
    auto *cacheLayout = new QHBoxLayout(cacheGroup);
    m_cacheGroup = new QButtonGroup(this);
    m_cacheShortRadio = new QRadioButton(i18n("Short Chat (Standard)"), cacheGroup);
    m_cacheLongRadio = new QRadioButton(i18n("Long Chat (1-Hour Cache)"), cacheGroup);
    m_cacheShortRadio->setChecked(true);
    m_cacheGroup->addButton(m_cacheShortRadio);
    m_cacheGroup->addButton(m_cacheLongRadio);
    cacheLayout->addWidget(m_cacheShortRadio);
    cacheLayout->addWidget(m_cacheLongRadio);
    formLayout->addWidget(cacheGroup);

    // 6. Autonomous Features
    auto *autoGroup = new QGroupBox(i18n("Autonomous Features"), formContainer);
    auto *autoLayout = new QVBoxLayout(autoGroup);
    m_cloudAssetsCheck = new QCheckBox(i18n("Autonomous Cloud Media Assets Retrieval (Pexels, Pixabay, Freesound)"), autoGroup);
    m_cloudAssetsCheck->setChecked(true);
    m_planModeCheck = new QCheckBox(i18n("Plan Mode (Generate numbered plan before mutating timeline)"), autoGroup);
    autoLayout->addWidget(m_cloudAssetsCheck);
    autoLayout->addWidget(m_planModeCheck);
    formLayout->addWidget(autoGroup);

    formLayout->addStretch(1);
    scrollArea->setWidget(formContainer);
    pageLayout->addWidget(scrollArea, 1);
}

void AIChatWidget::updateModeBadge()
{
    bool isYolo = m_modeYoloRadio ? m_modeYoloRadio->isChecked() : true;
    if (m_modeBadge) {
        if (isYolo) {
            m_modeBadge->setText(i18n("YOLO Mode"));
            m_modeBadge->setStyleSheet(QStringLiteral(
                "background-color: #252526; color: #4ec9b0; border: 1px solid #3c3c3c; "
                "border-radius: 2px; padding: 2px 6px; font-size: 10px; font-weight: 600; font-family: monospace;"
            ));
        } else {
            m_modeBadge->setText(i18n("Ask Mode"));
            m_modeBadge->setStyleSheet(QStringLiteral(
                "background-color: #252526; color: #ce9178; border: 1px solid #3c3c3c; "
                "border-radius: 2px; padding: 2px 6px; font-size: 10px; font-weight: 600; font-family: monospace;"
            ));
        }
    }
}

void AIChatWidget::updateMetricsDisplay(int totalTokens, qint64 latencyMs, const QString &modelId)
{
    QString modelDisplay = modelId.isEmpty() ? m_dispatcher->currentModel() : modelId;
    if (modelDisplay.contains(QLatin1Char('/'))) {
        modelDisplay = modelDisplay.section(QLatin1Char('/'), -1);
    }
    if (modelDisplay.isEmpty() || modelDisplay == QStringLiteral("auto")) {
        modelDisplay = QStringLiteral("auto");
    }

    QString tokensDisplay;
    if (totalTokens >= 1000) {
        tokensDisplay = QStringLiteral("%1k").arg(totalTokens / 1000.0, 0, 'f', 1);
    } else {
        tokensDisplay = QString::number(totalTokens);
    }

    QString latencyDisplay;
    if (latencyMs >= 1000) {
        latencyDisplay = QStringLiteral("%1s").arg(latencyMs / 1000.0, 0, 'f', 2);
    } else {
        latencyDisplay = QStringLiteral("%1ms").arg(latencyMs);
    }

    if (m_modelTag) {
        m_modelTag->setText(QStringLiteral("[Model: %1]").arg(modelDisplay));
        m_modelTag->setToolTip(i18n("Active AI Model: %1", modelId));
    }
    if (m_tokensTag) {
        m_tokensTag->setText(QStringLiteral("[Tokens: %1]").arg(tokensDisplay));
        m_tokensTag->setToolTip(i18n("Token Usage: %1 tokens", totalTokens));
    }
    if (m_latencyTag) {
        m_latencyTag->setText(QStringLiteral("[Latency: %1]").arg(latencyDisplay));
        m_latencyTag->setToolTip(i18n("Response Latency: %1ms", latencyMs));
    }
}

void AIChatWidget::slotMetricsUpdated(int totalTokens, qint64 latencyMs, const QString &modelId)
{
    updateMetricsDisplay(totalTokens, latencyMs, modelId);
}

void AIChatWidget::slotToggleSettings()
{
    if (m_stackedWidget->currentIndex() == 0) {
        m_stackedWidget->setCurrentIndex(1);
        if (m_dynamicModels.isEmpty()) {
            slotRefreshModels();
        }
    } else {
        slotSaveSettings();
        m_stackedWidget->setCurrentIndex(0);
    }
}

void AIChatWidget::slotRefreshModels()
{
    if (m_modelsStatusLabel) {
        m_modelsStatusLabel->setText(i18n("Fetching live models from AI proxy..."));
    }
    m_dispatcher->fetchAvailableModels();
}

void AIChatWidget::slotModelsLoaded(const QJsonArray &models)
{
    m_dynamicModels.clear();

    for (const auto &val : models) {
        QJsonObject obj = val.toObject();
        DynamicModelInfo info;
        info.id = obj[QStringLiteral("id")].toString();
        info.name = obj[QStringLiteral("name")].toString();
        info.provider = obj[QStringLiteral("provider")].toString().toLower();
        info.contextLength = obj[QStringLiteral("contextLength")].toInt(0);

        if (!info.id.isEmpty()) {
            m_dynamicModels.append(info);
        }
    }

    slotFilterModels();

    if (m_modelsStatusLabel) {
        m_modelsStatusLabel->setText(i18n("Loaded %1 live models from AI proxy.", m_dynamicModels.size()));
    }

    // Populate creative dropdowns with dynamic multi-modal models
    m_imageModelSelector->clear();
    m_imageModelSelector->addItem(QStringLiteral("Auto (AI Proxy Default)"), QStringLiteral("auto"));
    m_videoModelSelector->clear();
    m_videoModelSelector->addItem(QStringLiteral("Auto (AI Proxy Default)"), QStringLiteral("auto"));
    m_musicModelSelector->clear();
    m_musicModelSelector->addItem(QStringLiteral("Auto (AI Proxy Default)"), QStringLiteral("auto"));
    m_voiceModelSelector->clear();
    m_voiceModelSelector->addItem(QStringLiteral("Auto (AI Proxy Default)"), QStringLiteral("auto"));

    for (const auto &m : m_dynamicModels) {
        QString display = QStringLiteral("[%1] %2").arg(m.provider.toUpper(), m.name.isEmpty() ? m.id : m.name);
        m_imageModelSelector->addItem(display, m.id);
        m_videoModelSelector->addItem(display, m.id);
        m_musicModelSelector->addItem(display, m.id);
        m_voiceModelSelector->addItem(display, m.id);
    }
}

void AIChatWidget::slotFilterModels()
{
    QString query = m_modelSearchInput ? m_modelSearchInput->text().trimmed().toLower() : QString();
    QString provider = m_providerFilter ? m_providerFilter->currentData().toString() : QStringLiteral("all");

    QString currentSelectedId = m_editingModelSelector->currentData().toString();
    m_editingModelSelector->clear();
    m_editingModelSelector->addItem(QStringLiteral("Auto (Default AI Proxy Resolution)"), QStringLiteral("auto"));

    int matched = 0;
    for (const auto &m : m_dynamicModels) {
        if (provider != QStringLiteral("all") && m.provider != provider) {
            continue;
        }
        if (!query.isEmpty() && !m.id.toLower().contains(query) && !m.name.toLower().contains(query)) {
            continue;
        }

        QString ctxStr = m.contextLength > 0 ? QStringLiteral(" (%1k ctx)").arg(m.contextLength / 1000) : QString();
        QString display = QStringLiteral("[%1] %2%3").arg(m.provider.toUpper(), m.name.isEmpty() ? m.id : m.name, ctxStr);
        m_editingModelSelector->addItem(display, m.id);
        matched++;
    }

    // Restore previous selection if possible
    int idx = m_editingModelSelector->findData(currentSelectedId);
    if (idx >= 0) {
        m_editingModelSelector->setCurrentIndex(idx);
    }
}

void AIChatWidget::slotSaveSettings()
{
    AIAgentSettings s;
    s.mode = m_modeYoloRadio->isChecked() ? QStringLiteral("yolo") : QStringLiteral("ask");
    s.editingModelId = m_editingModelSelector->currentData().toString();
    if (s.editingModelId.isEmpty()) {
        s.editingModelId = QStringLiteral("auto");
    }
    s.provider = m_providerFilter->currentData().toString();
    s.imageModel = m_imageModelSelector->currentData().toString();
    s.videoModel = m_videoModelSelector->currentData().toString();
    s.musicModel = m_musicModelSelector->currentData().toString();
    s.voiceModel = m_voiceModelSelector->currentData().toString();
    s.soundModel = m_soundModelSelector->currentData().toString();

    if (m_mgSpeedRadio->isChecked()) s.mgTier = QStringLiteral("speed");
    else if (m_mgQualityRadio->isChecked()) s.mgTier = QStringLiteral("quality");
    else s.mgTier = QStringLiteral("balance");

    s.cacheMode = m_cacheLongRadio->isChecked() ? QStringLiteral("long") : QStringLiteral("short");
    s.cloudAssetsAccess = m_cloudAssetsCheck->isChecked();
    s.planMode = m_planModeCheck->isChecked();

    m_dispatcher->setAgentSettings(s);
    updateModeBadge();
    updateMetricsDisplay(0, 0, s.editingModelId);
}

QString AIChatWidget::formatMarkdownHtml(const QString &rawText)
{
    QString formatted = rawText.toHtmlEscaped();

    // Bold **text**
    formatted.replace(QRegularExpression(QStringLiteral("\\*\\*(.*?)\\*\\*")), QStringLiteral("<b>\\1</b>"));

    // Inline code `code`
    formatted.replace(QRegularExpression(QStringLiteral("`([^`]+)`")),
                      QStringLiteral("<code style='background: rgba(255,255,255,0.08); padding: 1px 4px; border-radius: 2px; font-family: monospace;'>\\1</code>"));

    // Code blocks ```...```
    formatted.replace(QRegularExpression(QStringLiteral("```(?:json|python|text)?\\n([\\s\\S]*?)```")),
                      QStringLiteral("<pre style='background: #181818; border: 1px solid #2d2d2d; border-radius: 2px; padding: 6px; font-family: monospace; font-size: 11px; color: #d4d4d4;'>\\1</pre>"));

    // Bullet lists
    formatted.replace(QRegularExpression(QStringLiteral("(?m)^\\s*-\\s+(.*)$")), QStringLiteral(" • \\1<br/>"));

    // Line breaks
    formatted.replace(QLatin1Char('\n'), QLatin1String("<br/>"));

    return formatted;
}

void AIChatWidget::appendUserMessage(const QString &text)
{
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));

    QString html = QStringLiteral(
        "<div style='margin: 10px 0; text-align: right;'>"
        "  <div style='display: inline-block; max-width: 85%; background: #252526; border: 1px solid #3c3c3c; color: #d4d4d4; border-radius: 2px; padding: 6px 10px; text-align: left; word-break: break-word; font-size: 12px; line-height: 1.45;'>"
        "    %1"
        "  </div>"
        "  <div style='font-size: 9.5px; color: #858585; margin-top: 2px;'>%2</div>"
        "</div>"
    ).arg(text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>")), timestamp);

    m_messageStream->append(html);
    m_messageStream->verticalScrollBar()->setValue(m_messageStream->verticalScrollBar()->maximum());
}

void AIChatWidget::appendAssistantResponse(const QString &text)
{
    if (text.trimmed().isEmpty()) return;

    QString htmlText = formatMarkdownHtml(text);

    QString html = QStringLiteral(
        "<div style='margin: 12px 0; color: #d4d4d4; font-size: 12px; line-height: 1.5; word-break: break-word;'>"
        "  %1"
        "</div>"
    ).arg(htmlText);

    m_messageStream->append(html);
    m_messageStream->verticalScrollBar()->setValue(m_messageStream->verticalScrollBar()->maximum());
}

void AIChatWidget::appendToolExecution(const QString &toolName, const QString &paramsSummary, bool success, const QString &errorMsg)
{
    QString statusColor = success ? QStringLiteral("#4ec9b0") : QStringLiteral("#f14c4c");
    QString statusTag = success ? QStringLiteral("[OK]") : QStringLiteral("[FAIL]");

    QString html = QStringLiteral(
        "<div style='margin: 4px 0; font-family: monospace; font-size: 11px; color: #858585; line-height: 1.4;'>"
        "  <span style='color: %1; font-weight: bold;'>%2</span> "
        "  <span style='color: #9cdcfe; font-weight: 600;'>%3</span>"
        "  <span style='color: #858585;'> %4</span>"
        "  %5"
        "</div>"
    ).arg(statusColor, statusTag, toolName,
          paramsSummary.isEmpty() ? QString() : QStringLiteral("· %1").arg(paramsSummary.toHtmlEscaped()),
          errorMsg.isEmpty() ? QString() : QStringLiteral("<div style='color: #f14c4c; margin-left: 18px;'>%1</div>").arg(errorMsg.toHtmlEscaped()));

    m_messageStream->append(html);
    m_messageStream->verticalScrollBar()->setValue(m_messageStream->verticalScrollBar()->maximum());
}

void AIChatWidget::appendSystemMessage(const QString &text)
{
    QString html = QStringLiteral(
        "<div style='margin: 6px 0; text-align: center; color: #858585; font-size: 10.5px; font-style: italic;'>"
        "  %1"
        "</div>"
    ).arg(text.toHtmlEscaped());

    m_messageStream->append(html);
    m_messageStream->verticalScrollBar()->setValue(m_messageStream->verticalScrollBar()->maximum());
}

void AIChatWidget::slotSendMessage()
{
    QString prompt = m_promptInput->text().trimmed();
    if (prompt.isEmpty()) return;

    appendUserMessage(prompt);
    m_promptInput->clear();

    QString engine = m_engineTargetSelector->currentData().toString();
    Q_EMIT sendPromptRequested(prompt, engine);
}

void AIChatWidget::slotQuickActionTriggered()
{
    auto *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString prompt = btn->property("promptText").toString();
    if (prompt.isEmpty()) return;

    m_promptInput->setText(prompt);
    slotSendMessage();
}

void AIChatWidget::slotClearChat()
{
    m_messageStream->clear();
    m_proposalCard->setVisible(false);
    appendSystemMessage(i18n("History cleared."));
}

void AIChatWidget::slotRequestStarted()
{
    m_liveStatusBar->setVisible(true);
    m_liveStatusText->setText(i18n("Analyzing edit intent..."));
    m_elapsedTimer.start();
    m_statusTimer->start(100);
}

void AIChatWidget::slotRequestFinished()
{
    m_statusTimer->stop();
    m_liveStatusBar->setVisible(false);
}

void AIChatWidget::slotUpdateLiveTimer()
{
    double sec = m_elapsedTimer.elapsed() / 1000.0;
    m_liveStatusTimer->setText(QStringLiteral("%1s").arg(sec, 0, 'f', 1));
}

void AIChatWidget::slotResponseReceived(const QString &summaryText, const QJsonObject &actionPayload)
{
    appendAssistantResponse(summaryText);

    if (actionPayload.isEmpty()) return;

    AIAgentSettings currentSettings = m_dispatcher->agentSettings();

    if (currentSettings.mode == QStringLiteral("ask")) {
        // Ask mode: present proposal card for confirmation
        m_pendingProposalAction = actionPayload;
        QString actionName = actionPayload[QStringLiteral("action")].toString();
        m_proposalTitle->setText(i18n("Proposed Action: %1", actionName));
        m_proposalSummary->setText(i18n("The Agent proposes executing '%1' on your project timeline.", actionName));
        m_proposalCard->setVisible(true);
    } else {
        // YOLO mode: execute immediately
        QString actionName = actionPayload[QStringLiteral("action")].toString();
        appendToolExecution(actionName, QStringLiteral("Executing..."), true);
        m_router->executeAction(actionPayload);
    }
}

void AIChatWidget::slotApplyProposal()
{
    if (!m_pendingProposalAction.isEmpty()) {
        QString actionName = m_pendingProposalAction[QStringLiteral("action")].toString();
        appendToolExecution(actionName, QStringLiteral("Applied by user"), true);
        m_router->executeAction(m_pendingProposalAction);
        m_pendingProposalAction = QJsonObject();
    }
    m_proposalCard->setVisible(false);
}

void AIChatWidget::slotRejectProposal()
{
    appendSystemMessage(i18n("Proposal rejected by user."));
    m_pendingProposalAction = QJsonObject();
    m_proposalCard->setVisible(false);
}

void AIChatWidget::slotExecutionFinished(const QString &resultMessage, bool success)
{
    appendToolExecution(i18n("Execution Result"), resultMessage, success);
}

void AIChatWidget::slotToolDataOutput(const QString &toolName, const QJsonObject &data)
{
    if (toolName == QStringLiteral("detect_scenes")) {
        int sceneCount = data[QStringLiteral("scene_count")].toInt(0);
        appendToolExecution(toolName, i18n("%1 scene cuts identified", sceneCount), true);
    }
}
