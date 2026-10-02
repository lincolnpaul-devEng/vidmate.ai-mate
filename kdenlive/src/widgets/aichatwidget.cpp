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
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QRegularExpression>

#include "aidispatcher.h"
#include "aicommandrouter.h"

#include <cmath>

// ─── Thinking Phrases (Velo thinkingPhrases.ts -- English film production idioms) ────
static const char *s_thinkingPhrases[] = {
    "Pulling focus",
    "Setting up the dolly track",
    "Checking the gate",
    "Slate is up",
    "Syncing live audio",
    "Blocking the scene",
    "Calling action",
    "Hitting the mark",
    "Rigging the lights",
    "Finding the feeling",
    "Starting fresh take",
    "Scouting the location",
    "Panning across",
    "Pushing in",
    "Tracking the subject",
    "Whip pan",
    "Going handheld",
    "Raising the jib",
    "Zooming out",
    "Framing wide shot",
    "Getting the close-up",
    "Jump cutting",
    "Hard cutting",
    "Cross-dissolving",
    "Match cutting",
    "Cross-cutting",
    "Building montage",
    "L-cutting audio",
    "Splicing the reel"
};
static constexpr int THINKING_PHRASE_COUNT = sizeof(s_thinkingPhrases) / sizeof(s_thinkingPhrases[0]);

const char *AIChatWidget::thinkingPhrase(int seed)
{
    return s_thinkingPhrases[std::abs(seed) % THINKING_PHRASE_COUNT];
}

// ─── Tool Argument Summarizer (Velo toolArgSummary pattern) ─────────────────
// Extracts the most discriminating parameter value from tool args JSON
// to prevent repetitive tool rows from looking identical.
QString AIChatWidget::extractToolArgSummary(const QString &paramsJson)
{
    if (paramsJson.isEmpty()) return QString();

    QJsonDocument doc = QJsonDocument::fromJson(paramsJson.toUtf8());
    if (!doc.isObject()) return paramsJson.left(32);

    QJsonObject obj = doc.object();

    // Prioritized key list (most specific discriminators first)
    static const char *summaryKeys[] = {
        "query", "itemId", "clipId", "templateName", "audioName", "name",
        "from", "to", "templateId", "category", "ratio", "action",
        "format", "target", "track", "renderId", "file", "path",
        "effect", "transition", "filter", "text", "position"
    };

    for (const char *key : summaryKeys) {
        if (obj.contains(QLatin1String(key))) {
            QString val = obj[QLatin1String(key)].toVariant().toString();
            if (val.isEmpty()) continue;

            // Truncate UUIDs to 8 characters
            static QRegularExpression uuidRe(QStringLiteral(
                "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"),
                QRegularExpression::CaseInsensitiveOption);
            if (uuidRe.match(val).hasMatch()) {
                val = val.left(8);
            }

            // Cap string length
            if (val.length() > 28) {
                val = val.left(28) + QStringLiteral("...");
            }

            return val;
        }
    }

    // Fallback: show first key=value
    QStringList keys = obj.keys();
    if (!keys.isEmpty()) {
        QString val = obj[keys.first()].toVariant().toString();
        if (val.length() > 28) val = val.left(28) + QStringLiteral("...");
        return QStringLiteral("%1=%2").arg(keys.first(), val);
    }

    return QString();
}

// ─── Constructor ────────────────────────────────────────────────────────────
AIChatWidget::AIChatWidget(QWidget *parent)
    : QWidget(parent)
    , m_statusTimer(new QTimer(this))
    , m_dispatcher(new AIDispatcher(this))
    , m_router(new AICommandRouter(this))
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

    updateMetricsDisplay(0, 0, m_dispatcher->currentModel());
    m_dispatcher->fetchAvailableModels();
}

// ─── Root Layout ────────────────────────────────────────────────────────────
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

