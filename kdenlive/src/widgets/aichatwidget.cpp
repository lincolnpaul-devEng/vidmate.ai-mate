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
    connect(m_dispatcher, &AIDispatcher::errorOccurred, this, [&](const QString &err) {
        appendSystemMessage(i18n("Network error: %1", err));
        slotRequestFinished();
    });
    connect(m_dispatcher, &AIDispatcher::requestStarted, this, &AIChatWidget::slotRequestStarted);
    connect(m_dispatcher, &AIDispatcher::requestFinished, this, &AIChatWidget::slotRequestFinished);

    connect(m_router, &AICommandRouter::executionFinished, this, &AIChatWidget::slotExecutionFinished);
    connect(m_router, &AICommandRouter::dataOutput, this, &AIChatWidget::slotToolDataOutput);

    connect(m_statusTimer, &QTimer::timeout, this, &AIChatWidget::slotUpdateLiveTimer);
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
    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // ── Header Bar ──────────────────────────────────────────────────────────
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(6);

    auto *brandLabel = new QLabel(QStringLiteral("<b>VidMate Agent</b>"), page);
    brandLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));

    m_modeBadge = new QLabel(page);
    m_modeBadge->setStyleSheet(QStringLiteral(
        "background-color: #2b3a4a; color: #5dade2; border: 1px solid #3498db; "
        "border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600;"
    ));

    m_engineTargetSelector = new QComboBox(page);
    m_engineTargetSelector->addItem(i18n("Auto (Kdenlive & Natron)"), QStringLiteral("auto"));
    m_engineTargetSelector->addItem(i18n("Kdenlive (NLE / Cuts)"), QStringLiteral("kdenlive"));
    m_engineTargetSelector->addItem(i18n("Natron (VFX / Compositing)"), QStringLiteral("natron"));
    m_engineTargetSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_assetStudioBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-media-playlist")), i18n("Assets"), page);
    m_assetStudioBtn->setToolTip(i18n("Open Stock Media & Voiceover Studio"));
    connect(m_assetStudioBtn, &QPushButton::clicked, this, &AIChatWidget::openAssetStudioRequested);

    m_settingsBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), i18n("Settings"), page);
    m_settingsBtn->setToolTip(i18n("Open Agent Settings"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &AIChatWidget::slotToggleSettings);

    m_clearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), QString(), page);
    m_clearBtn->setToolTip(i18n("Clear History"));
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
        "QFrame#liveStatusBar { background-color: #1e2530; border: 1px solid #3a4a5e; border-radius: 4px; padding: 4px; }"
    ));
    auto *liveLayout = new QHBoxLayout(m_liveStatusBar);
    liveLayout->setContentsMargins(6, 4, 6, 4);
    liveLayout->setSpacing(8);

    m_liveStatusDot = new QLabel(QStringLiteral("●"), m_liveStatusBar);
    m_liveStatusDot->setStyleSheet(QStringLiteral("color: #3498db; font-size: 13px;"));

    m_liveStatusText = new QLabel(i18n("Processing timeline task..."), m_liveStatusBar);
    m_liveStatusText->setStyleSheet(QStringLiteral("color: #ecf0f1; font-size: 11px;"));

    m_liveStatusTimer = new QLabel(QStringLiteral("0.0s"), m_liveStatusBar);
    m_liveStatusTimer->setStyleSheet(QStringLiteral("color: #bdc3c7; font-family: monospace; font-size: 11px;"));

    liveLayout->addWidget(m_liveStatusDot);
    liveLayout->addWidget(m_liveStatusText, 1);
    liveLayout->addWidget(m_liveStatusTimer);
    m_liveStatusBar->setVisible(false);
    mainLayout->addWidget(m_liveStatusBar);

    // ── Message Stream (No speech bubble container) ─────────────────────────
    m_messageStream = new QTextBrowser(page);
    m_messageStream->setOpenExternalLinks(true);
    m_messageStream->setReadOnly(true);
    m_messageStream->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  background-color: palette(base);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "  padding: 8px;"
        "  font-size: 12px;"
        "  line-height: 1.5;"
        "}"
    ));
    mainLayout->addWidget(m_messageStream, 1);

    // ── Proposal Review Card (Shown in Ask Mode) ────────────────────────────
    m_proposalCard = new QFrame(page);
    m_proposalCard->setObjectName(QStringLiteral("proposalCard"));
    m_proposalCard->setStyleSheet(QStringLiteral(
        "QFrame#proposalCard {"
        "  background-color: #21262d;"
        "  border: 1px solid #30363d;"
        "  border-left: 4px solid #3498db;"
        "  border-radius: 6px;"
        "  padding: 8px;"
        "}"
    ));
    auto *proposalLayout = new QVBoxLayout(m_proposalCard);
    proposalLayout->setContentsMargins(8, 6, 8, 6);
    proposalLayout->setSpacing(4);

    m_proposalTitle = new QLabel(i18n("Proposed Timeline Modification"), m_proposalCard);
    m_proposalTitle->setStyleSheet(QStringLiteral("font-weight: bold; color: #5dade2; font-size: 11px;"));

    m_proposalSummary = new QLabel(m_proposalCard);
    m_proposalSummary->setWordWrap(true);
    m_proposalSummary->setStyleSheet(QStringLiteral("color: #c9d1d9; font-size: 11px;"));

    auto *proposalBtnLayout = new QHBoxLayout();
    proposalBtnLayout->setSpacing(6);
    m_applyProposalBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok")), i18n("Apply Changes"), m_proposalCard);
    m_applyProposalBtn->setStyleSheet(QStringLiteral("background-color: #238636; color: white; font-weight: bold; border-radius: 4px; padding: 4px 10px;"));
    connect(m_applyProposalBtn, &QPushButton::clicked, this, &AIChatWidget::slotApplyProposal);

    m_rejectProposalBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-cancel")), i18n("Reject"), m_proposalCard);
    m_rejectProposalBtn->setStyleSheet(QStringLiteral("background-color: #30363d; color: #c9d1d9; border-radius: 4px; padding: 4px 10px;"));
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
    quickScroll->setFixedHeight(36);
    quickScroll->setWidgetResizable(true);
    quickScroll->setFrameShape(QFrame::NoFrame);
    quickScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quickScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *quickContainer = new QWidget(quickScroll);
    auto *quickLayout = new QHBoxLayout(quickContainer);
    quickLayout->setContentsMargins(0, 0, 0, 0);
    quickLayout->setSpacing(6);

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
            "QPushButton { background-color: palette(alternate-base); border: 1px solid palette(mid); border-radius: 12px; padding: 2px 8px; font-size: 11px; color: palette(text); }"
            "QPushButton:hover { background-color: palette(highlight); color: palette(highlighted-text); }"
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
    connect(m_promptInput, &QLineEdit::returnPressed, this, &AIChatWidget::slotSendMessage);

    m_sendBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-send")), i18n("Send"), page);
    m_sendBtn->setStyleSheet(QStringLiteral("font-weight: 600; padding: 4px 12px;"));
    connect(m_sendBtn, &QPushButton::clicked, this, &AIChatWidget::slotSendMessage);

    inputLayout->addWidget(m_promptInput, 1);
    inputLayout->addWidget(m_sendBtn);
    mainLayout->addLayout(inputLayout);

    // Initial greeting
    appendSystemMessage(i18n("VidMate AI Agent ready. Direct edits, camera cuts, or Natron VFX pipelines."));
}

void AIChatWidget::setupSettingsPage(QWidget *page)
{
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(8, 8, 8, 8);
    pageLayout->setSpacing(8);

    // Header with Back Button
    auto *topBar = new QHBoxLayout();
    auto *backBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")), i18n("Back to Workspace"), page);
    connect(backBtn, &QPushButton::clicked, this, &AIChatWidget::slotToggleSettings);

    auto *titleLabel = new QLabel(QStringLiteral("<b>Agent Configuration</b>"), page);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));

    topBar->addWidget(backBtn);
    topBar->addWidget(titleLabel);
    topBar->addStretch(1);
    pageLayout->addLayout(topBar);

    // Scrollable Settings Form
    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *formContainer = new QWidget(scrollArea);
    auto *formLayout = new QVBoxLayout(formContainer);
    formLayout->setContentsMargins(4, 4, 4, 4);
    formLayout->setSpacing(12);

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

    // 2. Video Editing Model Group
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

    filterLayout->addWidget(m_modelSearchInput, 1);
    filterLayout->addWidget(m_providerFilter);
    modelLayout->addLayout(filterLayout);

    m_editingModelSelector = new QComboBox(modelGroup);
    m_allAvailableModels = {
        QStringLiteral("Auto (Default AI Proxy Resolution)"),
        QStringLiteral("anthropic/claude-3.7-sonnet"),
        QStringLiteral("openai/gpt-4o"),
        QStringLiteral("deepseek/deepseek-r1"),
        QStringLiteral("meta-llama/llama-3.3-70b-instruct"),
        QStringLiteral("mistralai/mistral-large"),
        QStringLiteral("groq/llama-3.3-70b-versatile")
    };
    m_editingModelSelector->addItems(m_allAvailableModels);
    modelLayout->addWidget(m_editingModelSelector);
    formLayout->addWidget(modelGroup);

    // 3. Creative AI Multi-Modal Generation Models
    auto *creativeGroup = new QGroupBox(i18n("Creative Generation Models"), formContainer);
    auto *creativeLayout = new QFormLayout(creativeGroup);
    creativeLayout->setLabelAlignment(Qt::AlignLeft);

    m_imageModelSelector = new QComboBox(creativeGroup);
    m_imageModelSelector->addItems({QStringLiteral("Auto (AI Proxy)"), QStringLiteral("black-forest-labs/flux-1-schnell"), QStringLiteral("stabilityai/stable-diffusion-xl-base-1.0")});
    creativeLayout->addRow(i18n("Image Model:"), m_imageModelSelector);

    m_videoModelSelector = new QComboBox(creativeGroup);
    m_videoModelSelector->addItems({QStringLiteral("Auto (AI Proxy)"), QStringLiteral("kling/v1.5-pro"), QStringLiteral("runway/gen3-alpha")});
    creativeLayout->addRow(i18n("Video Model:"), m_videoModelSelector);

    m_musicModelSelector = new QComboBox(creativeGroup);
    m_musicModelSelector->addItems({QStringLiteral("Auto (AI Proxy)"), QStringLiteral("suno/chirp-v3.5"), QStringLiteral("facebook/musicgen-large")});
    creativeLayout->addRow(i18n("Music Model:"), m_musicModelSelector);

    m_voiceModelSelector = new QComboBox(creativeGroup);
    m_voiceModelSelector->addItems({QStringLiteral("ElevenLabs (Rachel)"), QStringLiteral("ElevenLabs (Adam)"), QStringLiteral("ElevenLabs (Antoni)"), QStringLiteral("ElevenLabs (Bella)"), QStringLiteral("OpenAI TTS (alloy)")});
    creativeLayout->addRow(i18n("Voiceover Model:"), m_voiceModelSelector);

    m_soundModelSelector = new QComboBox(creativeGroup);
    m_soundModelSelector->addItems({QStringLiteral("Auto (Freesound + ElevenLabs SFX)"), QStringLiteral("freesound/neural-search"), QStringLiteral("elevenlabs/sound-effects")});
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
                "background-color: #2b3a4a; color: #5dade2; border: 1px solid #3498db; "
                "border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600;"
            ));
        } else {
            m_modeBadge->setText(i18n("Ask Mode"));
            m_modeBadge->setStyleSheet(QStringLiteral(
                "background-color: #3e332a; color: #f39c12; border: 1px solid #e67e22; "
                "border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600;"
            ));
        }
    }
}