// ─── Workspace Page (Main Chat View) ────────────────────────────────────────
void AIChatWidget::setupWorkspacePage(QWidget *page)
{
    page->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1e1e1e; color: #cccccc; }"
    ));

    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Header Bar (Flat, 0.5px hairline bottom border) ─────────────────────
    auto *headerWidget = new QWidget(page);
    headerWidget->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1e1e1e; border-bottom: 1px solid #2d2d2d; }"
    ));
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(10, 6, 10, 6);
    headerLayout->setSpacing(6);

    auto *brandLabel = new QLabel(QStringLiteral("<b>VidMate Agent</b>"), headerWidget);
    brandLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #e1e4e8; border: none;"));

    m_modeBadge = new QLabel(headerWidget);
    m_modeBadge->setStyleSheet(QStringLiteral(
        "background-color: #252526; color: #4ec9b0; border: 1px solid #3c3c3c; "
        "border-radius: 2px; padding: 2px 6px; font-size: 10px; font-weight: 600; font-family: monospace;"
    ));

    m_engineTargetSelector = new QComboBox(headerWidget);
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
        "QPushButton { background-color: transparent; border: none; padding: 3px 8px; color: #858585; font-size: 11px; }"
        "QPushButton:hover { background-color: #2d2d2d; color: #ffffff; }"
        "QPushButton:pressed { background-color: #0e639c; }"
    );

    m_inspectorBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-list-details")), QString(), headerWidget);
    m_inspectorBtn->setToolTip(i18n("Diagnostic Inspector"));
    m_inspectorBtn->setStyleSheet(headerBtnStyle);
    connect(m_inspectorBtn, &QPushButton::clicked, this, &AIChatWidget::slotShowInspector);

    m_assetStudioBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-media-playlist")), QString(), headerWidget);
    m_assetStudioBtn->setToolTip(i18n("Open Stock Media & Voiceover Studio"));
    m_assetStudioBtn->setStyleSheet(headerBtnStyle);
    connect(m_assetStudioBtn, &QPushButton::clicked, this, &AIChatWidget::openAssetStudioRequested);

    m_settingsBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), QString(), headerWidget);
    m_settingsBtn->setToolTip(i18n("Agent Settings"));
    m_settingsBtn->setStyleSheet(headerBtnStyle);
    connect(m_settingsBtn, &QPushButton::clicked, this, &AIChatWidget::slotToggleSettings);

    m_clearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), QString(), headerWidget);
    m_clearBtn->setToolTip(i18n("Clear History"));
    m_clearBtn->setStyleSheet(headerBtnStyle);
    connect(m_clearBtn, &QPushButton::clicked, this, &AIChatWidget::slotClearChat);

    headerLayout->addWidget(brandLabel);
    headerLayout->addWidget(m_modeBadge);
    headerLayout->addWidget(m_engineTargetSelector, 1);
    headerLayout->addWidget(m_inspectorBtn);
    headerLayout->addWidget(m_assetStudioBtn);
    headerLayout->addWidget(m_settingsBtn);
    headerLayout->addWidget(m_clearBtn);
    mainLayout->addWidget(headerWidget);

    // ── Live Run Status Bar (Pulsing dot + thinking phrase + elapsed timer) ─
    m_liveStatusBar = new QFrame(page);
    m_liveStatusBar->setObjectName(QStringLiteral("liveStatusBar"));
    m_liveStatusBar->setStyleSheet(QStringLiteral(
        "QFrame#liveStatusBar { background-color: #1e1e1e; border-bottom: 1px solid #2d2d2d; padding: 0; }"
    ));
    auto *liveLayout = new QHBoxLayout(m_liveStatusBar);
    liveLayout->setContentsMargins(10, 5, 10, 5);
    liveLayout->setSpacing(8);

    m_liveStatusDot = new QLabel(m_liveStatusBar);
    m_liveStatusDot->setFixedSize(8, 8);
    m_liveStatusDot->setStyleSheet(QStringLiteral(
        "background-color: #007acc; border-radius: 4px; border: none;"
    ));

    m_liveStatusText = new QLabel(i18n("Analyzing..."), m_liveStatusBar);
    m_liveStatusText->setStyleSheet(QStringLiteral("color: #858585; font-size: 11.5px; border: none;"));

    m_liveStatusTimer = new QLabel(QStringLiteral("0.0s"), m_liveStatusBar);
    m_liveStatusTimer->setStyleSheet(QStringLiteral(
        "color: #858585; font-family: 'JetBrains Mono', 'Fira Code', monospace; font-size: 11px; border: none;"
    ));

    liveLayout->addWidget(m_liveStatusDot);
    liveLayout->addWidget(m_liveStatusText, 1);
    liveLayout->addWidget(m_liveStatusTimer);
    m_liveStatusBar->setVisible(false);
    mainLayout->addWidget(m_liveStatusBar);

    // ── Message Stream (Flat, borderless, dark) ─────────────────────────────
    m_messageStream = new QTextBrowser(page);
    m_messageStream->setOpenExternalLinks(true);
    m_messageStream->setReadOnly(true);
    m_messageStream->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  background-color: #1e1e1e;"
        "  border: none;"
        "  padding: 4px 12px;"
        "  font-size: 13px;"
        "  line-height: 1.55;"
        "  color: #d4d4d4;"
        "  selection-background-color: #264f78;"
        "}"
    ));
    mainLayout->addWidget(m_messageStream, 1);

    // ── Proposal Review Card ────────────────────────────────────────────────
    m_proposalCard = new QFrame(page);
    m_proposalCard->setObjectName(QStringLiteral("proposalCard"));
    m_proposalCard->setStyleSheet(QStringLiteral(
        "QFrame#proposalCard {"
        "  background-color: #252526;"
        "  border: 1px solid #3c3c3c;"
        "  border-left: 3px solid #007acc;"
        "  border-radius: 2px;"
        "  margin: 4px 10px;"
        "}"
    ));
    auto *proposalLayout = new QVBoxLayout(m_proposalCard);
    proposalLayout->setContentsMargins(10, 8, 10, 8);
    proposalLayout->setSpacing(4);

    m_proposalTitle = new QLabel(i18n("Proposed Timeline Modification"), m_proposalCard);
    m_proposalTitle->setStyleSheet(QStringLiteral("font-weight: bold; color: #4ec9b0; font-size: 11px;"));

    m_proposalSummary = new QLabel(m_proposalCard);
    m_proposalSummary->setWordWrap(true);
    m_proposalSummary->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 11px;"));

    auto *proposalBtnLayout = new QHBoxLayout();
    proposalBtnLayout->setSpacing(6);
    m_applyProposalBtn = new QPushButton(i18n("Apply Changes"), m_proposalCard);
    m_applyProposalBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #0e639c; color: white; font-weight: 600; border: none; border-radius: 2px; padding: 5px 14px; font-size: 11px; }"
        "QPushButton:hover { background-color: #1177bb; }"
    ));
    connect(m_applyProposalBtn, &QPushButton::clicked, this, &AIChatWidget::slotApplyProposal);

    m_rejectProposalBtn = new QPushButton(i18n("Reject"), m_proposalCard);
    m_rejectProposalBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: transparent; color: #858585; border: 1px solid #3c3c3c; border-radius: 2px; padding: 5px 14px; font-size: 11px; }"
        "QPushButton:hover { color: #cccccc; border-color: #858585; }"
    ));
    connect(m_rejectProposalBtn, &QPushButton::clicked, this, &AIChatWidget::slotRejectProposal);

    proposalBtnLayout->addStretch(1);
    proposalBtnLayout->addWidget(m_applyProposalBtn);
    proposalBtnLayout->addWidget(m_rejectProposalBtn);

    proposalLayout->addWidget(m_proposalTitle);
    proposalLayout->addWidget(m_proposalSummary);
    proposalLayout->addLayout(proposalBtnLayout);
    m_proposalCard->setVisible(false);
    mainLayout->addWidget(m_proposalCard);

    // ── Quick Workflow Starters (Horizontal scrolling pill chips) ────────────
    auto *quickScroll = new QScrollArea(page);
    quickScroll->setFixedHeight(34);
    quickScroll->setWidgetResizable(true);
    quickScroll->setFrameShape(QFrame::NoFrame);
    quickScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quickScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quickScroll->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    auto *quickContainer = new QWidget(quickScroll);
    quickContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *quickLayout = new QHBoxLayout(quickContainer);
    quickLayout->setContentsMargins(10, 0, 10, 0);
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
            "QPushButton {"
            "  background-color: #1e1e1e; border: 1px solid #333333; border-radius: 10px;"
            "  padding: 3px 10px; font-size: 11px; color: #858585;"
            "}"
            "QPushButton:hover { border-color: #007acc; color: #d4d4d4; }"
        ));
        connect(btn, &QPushButton::clicked, this, &AIChatWidget::slotQuickActionTriggered);
        quickLayout->addWidget(btn);
    }
    quickLayout->addStretch(1);
    quickScroll->setWidget(quickContainer);
    mainLayout->addWidget(quickScroll);

    // ── Composer Input Section (Flat, hairline top border) ───────────────────
    auto *composerWidget = new QWidget(page);
    composerWidget->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1e1e1e; border-top: 1px solid #2d2d2d; }"
    ));
    auto *inputLayout = new QHBoxLayout(composerWidget);
    inputLayout->setContentsMargins(10, 8, 10, 6);
    inputLayout->setSpacing(6);

    m_promptInput = new QLineEdit(composerWidget);
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
        "QLineEdit:focus { border: 1px solid #007acc; }"
    ));
    connect(m_promptInput, &QLineEdit::returnPressed, this, &AIChatWidget::slotSendMessage);

    m_sendBtn = new QPushButton(composerWidget);
    m_sendBtn->setFixedSize(30, 30);
    m_sendBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-send")));
    m_sendBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #0e639c; border: none; border-radius: 15px; padding: 0;"
        "}"
        "QPushButton:hover { background-color: #1177bb; }"
        "QPushButton:pressed { background-color: #094771; }"
    ));
    connect(m_sendBtn, &QPushButton::clicked, this, &AIChatWidget::slotSendMessage);

    inputLayout->addWidget(m_promptInput, 1);
    inputLayout->addWidget(m_sendBtn);
    mainLayout->addWidget(composerWidget);

    // ── Bottom Metrics Footer (Model, Tokens, Latency chips) ────────────────
    m_metricsFooter = new QWidget(page);
    m_metricsFooter->setStyleSheet(QStringLiteral("background: #1e1e1e; border: none;"));
    auto *metricsLayout = new QHBoxLayout(m_metricsFooter);
    metricsLayout->setContentsMargins(10, 2, 10, 4);
    metricsLayout->setSpacing(6);
    metricsLayout->addStretch(1);

    const QString tagStyle = QStringLiteral(
        "QLabel {"
        "  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
        "  font-size: 10px;"
        "  color: #6a737d;"
        "  background-color: transparent;"
        "  border: none;"
        "  padding: 0 2px;"
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

// ─── Settings Page (Unchanged from existing, preserved fully) ───────────────
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

    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *formContainer = new QWidget(scrollArea);
    formContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *formLayout = new QVBoxLayout(formContainer);
    formLayout->setContentsMargins(4, 4, 4, 4);
    formLayout->setSpacing(10);

    // 1. Execution Mode
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

    // 2. Video Editing Model (Live dynamic models)
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

    // 4. Motion Graphics Tier
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

// ─── Enhanced Markdown to HTML Formatter ────────────────────────────────────
// Supports: H1-H4, bold, italic, strikethrough, inline code, fenced code blocks,
// bullet lists, numbered lists, horizontal rules, and basic pipe tables.
QString AIChatWidget::formatMarkdownHtml(const QString &rawText)
{
    QString text = rawText;

    // Phase 1: Extract and protect fenced code blocks
    QStringList codeBlocks;
    static QRegularExpression codeBlockRe(QStringLiteral("```(?:[a-z]*)\\n([\\s\\S]*?)```"));
    QRegularExpressionMatchIterator it = codeBlockRe.globalMatch(text);
    int blockIdx = 0;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString code = match.captured(1).toHtmlEscaped();
        codeBlocks.append(QStringLiteral(
            "<pre style='background: #181818; border: 1px solid #2d2d2d; border-radius: 3px; "
            "padding: 8px 10px; font-family: \"JetBrains Mono\", \"Fira Code\", monospace; "
            "font-size: 11.5px; color: #d4d4d4; overflow-x: auto; white-space: pre; margin: 6px 0;'>%1</pre>"
        ).arg(code));
        text.replace(match.captured(0), QStringLiteral("\x01CODEBLOCK_%1\x01").arg(blockIdx++));
    }

    // Phase 2: Extract and protect inline code
    QStringList inlineCodes;
    static QRegularExpression inlineCodeRe(QStringLiteral("`([^`]+)`"));
    it = inlineCodeRe.globalMatch(text);
    int icIdx = 0;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        inlineCodes.append(QStringLiteral(
            "<code style='background: rgba(255,255,255,0.07); padding: 1px 5px; border-radius: 3px; "
            "font-family: \"JetBrains Mono\", \"Fira Code\", monospace; font-size: 0.9em; "
            "color: #dcdcaa; border: 1px solid #333333;'>%1</code>"
        ).arg(match.captured(1).toHtmlEscaped()));
        text.replace(match.captured(0), QStringLiteral("\x01INLINECODE_%1\x01").arg(icIdx++));
    }

    // Phase 3: HTML-escape remaining text
    text = text.toHtmlEscaped();

    // Phase 4: Process block-level elements line by line
    QStringList lines = text.split(QLatin1Char('\n'));
    QStringList result;
    bool inTable = false;
    bool headerRow = false;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        QString trimmed = line.trimmed();

        // Horizontal rule
        if (trimmed == QStringLiteral("---") || trimmed == QStringLiteral("***") || trimmed == QStringLiteral("___")) {
            if (inTable) { result.append(QStringLiteral("</table>")); inTable = false; }
            result.append(QStringLiteral("<hr style='border: none; border-top: 1px solid #333333; margin: 10px 0;'/>"));
            continue;
        }

        // Headers
        if (trimmed.startsWith(QStringLiteral("#### "))) {
            if (inTable) { result.append(QStringLiteral("</table>")); inTable = false; }
            result.append(QStringLiteral("<div style='font-size: 13px; font-weight: 600; color: #e1e4e8; margin: 10px 0 4px;'>%1</div>")
                          .arg(trimmed.mid(5)));
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("### "))) {
            if (inTable) { result.append(QStringLiteral("</table>")); inTable = false; }
            result.append(QStringLiteral("<div style='font-size: 14px; font-weight: 600; color: #e1e4e8; margin: 12px 0 4px;'>%1</div>")
                          .arg(trimmed.mid(4)));
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("## "))) {
            if (inTable) { result.append(QStringLiteral("</table>")); inTable = false; }
            result.append(QStringLiteral("<div style='font-size: 15px; font-weight: 600; color: #e1e4e8; margin: 14px 0 4px;'>%1</div>")
                          .arg(trimmed.mid(3)));
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("# "))) {
            if (inTable) { result.append(QStringLiteral("</table>")); inTable = false; }
            result.append(QStringLiteral("<div style='font-size: 16px; font-weight: 700; color: #e1e4e8; margin: 16px 0 6px;'>%1</div>")
                          .arg(trimmed.mid(2)));
            continue;
        }

        // Table rows (pipe-delimited)
        if (trimmed.startsWith(QLatin1Char('|')) && trimmed.endsWith(QLatin1Char('|')) && trimmed.count(QLatin1Char('|')) >= 3) {
            // Skip separator rows (|---|---|)
            static QRegularExpression sepRe(QStringLiteral("^\\|[\\s:-]+\\|$"));
            if (sepRe.match(trimmed.simplified().remove(QLatin1Char(' '))).hasMatch()
                || trimmed.contains(QStringLiteral("---"))) {
                headerRow = true;
                continue;
            }

            if (!inTable) {
                result.append(QStringLiteral(
                    "<table style='border-collapse: collapse; font-size: 12px; margin: 6px 0; width: 100%;'>"));
                inTable = true;
                headerRow = false;
            }

            QStringList cells = trimmed.mid(1, trimmed.length() - 2).split(QLatin1Char('|'));
            bool isHeader = !headerRow && (i + 1 < lines.size()) && lines[i + 1].trimmed().contains(QStringLiteral("---"));

            result.append(QStringLiteral("<tr>"));
            for (const QString &cell : cells) {
                QString tag = isHeader ? QStringLiteral("th") : QStringLiteral("td");
                QString style = isHeader
                    ? QStringLiteral("style='padding: 4px 8px; border-bottom: 1px solid #3c3c3c; color: #e1e4e8; font-weight: 600; text-align: left; white-space: nowrap;'")
                    : QStringLiteral("style='padding: 4px 8px; border-bottom: 1px solid #2d2d2d; color: #d4d4d4;'");
                result.append(QStringLiteral("<%1 %2>%3</%1>").arg(tag, style, cell.trimmed()));
            }
            result.append(QStringLiteral("</tr>"));
            continue;
        }

        // Close table if line is not a table row
        if (inTable) {
            result.append(QStringLiteral("</table>"));
            inTable = false;
        }

        // Bullet lists
        static QRegularExpression bulletRe(QStringLiteral("^(\\s*)[-*]\\s+(.*)$"));
        QRegularExpressionMatch bulletMatch = bulletRe.match(line);
        if (bulletMatch.hasMatch()) {
            int indent = bulletMatch.captured(1).length();
            int marginLeft = 8 + indent * 6;
            result.append(QStringLiteral(
                "<div style='margin: 2px 0 2px %1px; color: #d4d4d4;'>"
                "<span style='color: #858585; margin-right: 6px;'>&#8226;</span>%2</div>"
            ).arg(marginLeft).arg(bulletMatch.captured(2)));
            continue;
        }

        // Numbered lists
        static QRegularExpression numRe(QStringLiteral("^(\\s*)(\\d+)\\.\\s+(.*)$"));
        QRegularExpressionMatch numMatch = numRe.match(line);
        if (numMatch.hasMatch()) {
            int indent = numMatch.captured(1).length();
            int marginLeft = 8 + indent * 6;
            result.append(QStringLiteral(
                "<div style='margin: 2px 0 2px %1px; color: #d4d4d4;'>"
                "<span style='color: #858585; margin-right: 6px;'>%2.</span>%3</div>"
            ).arg(marginLeft).arg(numMatch.captured(2), numMatch.captured(3)));
            continue;
        }

        // Regular paragraph line
        if (trimmed.isEmpty()) {
            result.append(QStringLiteral("<div style='height: 6px;'></div>"));
        } else {
            result.append(QStringLiteral("<div>%1</div>").arg(line));
        }
    }

    if (inTable) {
        result.append(QStringLiteral("</table>"));
    }

    text = result.join(QLatin1Char('\n'));

    // Phase 5: Inline formatting
    // Bold **text**
    text.replace(QRegularExpression(QStringLiteral("\\*\\*(.+?)\\*\\*")), QStringLiteral("<b>\\1</b>"));
    // Strikethrough ~~text~~
    text.replace(QRegularExpression(QStringLiteral("~~(.+?)~~")), QStringLiteral("<s style='color: #858585;'>\\1</s>"));
    // Italic *text* (but not inside ** or HTML tags)
    text.replace(QRegularExpression(QStringLiteral("(?<!\\*)\\*([^*]+?)\\*(?!\\*)")), QStringLiteral("<i>\\1</i>"));

    // Phase 6: Restore protected code blocks and inline code
    for (int i = 0; i < codeBlocks.size(); ++i) {
        text.replace(QStringLiteral("\x01CODEBLOCK_%1\x01").arg(i), codeBlocks[i]);
    }
    for (int i = 0; i < inlineCodes.size(); ++i) {
        text.replace(QStringLiteral("\x01INLINECODE_%1\x01").arg(i), inlineCodes[i]);
    }

    return text;
}