void AIChatWidget::slotToggleSettings()
{
    if (m_stackedWidget->currentIndex() == 0) {
        m_stackedWidget->setCurrentIndex(1);
    } else {
        slotSaveSettings();
        m_stackedWidget->setCurrentIndex(0);
    }
}

void AIChatWidget::slotSaveSettings()
{
    AIAgentSettings s;
    s.mode = m_modeYoloRadio->isChecked() ? QStringLiteral("yolo") : QStringLiteral("ask");
    s.editingModelId = m_editingModelSelector->currentText();
    if (s.editingModelId.startsWith(QStringLiteral("Auto"))) {
        s.editingModelId = QStringLiteral("auto");
    }
    s.provider = m_providerFilter->currentData().toString();
    s.imageModel = m_imageModelSelector->currentText();
    s.videoModel = m_videoModelSelector->currentText();
    s.musicModel = m_musicModelSelector->currentText();
    s.voiceModel = m_voiceModelSelector->currentText();
    s.soundModel = m_soundModelSelector->currentText();

    if (m_mgSpeedRadio->isChecked()) s.mgTier = QStringLiteral("speed");
    else if (m_mgQualityRadio->isChecked()) s.mgTier = QStringLiteral("quality");
    else s.mgTier = QStringLiteral("balance");

    s.cacheMode = m_cacheLongRadio->isChecked() ? QStringLiteral("long") : QStringLiteral("short");
    s.cloudAssetsAccess = m_cloudAssetsCheck->isChecked();
    s.planMode = m_planModeCheck->isChecked();

    m_dispatcher->setAgentSettings(s);
    updateModeBadge();
}

void AIChatWidget::slotFilterModels(const QString &query)
{
    m_editingModelSelector->clear();
    for (const QString &m : m_allAvailableModels) {
        if (query.isEmpty() || m.contains(query, Qt::CaseInsensitive)) {
            m_editingModelSelector->addItem(m);
        }
    }
}

QString AIChatWidget::formatMarkdownHtml(const QString &rawText)
{
    QString formatted = rawText.toHtmlEscaped();

    // Bold **text**
    formatted.replace(QRegularExpression(QStringLiteral("\\*\\*(.*?)\\*\\*")), QStringLiteral("<b>\\1</b>"));

    // Inline code `code`
    formatted.replace(QRegularExpression(QStringLiteral("`([^`]+)`")),
                      QStringLiteral("<code style='background: rgba(255,255,255,0.08); padding: 1px 4px; border-radius: 3px; font-family: monospace;'>\\1</code>"));

    // Code blocks ```...```
    formatted.replace(QRegularExpression(QStringLiteral("```(?:json|python|text)?\\n([\\s\\S]*?)```")),
                      QStringLiteral("<pre style='background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); border-radius: 4px; padding: 6px; font-family: monospace; font-size: 11px;'>\\1</pre>"));

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
        "<div style='margin: 12px 0; text-align: right;'>"
        "  <div style='display: inline-block; max-width: 85%; background: #262c38; border: 1px solid #3b4556; color: #f0f6fc; border-radius: 8px; padding: 8px 12px; text-align: left; word-break: break-word; font-size: 12px; line-height: 1.45;'>"
        "    %1"
        "  </div>"
        "  <div style='font-size: 10px; color: #8b949e; margin-top: 2px;'>%2</div>"
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
        "<div style='margin: 14px 0; color: #e6edf3; font-size: 12px; line-height: 1.55; word-break: break-word;'>"
        "  %1"
        "</div>"
    ).arg(htmlText);

    m_messageStream->append(html);
    m_messageStream->verticalScrollBar()->setValue(m_messageStream->verticalScrollBar()->maximum());
}

void AIChatWidget::appendToolExecution(const QString &toolName, const QString &paramsSummary, bool success, const QString &errorMsg)
{
    QString statusColor = success ? QStringLiteral("#27ae60") : QStringLiteral("#e74c3c");
    QString statusTag = success ? QStringLiteral("[OK]") : QStringLiteral("[FAIL]");

    QString html = QStringLiteral(
        "<div style='margin: 6px 0; font-family: monospace; font-size: 11px; color: #8b949e; line-height: 1.4;'>"
        "  <span style='color: %1; font-weight: bold;'>%2</span> "
        "  <span style='color: #c9d1d9; font-weight: 600;'>%3</span>"
        "  <span style='color: #8b949e;'> %4</span>"
        "  %5"
        "</div>"
    ).arg(statusColor, statusTag, toolName,
          paramsSummary.isEmpty() ? QString() : QStringLiteral("· %1").arg(paramsSummary.toHtmlEscaped()),
          errorMsg.isEmpty() ? QString() : QStringLiteral("<div style='color: #e74c3c; margin-left: 18px;'>%1</div>").arg(errorMsg.toHtmlEscaped()));

    m_messageStream->append(html);
    m_messageStream->verticalScrollBar()->setValue(m_messageStream->verticalScrollBar()->maximum());
}

void AIChatWidget::appendSystemMessage(const QString &text)
{
    QString html = QStringLiteral(
        "<div style='margin: 8px 0; text-align: center; color: #8b949e; font-size: 11px; font-style: italic;'>"
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