// ─── Auto-Follow Scroll Logic (Velo useChatScrollController pattern) ────────
bool AIChatWidget::isScrollNearBottom() const
{
    if (!m_messageStream) return true;
    QScrollBar *sb = m_messageStream->verticalScrollBar();
    return (sb->maximum() - sb->value()) <= 48;
}

void AIChatWidget::scrollToBottomIfFollowing()
{
    if (!m_messageStream) return;

    // Update auto-follow state based on current position
    m_autoFollow = isScrollNearBottom();

    if (m_autoFollow) {
        QScrollBar *sb = m_messageStream->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

// ─── Message Append Methods (with history tracking and auto-follow) ─────────
void AIChatWidget::appendUserMessage(const QString &text)
{
    ChatMessageEntry entry;
    entry.role = ChatMessageEntry::User;
    entry.text = text;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(entry);

    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));

    QString html = QStringLiteral(
        "<div style='margin: 10px 0; text-align: right;'>"
        "  <div style='display: inline-block; max-width: 85%%; background: #252526; "
        "    color: #d4d4d4; border-radius: 4px; padding: 8px 12px; text-align: left; "
        "    word-break: break-word; font-size: 12.5px; line-height: 1.5;'>"
        "    %1"
        "  </div>"
        "  <div style='font-size: 9.5px; color: #555555; margin-top: 2px;'>%2</div>"
        "</div>"
    ).arg(text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>")), timestamp);

    m_messageStream->append(html);
    scrollToBottomIfFollowing();
}

void AIChatWidget::appendAssistantResponse(const QString &text)
{
    if (text.trimmed().isEmpty()) return;

    // Parse <thinking> blocks from response
    QString visibleText = text;
    QString thinkingText;
    static QRegularExpression thinkingRe(QStringLiteral("<thinking>(.*?)</thinking>"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch thinkMatch = thinkingRe.match(text);
    if (thinkMatch.hasMatch()) {
        thinkingText = thinkMatch.captured(1).trimmed();
        visibleText.remove(thinkMatch.captured(0));
        visibleText = visibleText.trimmed();
    }
    // Also handle <think> tags (DeepSeek, Qwen)
    static QRegularExpression thinkRe(QStringLiteral("<think>(.*?)</think>"),
                                       QRegularExpression::DotMatchesEverythingOption);
    thinkMatch = thinkRe.match(visibleText);
    if (thinkMatch.hasMatch()) {
        if (thinkingText.isEmpty()) {
            thinkingText = thinkMatch.captured(1).trimmed();
        } else {
            thinkingText += QStringLiteral("\n") + thinkMatch.captured(1).trimmed();
        }
        visibleText.remove(thinkMatch.captured(0));
        visibleText = visibleText.trimmed();
    }

    ChatMessageEntry entry;
    entry.role = ChatMessageEntry::Assistant;
    entry.text = visibleText;
    entry.thinkingText = thinkingText;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(entry);

    QString html;

    // Render thinking block (collapsible-style, dimmed, monospace)
    if (!thinkingText.isEmpty()) {
        html += QStringLiteral(
            "<div style='margin: 8px 0 4px; padding: 6px 10px; background: #181818; "
            "  border: 1px solid #2d2d2d; border-radius: 3px;'>"
            "  <div style='font-size: 10.5px; color: #858585; margin-bottom: 4px; font-weight: 600;'>"
            "    Thinking Process</div>"
            "  <div style='font-family: \"JetBrains Mono\", \"Fira Code\", monospace; font-size: 11px; "
            "    color: #858585; font-style: italic; line-height: 1.45; max-height: 180px; overflow-y: auto;'>"
            "    %1"
            "  </div>"
            "</div>"
        ).arg(thinkingText.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>")));
    }

    // Render visible response with enhanced markdown
    if (!visibleText.isEmpty()) {
        QString htmlText = formatMarkdownHtml(visibleText);
        html += QStringLiteral(
            "<div style='margin: 8px 0; color: #d4d4d4; font-size: 13px; line-height: 1.55; word-break: break-word;'>"
            "  %1"
            "</div>"
        ).arg(htmlText);
    }

    m_messageStream->append(html);
    scrollToBottomIfFollowing();
}

void AIChatWidget::appendToolExecution(const QString &toolName, const QString &paramsSummary,
                                        bool success, const QString &errorMsg)
{
    // Store in message history
    ChatMessageEntry entry;
    entry.role = ChatMessageEntry::Tool;
    entry.toolName = toolName;
    entry.toolArgsSummary = paramsSummary;
    entry.toolSuccess = success;
    entry.toolError = errorMsg;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(entry);

    // Record in inspector tool outcomes ring buffer
    ToolOutcomeEntry outcome;
    outcome.toolName = toolName;
    outcome.argsSummary = paramsSummary;
    outcome.success = success;
    outcome.error = errorMsg;
    outcome.timestamp = QDateTime::currentMSecsSinceEpoch();
    if (m_toolOutcomes.size() >= MAX_TOOL_OUTCOMES) {
        m_toolOutcomes.removeFirst();
    }
    m_toolOutcomes.append(outcome);

    // Update session metrics
    m_sessionMetrics.totalToolCalls++;
    if (success) {
        m_sessionMetrics.successfulTools++;
    } else {
        m_sessionMetrics.failedTools++;
    }

    // Check for tool grouping: if the last TOOL_GROUP_MIN messages are all
    // Tool entries with the same name, re-render as a collapsed group.
    int consecutiveCount = 0;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == ChatMessageEntry::Tool && m_messages[i].toolName == toolName) {
            consecutiveCount++;
        } else {
            break;
        }
    }

    if (consecutiveCount >= TOOL_GROUP_MIN) {
        // Re-render the entire message view with grouping applied
        rebuildMessageView();
        return;
    }

    // Standard single tool execution row
    // Colored dot: green (#4ec9b0) for success, red (#f14c4c) for failure
    QString dotColor = success ? QStringLiteral("#4ec9b0") : QStringLiteral("#f14c4c");

    // Smart arg summary
    QString summary = paramsSummary;
    if (!summary.isEmpty() && summary.startsWith(QLatin1Char('{'))) {
        summary = extractToolArgSummary(summary);
    }

    QString html = QStringLiteral(
        "<div style='margin: 3px 0; display: flex; align-items: flex-start; font-size: 12px; color: #858585; line-height: 1.45;'>"
        "  <span style='color: %1; font-size: 10px; margin-right: 8px; margin-top: 2px;'>&#9679;</span>"
        "  <span style='font-family: \"JetBrains Mono\", \"Fira Code\", monospace; color: #9cdcfe; font-size: 11.5px;'>%2</span>"
        "  %3"
        "  %4"
        "</div>"
    ).arg(dotColor, toolName.toHtmlEscaped(),
          summary.isEmpty() ? QString() : QStringLiteral("<span style='color: #555555; margin-left: 6px;'> &middot; %1</span>").arg(summary.toHtmlEscaped()),
          errorMsg.isEmpty() ? QString() : QStringLiteral("<div style='color: #f14c4c; font-size: 11px; margin-left: 20px;'>%1</div>").arg(errorMsg.toHtmlEscaped()));

    m_messageStream->append(html);
    scrollToBottomIfFollowing();
}

void AIChatWidget::appendSystemMessage(const QString &text)
{
    ChatMessageEntry entry;
    entry.role = ChatMessageEntry::System;
    entry.text = text;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(entry);

    QString html = QStringLiteral(
        "<div style='margin: 6px 0; text-align: center; color: #555555; font-size: 10.5px; font-style: italic;'>"
        "  %1"
        "</div>"
    ).arg(text.toHtmlEscaped());

    m_messageStream->append(html);
    scrollToBottomIfFollowing();
}

// ─── Rebuild Message View with Tool Grouping (Velo message-groups.ts) ───────
// Re-renders the entire message stream, applying tool group collapsing
// for 3+ consecutive calls to the same tool.
void AIChatWidget::rebuildMessageView()
{
    m_messageStream->clear();

    // Apply virtual windowing: only render last MESSAGE_WINDOW_SIZE messages
    int startIdx = qMax(0, m_messages.size() - MESSAGE_WINDOW_SIZE);

    if (startIdx > 0) {
        m_messageStream->append(QStringLiteral(
            "<div style='text-align: center; color: #555555; font-size: 10.5px; padding: 6px; cursor: pointer;'>"
            "  %1 earlier messages"
            "</div>"
        ).arg(startIdx));
    }

    int i = startIdx;
    while (i < m_messages.size()) {
        const ChatMessageEntry &msg = m_messages[i];

        if (msg.role == ChatMessageEntry::Tool) {
            // Count consecutive tool calls with the same name
            int j = i + 1;
            while (j < m_messages.size()
                   && m_messages[j].role == ChatMessageEntry::Tool
                   && m_messages[j].toolName == msg.toolName) {
                j++;
            }
            int runLength = j - i;

            if (runLength >= TOOL_GROUP_MIN) {
                // Render as collapsed group
                int successCount = 0;
                int failCount = 0;
                for (int k = i; k < j; ++k) {
                    if (m_messages[k].toolSuccess) successCount++;
                    else failCount++;
                }

                QString dotColor = (failCount > 0) ? QStringLiteral("#f14c4c") : QStringLiteral("#4ec9b0");
                QString countLabel = (failCount > 0)
                    ? QStringLiteral("%1 calls (%2 failed)").arg(runLength).arg(failCount)
                    : QStringLiteral("%1 calls").arg(runLength);

                QString groupHtml = QStringLiteral(
                    "<div style='margin: 4px 0; font-size: 12px; color: #858585; line-height: 1.45;'>"
                    "  <span style='color: %1; font-size: 10px; margin-right: 8px;'>&#9679;</span>"
                    "  <span style='font-family: \"JetBrains Mono\", \"Fira Code\", monospace; color: #9cdcfe; font-size: 11.5px;'>%2</span>"
                    "  <span style='color: #555555; margin-left: 6px;'> &middot; %3</span>"
                    "</div>"
                ).arg(dotColor, msg.toolName.toHtmlEscaped(), countLabel);

                // Render individual items underneath with indent
                groupHtml += QStringLiteral("<div style='border-left: 1px solid #333333; margin-left: 12px; padding-left: 10px;'>");
                for (int k = i; k < j; ++k) {
                    const ChatMessageEntry &tool = m_messages[k];
                    QString itemDot = tool.toolSuccess ? QStringLiteral("#4ec9b0") : QStringLiteral("#f14c4c");
                    QString summary = tool.toolArgsSummary;
                    if (!summary.isEmpty() && summary.startsWith(QLatin1Char('{'))) {
                        summary = extractToolArgSummary(summary);
                    }
                    groupHtml += QStringLiteral(
                        "<div style='margin: 1px 0; font-size: 11px; color: #656565;'>"
                        "  <span style='color: %1; font-size: 8px;'>&#9679;</span>"
                        "  %2"
                        "  %3"
                        "</div>"
                    ).arg(itemDot,
                          tool.toolName.toHtmlEscaped(),
                          summary.isEmpty() ? QString() : QStringLiteral(" &middot; %1").arg(summary.toHtmlEscaped()));
                }
                groupHtml += QStringLiteral("</div>");

                m_messageStream->append(groupHtml);
                i = j;
                continue;
            }
        }

        // Render individual message
        switch (msg.role) {
        case ChatMessageEntry::User: {
            QString timestamp = QDateTime::fromMSecsSinceEpoch(msg.timestamp).toString(QStringLiteral("hh:mm"));
            m_messageStream->append(QStringLiteral(
                "<div style='margin: 10px 0; text-align: right;'>"
                "  <div style='display: inline-block; max-width: 85%%; background: #252526; "
                "    color: #d4d4d4; border-radius: 4px; padding: 8px 12px; text-align: left; "
                "    word-break: break-word; font-size: 12.5px; line-height: 1.5;'>"
                "    %1"
                "  </div>"
                "  <div style='font-size: 9.5px; color: #555555; margin-top: 2px;'>%2</div>"
                "</div>"
            ).arg(msg.text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>")), timestamp));
            break;
        }
        case ChatMessageEntry::Assistant: {
            QString html;
            if (!msg.thinkingText.isEmpty()) {
                html += QStringLiteral(
                    "<div style='margin: 8px 0 4px; padding: 6px 10px; background: #181818; "
                    "  border: 1px solid #2d2d2d; border-radius: 3px;'>"
                    "  <div style='font-size: 10.5px; color: #858585; margin-bottom: 4px; font-weight: 600;'>"
                    "    Thinking Process</div>"
                    "  <div style='font-family: \"JetBrains Mono\", \"Fira Code\", monospace; font-size: 11px; "
                    "    color: #858585; font-style: italic; line-height: 1.45; max-height: 180px; overflow-y: auto;'>"
                    "    %1"
                    "  </div>"
                    "</div>"
                ).arg(msg.thinkingText.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>")));
            }
            if (!msg.text.isEmpty()) {
                html += QStringLiteral(
                    "<div style='margin: 8px 0; color: #d4d4d4; font-size: 13px; line-height: 1.55; word-break: break-word;'>"
                    "  %1"
                    "</div>"
                ).arg(formatMarkdownHtml(msg.text));
            }
            m_messageStream->append(html);
            break;
        }
        case ChatMessageEntry::Tool: {
            QString dotColor = msg.toolSuccess ? QStringLiteral("#4ec9b0") : QStringLiteral("#f14c4c");
            QString summary = msg.toolArgsSummary;
            if (!summary.isEmpty() && summary.startsWith(QLatin1Char('{'))) {
                summary = extractToolArgSummary(summary);
            }
            m_messageStream->append(QStringLiteral(
                "<div style='margin: 3px 0; font-size: 12px; color: #858585; line-height: 1.45;'>"
                "  <span style='color: %1; font-size: 10px; margin-right: 8px;'>&#9679;</span>"
                "  <span style='font-family: \"JetBrains Mono\", \"Fira Code\", monospace; color: #9cdcfe; font-size: 11.5px;'>%2</span>"
                "  %3%4"
                "</div>"
            ).arg(dotColor, msg.toolName.toHtmlEscaped(),
                  summary.isEmpty() ? QString() : QStringLiteral(" <span style='color: #555555;'>&middot; %1</span>").arg(summary.toHtmlEscaped()),
                  msg.toolError.isEmpty() ? QString() : QStringLiteral("<div style='color: #f14c4c; font-size: 11px; margin-left: 20px;'>%1</div>").arg(msg.toolError.toHtmlEscaped())));
            break;
        }
        case ChatMessageEntry::System:
            m_messageStream->append(QStringLiteral(
                "<div style='margin: 6px 0; text-align: center; color: #555555; font-size: 10.5px; font-style: italic;'>%1</div>"
            ).arg(msg.text.toHtmlEscaped()));
            break;
        }
        i++;
    }

    scrollToBottomIfFollowing();
}

// ─── Mode Badge ─────────────────────────────────────────────────────────────
void AIChatWidget::updateModeBadge()
{
    bool isYolo = m_modeYoloRadio ? m_modeYoloRadio->isChecked() : true;
    if (m_modeBadge) {
        if (isYolo) {
            m_modeBadge->setText(i18n("YOLO"));
            m_modeBadge->setStyleSheet(QStringLiteral(
                "background-color: #252526; color: #4ec9b0; border: 1px solid #3c3c3c; "
                "border-radius: 2px; padding: 2px 6px; font-size: 10px; font-weight: 600; font-family: monospace;"
            ));
        } else {
            m_modeBadge->setText(i18n("ASK"));
            m_modeBadge->setStyleSheet(QStringLiteral(
                "background-color: #252526; color: #ce9178; border: 1px solid #3c3c3c; "
                "border-radius: 2px; padding: 2px 6px; font-size: 10px; font-weight: 600; font-family: monospace;"
            ));
        }
    }
}

// ─── Metrics Display ────────────────────────────────────────────────────────
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

    if (m_modelTag) m_modelTag->setText(QStringLiteral("Model: %1").arg(modelDisplay));
    if (m_tokensTag) m_tokensTag->setText(QStringLiteral("Tokens: %1").arg(tokensDisplay));
    if (m_latencyTag) m_latencyTag->setText(QStringLiteral("Latency: %1").arg(latencyDisplay));
}

void AIChatWidget::slotMetricsUpdated(int totalTokens, qint64 latencyMs, const QString &modelId)
{
    // Update session metrics for inspector
    m_sessionMetrics.totalRequests++;
    m_sessionMetrics.lastLatencyMs = latencyMs;
    m_sessionMetrics.totalLatencyMs += latencyMs;
    m_sessionMetrics.lastModel = modelId;
    // Token breakdown: approximate input/output split (if not available separately)
    m_sessionMetrics.lastInputTokens = totalTokens;
    m_sessionMetrics.totalInputTokens += totalTokens;

    updateMetricsDisplay(totalTokens, latencyMs, modelId);
}

// ─── Live Run Status (Pulsing dot + thinking phrases + elapsed timer) ───────
void AIChatWidget::slotRequestStarted()
{
    m_thinkingSeed = static_cast<int>(QDateTime::currentMSecsSinceEpoch() & 0x7FFFFFFF);

    m_liveStatusBar->setVisible(true);
    m_liveStatusText->setText(QString::fromUtf8(thinkingPhrase(m_thinkingSeed)) + QStringLiteral("..."));
    m_liveStatusTimer->setText(QStringLiteral("0.0s"));
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
    qint64 elapsed = m_elapsedTimer.elapsed();
    double sec = elapsed / 1000.0;
    m_liveStatusTimer->setText(QStringLiteral("%1s").arg(sec, 0, 'f', 1));

    // Pulsing dot: sinusoidal opacity modulation (1.2s cycle, matching Velo cc-rec-pulse)
    double phase = std::sin(elapsed * 0.001 * M_PI * 2.0 / 1.2) * 0.5 + 0.5;
    int alpha = static_cast<int>(80 + phase * 175); // Range: 80-255
    m_liveStatusDot->setStyleSheet(QStringLiteral(
        "background-color: rgba(0, 122, 204, %1); border-radius: 4px; border: none;"
    ).arg(alpha));

    // Rotate thinking phrases every 3 seconds
    int phraseIdx = static_cast<int>(elapsed / 3000);
    if (phraseIdx != (m_thinkingSeed % THINKING_PHRASE_COUNT)) {
        m_liveStatusText->setText(QString::fromUtf8(thinkingPhrase(m_thinkingSeed + phraseIdx)) + QStringLiteral("..."));
    }
}

// ─── Diagnostic Inspector Dialog (Velo AgentRunInspector pattern) ───────────
void AIChatWidget::slotShowInspector()
{
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(i18n("Agent Diagnostic Inspector"));
    dialog->setMinimumSize(380, 480);
    dialog->setStyleSheet(QStringLiteral(
        "QDialog { background-color: #1e1e1e; color: #cccccc; }"
        "QLabel { color: #cccccc; }"
        "QGroupBox { font-weight: bold; border: 1px solid #333333; border-radius: 2px; margin-top: 10px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 4px; color: #9cdcfe; }"
    ));

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *titleLabel = new QLabel(QStringLiteral("<b style='font-size: 13px;'>Agent Run Inspector</b>"), dialog);
    layout->addWidget(titleLabel);

    // Session Overview
    auto *sessionGroup = new QGroupBox(i18n("Session Totals"), dialog);
    auto *sessionForm = new QFormLayout(sessionGroup);
    sessionForm->setLabelAlignment(Qt::AlignLeft);

    const QString valStyle = QStringLiteral("font-family: 'JetBrains Mono', monospace; font-size: 11px; color: #d4d4d4;");

    auto makeVal = [&](const QString &text) -> QLabel * {
        auto *label = new QLabel(text, sessionGroup);
        label->setStyleSheet(valStyle);
        return label;
    };

    sessionForm->addRow(i18n("Model Requests:"), makeVal(QString::number(m_sessionMetrics.totalRequests)));
    sessionForm->addRow(i18n("Total Tokens:"), makeVal(QStringLiteral("%1").arg(m_sessionMetrics.totalInputTokens)));

    QString avgLatency = m_sessionMetrics.totalRequests > 0
        ? QStringLiteral("%1ms").arg(m_sessionMetrics.totalLatencyMs / m_sessionMetrics.totalRequests)
        : QStringLiteral("-");
    sessionForm->addRow(i18n("Avg Latency:"), avgLatency.isEmpty() ? makeVal(QStringLiteral("-")) : makeVal(avgLatency));
    sessionForm->addRow(i18n("Active Model:"), makeVal(m_sessionMetrics.lastModel.isEmpty() ? QStringLiteral("auto") : m_sessionMetrics.lastModel));
    layout->addWidget(sessionGroup);

    // Tool Execution Summary
    auto *toolGroup = new QGroupBox(i18n("Tool Execution"), dialog);
    auto *toolForm = new QFormLayout(toolGroup);
    toolForm->setLabelAlignment(Qt::AlignLeft);

    toolForm->addRow(i18n("Total Calls:"), makeVal(QString::number(m_sessionMetrics.totalToolCalls)));

    auto *successLabel = new QLabel(QString::number(m_sessionMetrics.successfulTools), toolGroup);
    successLabel->setStyleSheet(QStringLiteral("font-family: 'JetBrains Mono', monospace; font-size: 11px; color: #4ec9b0;"));
    toolForm->addRow(i18n("Successful:"), successLabel);

    auto *failLabel = new QLabel(QString::number(m_sessionMetrics.failedTools), toolGroup);
    failLabel->setStyleSheet(QStringLiteral("font-family: 'JetBrains Mono', monospace; font-size: 11px; color: %1;")
                              .arg(m_sessionMetrics.failedTools > 0 ? QStringLiteral("#f14c4c") : QStringLiteral("#d4d4d4")));
    toolForm->addRow(i18n("Failed:"), failLabel);

    double successRate = m_sessionMetrics.totalToolCalls > 0
        ? (m_sessionMetrics.successfulTools * 100.0 / m_sessionMetrics.totalToolCalls)
        : 0;
    toolForm->addRow(i18n("Success Rate:"), makeVal(QStringLiteral("%1%").arg(successRate, 0, 'f', 1)));
    layout->addWidget(toolGroup);

    // Recent Tool Outcomes (last N, reverse chronological)
    if (!m_toolOutcomes.isEmpty()) {
        auto *historyGroup = new QGroupBox(i18n("Recent Tool Outcomes"), dialog);
        auto *historyLayout = new QVBoxLayout(historyGroup);
        historyLayout->setSpacing(2);

        for (int i = m_toolOutcomes.size() - 1; i >= 0; --i) {
            const ToolOutcomeEntry &o = m_toolOutcomes[i];
            QString dotColor = o.success ? QStringLiteral("#4ec9b0") : QStringLiteral("#f14c4c");
            QString status = o.success ? QStringLiteral("success") : QStringLiteral("failed");

            auto *row = new QLabel(historyGroup);
            row->setTextFormat(Qt::RichText);
            row->setText(QStringLiteral(
                "<span style='color: %1; font-size: 9px;'>&#9679;</span> "
                "<span style='font-family: \"JetBrains Mono\", monospace; font-size: 11px; color: #9cdcfe;'>%2</span>"
                " <span style='color: #555555; font-size: 10px;'>%3</span>"
                "%4"
            ).arg(dotColor, o.toolName.toHtmlEscaped(), status,
                  o.argsSummary.isEmpty() ? QString() : QStringLiteral(" <span style='color: #555555; font-size: 10px;'>&middot; %1</span>").arg(o.argsSummary.left(30).toHtmlEscaped())));
            historyLayout->addWidget(row);
        }
        layout->addWidget(historyGroup);
    }

    // Last Request Breakdown
    auto *lastGroup = new QGroupBox(i18n("Last Request"), dialog);
    auto *lastForm = new QFormLayout(lastGroup);
    lastForm->setLabelAlignment(Qt::AlignLeft);
    lastForm->addRow(i18n("Input Tokens:"), makeVal(QString::number(m_sessionMetrics.lastInputTokens)));
    lastForm->addRow(i18n("Latency:"), makeVal(QStringLiteral("%1ms").arg(m_sessionMetrics.lastLatencyMs)));
    lastForm->addRow(i18n("Model:"), makeVal(m_sessionMetrics.lastModel.isEmpty() ? QStringLiteral("auto") : m_sessionMetrics.lastModel));
    layout->addWidget(lastGroup);

    layout->addStretch(1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttonBox->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 2px; padding: 5px 16px; color: #cccccc; }"
        "QPushButton:hover { background-color: #2d2d2d; border-color: #007acc; }"
    ));
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttonBox);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

// ─── Existing Slots (Preserved) ─────────────────────────────────────────────
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
        if (m_dynamicModels.isEmpty()) {
            m_modelsStatusLabel->setText(i18n("No live models loaded. Check connection and click Refresh."));
        } else {
            m_modelsStatusLabel->setText(i18n("Loaded %1 live models from AI proxy.", m_dynamicModels.size()));
        }
    }

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

    for (const auto &m : m_dynamicModels) {
        if (provider != QStringLiteral("all") && m.provider != provider) continue;
        if (!query.isEmpty() && !m.id.toLower().contains(query) && !m.name.toLower().contains(query)) continue;

        QString ctxStr = m.contextLength > 0 ? QStringLiteral(" (%1k ctx)").arg(m.contextLength / 1000) : QString();
        QString display = QStringLiteral("[%1] %2%3").arg(m.provider.toUpper(), m.name.isEmpty() ? m.id : m.name, ctxStr);
        m_editingModelSelector->addItem(display, m.id);
    }

    int idx = m_editingModelSelector->findData(currentSelectedId);
    if (idx >= 0) m_editingModelSelector->setCurrentIndex(idx);
}

void AIChatWidget::slotSaveSettings()
{
    AIAgentSettings s;
    s.mode = m_modeYoloRadio->isChecked() ? QStringLiteral("yolo") : QStringLiteral("ask");
    s.editingModelId = m_editingModelSelector->currentData().toString();
    if (s.editingModelId.isEmpty()) s.editingModelId = QStringLiteral("auto");
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
    m_messages.clear();
    m_toolOutcomes.clear();
    m_sessionMetrics = SessionMetrics{};
    m_proposalCard->setVisible(false);
    appendSystemMessage(i18n("History cleared."));
}

void AIChatWidget::slotResponseReceived(const QString &summaryText, const QJsonObject &actionPayload)
{
    appendAssistantResponse(summaryText);

    if (actionPayload.isEmpty()) return;

    AIAgentSettings currentSettings = m_dispatcher->agentSettings();

    if (currentSettings.mode == QStringLiteral("ask")) {
        m_pendingProposalAction = actionPayload;
        QString actionName = actionPayload[QStringLiteral("action")].toString();
        m_proposalTitle->setText(i18n("Proposed Action: %1", actionName));
        m_proposalSummary->setText(i18n("The Agent proposes executing '%1' on your project timeline.", actionName));
        m_proposalCard->setVisible(true);
    } else {
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
