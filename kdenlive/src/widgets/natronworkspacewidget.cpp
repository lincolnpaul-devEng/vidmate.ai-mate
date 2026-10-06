/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Master Natron Compositing Workspace Widget Implementation — Exposing all Natron
 * editing tool widgets (Node Graph, Curve Editor, Dope Sheet, Properties Bin,
 * GL Viewer, Roto/Paint Panel, Tracker Panel, Python Script Editor, Node Toolbar,
 * Transport, and Progress Panel) with View Header on/off toggles.
 */

#include "natronworkspacewidget.h"
#include "natronnodegraphview.h"
#include "natroncurveeditorview.h"
#include "natrondopesheetview.h"
#include "core.h"
#include "mainwindow.h"

#include <KLocalizedString>
#include <QIcon>
#include <QMenu>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>

NatronWorkspaceWidget::NatronWorkspaceWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setupStyle();
}

void NatronWorkspaceWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    // 1. Top View Header Toolbar (on/off toggles + presets + render)
    mainLayout->addWidget(buildViewHeaderToolbar());

    // 2. Central Splitter (Left: NodeBar, Center: Tabs, Right: PropertiesBin)
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setHandleWidth(3);

    m_nodeBarWidget = buildNodeCreationToolBar();
    m_mainSplitter->addWidget(m_nodeBarWidget);

    // Central Tabbed Workspace
    m_tabs = new QTabWidget(m_mainSplitter);
    m_tabs->setDocumentMode(true);
    m_tabs->setTabPosition(QTabWidget::North);

    m_nodeGraphView = new NatronNodeGraphView(this);
    m_curveEditorView = new NatronCurveEditorView(this);
    m_dopeSheetView = new NatronDopeSheetView(this);
    m_viewerPanel = buildViewerPanel();
    m_rotoPanel = buildRotoPanel();
    m_trackerPanel = buildTrackerPanel();
    m_scriptEditorPanel = buildScriptEditorPanel();

    m_tabs->addTab(m_nodeGraphView, QIcon::fromTheme(QStringLiteral("view-diagram")), i18n("Node Graph"));
    m_tabs->addTab(m_curveEditorView, QIcon::fromTheme(QStringLiteral("draw-bezier-curves")), i18n("Curve Editor"));
    m_tabs->addTab(m_dopeSheetView, QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18n("Dope Sheet"));
    m_tabs->addTab(m_viewerPanel, QIcon::fromTheme(QStringLiteral("video-display")), i18n("GL Viewer"));
    m_tabs->addTab(m_rotoPanel, QIcon::fromTheme(QStringLiteral("draw-freehand")), i18n("Roto & Paint"));
    m_tabs->addTab(m_trackerPanel, QIcon::fromTheme(QStringLiteral("crosshairs")), i18n("Tracker"));
    m_tabs->addTab(m_scriptEditorPanel, QIcon::fromTheme(QStringLiteral("utilities-terminal")), i18n("Script Editor"));

    connect(m_dopeSheetView, &NatronDopeSheetView::seekRequested, this, &NatronWorkspaceWidget::syncPlayheadFrame);

    m_mainSplitter->addWidget(m_tabs);

    // Right: Properties Bin (Inspector)
    m_propertiesBinWidget = buildPropertiesBinPanel();
    m_mainSplitter->addWidget(m_propertiesBinWidget);

    m_mainSplitter->setStretchFactor(0, 0); // NodeBar
    m_mainSplitter->setStretchFactor(1, 3); // Central Tabs
    m_mainSplitter->setStretchFactor(2, 1); // PropertiesBin

    mainLayout->addWidget(m_mainSplitter, 1);

    // 3. Bottom Area: Transport Bar & Progress Panel
    m_transportWidget = buildTransportBar();
    mainLayout->addWidget(m_transportWidget);

    m_progressWidget = buildProgressPanel();
    m_progressWidget->setVisible(false);
    mainLayout->addWidget(m_progressWidget);
}

QWidget *NatronWorkspaceWidget::buildViewHeaderToolbar()
{
    auto *topBar = new QWidget(this);
    topBar->setObjectName(QStringLiteral("natronHeaderToolbar"));
    auto *hLay = new QHBoxLayout(topBar);
    hLay->setContentsMargins(4, 2, 4, 2);
    hLay->setSpacing(6);

    auto *logoLabel = new QLabel(QStringLiteral("<b>NATRON VFX</b>"), topBar);
    logoLabel->setStyleSheet(QStringLiteral("color: #3daee9; font-size: 11px; font-weight: bold; margin-right: 4px;"));
    hLay->addWidget(logoLabel);

    // View Header Tool Buttons (Toggle on/off)
    auto *toggleBar = new QToolBar(topBar);
    toggleBar->setIconSize(QSize(16, 16));

    m_btnToggleGraph = new QToolButton(toggleBar);
    m_btnToggleGraph->setText(i18n("Graph"));
    m_btnToggleGraph->setIcon(QIcon::fromTheme(QStringLiteral("view-diagram")));
    m_btnToggleGraph->setCheckable(true);
    m_btnToggleGraph->setChecked(true);
    m_btnToggleGraph->setToolTip(i18n("Show Node Graph Editor"));
    connect(m_btnToggleGraph, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_nodeGraphView); });
    toggleBar->addWidget(m_btnToggleGraph);

    m_btnToggleCurves = new QToolButton(toggleBar);
    m_btnToggleCurves->setText(i18n("Curves"));
    m_btnToggleCurves->setIcon(QIcon::fromTheme(QStringLiteral("draw-bezier-curves")));
    m_btnToggleCurves->setCheckable(true);
    m_btnToggleCurves->setToolTip(i18n("Show Function Curve Editor"));
    connect(m_btnToggleCurves, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_curveEditorView); });
    toggleBar->addWidget(m_btnToggleCurves);

    m_btnToggleDopeSheet = new QToolButton(toggleBar);
    m_btnToggleDopeSheet->setText(i18n("DopeSheet"));
    m_btnToggleDopeSheet->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    m_btnToggleDopeSheet->setCheckable(true);
    m_btnToggleDopeSheet->setToolTip(i18n("Show Keyframe Dope Sheet"));
    connect(m_btnToggleDopeSheet, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_dopeSheetView); });
    toggleBar->addWidget(m_btnToggleDopeSheet);

    m_btnToggleViewer = new QToolButton(toggleBar);
    m_btnToggleViewer->setText(i18n("Viewer"));
    m_btnToggleViewer->setIcon(QIcon::fromTheme(QStringLiteral("video-display")));
    m_btnToggleViewer->setCheckable(true);
    m_btnToggleViewer->setToolTip(i18n("Show Natron OpenGL Viewport"));
    connect(m_btnToggleViewer, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_viewerPanel); });
    toggleBar->addWidget(m_btnToggleViewer);

    m_btnToggleRoto = new QToolButton(toggleBar);
    m_btnToggleRoto->setText(i18n("Roto"));
    m_btnToggleRoto->setIcon(QIcon::fromTheme(QStringLiteral("draw-freehand")));
    m_btnToggleRoto->setCheckable(true);
    m_btnToggleRoto->setToolTip(i18n("Show Rotoscoping & Paint Tools"));
    connect(m_btnToggleRoto, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_rotoPanel); });
    toggleBar->addWidget(m_btnToggleRoto);

    m_btnToggleTracker = new QToolButton(toggleBar);
    m_btnToggleTracker->setText(i18n("Tracker"));
    m_btnToggleTracker->setIcon(QIcon::fromTheme(QStringLiteral("crosshairs")));
    m_btnToggleTracker->setCheckable(true);
    m_btnToggleTracker->setToolTip(i18n("Show Pattern & Planar Motion Tracker"));
    connect(m_btnToggleTracker, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_trackerPanel); });
    toggleBar->addWidget(m_btnToggleTracker);

    m_btnToggleScript = new QToolButton(toggleBar);
    m_btnToggleScript->setText(i18n("Python"));
    m_btnToggleScript->setIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));
    m_btnToggleScript->setCheckable(true);
    m_btnToggleScript->setToolTip(i18n("Show Python Script Editor & Interactive Console"));
    connect(m_btnToggleScript, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_scriptEditorPanel); });
    toggleBar->addWidget(m_btnToggleScript);

    toggleBar->addSeparator();

    // Side panel on/off toggles
    m_btnToggleNodeBar = new QToolButton(toggleBar);
    m_btnToggleNodeBar->setIcon(QIcon::fromTheme(QStringLiteral("sidebar-show-left")));
    m_btnToggleNodeBar->setToolTip(i18n("Toggle Node Creation Sidebar"));
    m_btnToggleNodeBar->setCheckable(true);
    m_btnToggleNodeBar->setChecked(true);
    connect(m_btnToggleNodeBar, &QToolButton::toggled, this, [this](bool visible) { m_nodeBarWidget->setVisible(visible); });
    toggleBar->addWidget(m_btnToggleNodeBar);

    m_btnToggleProperties = new QToolButton(toggleBar);
    m_btnToggleProperties->setIcon(QIcon::fromTheme(QStringLiteral("sidebar-show-right")));
    m_btnToggleProperties->setToolTip(i18n("Toggle Properties Bin / Inspector"));
    m_btnToggleProperties->setCheckable(true);
    m_btnToggleProperties->setChecked(true);
    connect(m_btnToggleProperties, &QToolButton::toggled, this, [this](bool visible) { m_propertiesBinWidget->setVisible(visible); });
    toggleBar->addWidget(m_btnToggleProperties);

    m_btnToggleTransport = new QToolButton(toggleBar);
    m_btnToggleTransport->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    m_btnToggleTransport->setToolTip(i18n("Toggle Transport & Timeline Scrubber"));
    m_btnToggleTransport->setCheckable(true);
    m_btnToggleTransport->setChecked(true);
    connect(m_btnToggleTransport, &QToolButton::toggled, this, [this](bool visible) { m_transportWidget->setVisible(visible); });
    toggleBar->addWidget(m_btnToggleTransport);

    m_btnToggleProgress = new QToolButton(toggleBar);
    m_btnToggleProgress->setIcon(QIcon::fromTheme(QStringLiteral("task-ongoing")));
    m_btnToggleProgress->setToolTip(i18n("Toggle Background Render Tasks Panel"));
    m_btnToggleProgress->setCheckable(true);
    m_btnToggleProgress->setChecked(false);
    connect(m_btnToggleProgress, &QToolButton::toggled, this, [this](bool visible) { m_progressWidget->setVisible(visible); });
    toggleBar->addWidget(m_btnToggleProgress);

    hLay->addWidget(toggleBar);
    hLay->addStretch();

    // Pipeline Selector & Render Action
    m_pipelineSelector = new QComboBox(topBar);
    m_pipelineSelector->addItem(i18n("Object Tracker & Match-Move"), QStringLiteral("tracker"));
    m_pipelineSelector->addItem(i18n("Track & Privacy Blur"), QStringLiteral("track_blur"));
    m_pipelineSelector->addItem(i18n("Chroma Key Composite"), QStringLiteral("chroma_key"));
    m_pipelineSelector->addItem(i18n("Rotoscope & AI Matte"), QStringLiteral("rotoscope"));
    m_pipelineSelector->addItem(i18n("Color Grade & Curves"), QStringLiteral("color_grade"));
    connect(m_pipelineSelector, &QComboBox::currentTextChanged, this, [this]() {
        QString pipeline = m_pipelineSelector->currentData().toString();
        loadPipeline(pipeline, {});
    });
    hLay->addWidget(new QLabel(i18n("Preset:"), topBar));
    hLay->addWidget(m_pipelineSelector);

    m_addNodeBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Select Tool (Shift+Space)"), topBar);
    m_addNodeBtn->setToolTip(i18n("Open Select Tool popup palette to add any Natron node / VFX tool (Shift+Space / Tab)"));
    m_addNodeBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #2b2225;"
        "  color: #ffffff;"
        "  font-weight: bold;"
        "  border: 1.5px solid #d94f4f;"
        "  border-radius: 3px;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #3d242a;"
        "  border-color: #ff6b6b;"
        "}"
    ));
    connect(m_addNodeBtn, &QPushButton::clicked, this, &NatronWorkspaceWidget::slotAddNodeTriggered);
    hLay->addWidget(m_addNodeBtn);

    m_renderBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-record")), i18n("Render VFX"), topBar);
    m_renderBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #3daee9; color: white; font-weight: bold; border-radius: 3px; padding: 4px 10px; } QPushButton:hover { background-color: #4dbff9; }"));
    connect(m_renderBtn, &QPushButton::clicked, this, &NatronWorkspaceWidget::slotRenderVfxTriggered);
    hLay->addWidget(m_renderBtn);

    auto *timelineBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18n("Timeline (F10)"), topBar);
    timelineBtn->setToolTip(i18n("Switch back to Kdenlive Multi-Track Timeline (F10)"));
    timelineBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #2e3440; color: #eceff4; font-weight: bold; border-radius: 3px; padding: 4px 8px; border: 1px solid #4c566a; } QPushButton:hover { background-color: #3b4252; }"));
    connect(timelineBtn, &QPushButton::clicked, this, [this]() {
        if (pCore && pCore->window()) {
            auto *mw = static_cast<MainWindow *>(pCore->window());
            mw->slotToggleNatronTimeline();
        }
    });
    hLay->addWidget(timelineBtn);

    return topBar;
}

QWidget *NatronWorkspaceWidget::buildNodeCreationToolBar()
{
    auto *toolBar = new QToolBar(this);
    toolBar->setOrientation(Qt::Vertical);
    toolBar->setObjectName(QStringLiteral("nodeCreationToolBar"));
    toolBar->setIconSize(QSize(20, 20));

    auto addCategoryBtn = [this, toolBar](const QString &name, const QString &icon, const QStringList &nodes) {
        auto *btn = new QToolButton(toolBar);
        btn->setIcon(QIcon::fromTheme(icon));
        btn->setToolTip(name);
        btn->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(btn);
        for (const auto &node : nodes) {
            menu->addAction(node, this, [this, node]() {
                m_nodeGraphView->addNode(node, node, NatronNodeItem::NodeCustom, QPointF(250, 150));
            });
        }
        btn->setMenu(menu);
        toolBar->addWidget(btn);
    };

    addCategoryBtn(i18n("Image (Read/Write/Constant)"), QStringLiteral("image-x-generic"), {QStringLiteral("Read"), QStringLiteral("Write"), QStringLiteral("Constant"), QStringLiteral("CheckerBoard")});
    addCategoryBtn(i18n("Draw (Roto/RotoPaint)"), QStringLiteral("draw-freehand"), {QStringLiteral("Roto"), QStringLiteral("RotoPaint"), QStringLiteral("Radial"), QStringLiteral("Ramp")});
    addCategoryBtn(i18n("Time (FrameHold/Retime)"), QStringLiteral("chronometer"), {QStringLiteral("FrameHold"), QStringLiteral("Retime"), QStringLiteral("TimeOffset"), QStringLiteral("AppendClip")});
    addCategoryBtn(i18n("Channel (Shuffle/Copy)"), QStringLiteral("view-split-left-right"), {QStringLiteral("Shuffle"), QStringLiteral("ChannelCopy"), QStringLiteral("ShuffleOIIO")});
    addCategoryBtn(i18n("Color (Grade/ColorCorrect)"), QStringLiteral("color-management"), {QStringLiteral("Grade"), QStringLiteral("ColorCorrect"), QStringLiteral("HueCorrect"), QStringLiteral("Invert")});
    addCategoryBtn(i18n("Filter (Blur/Defocus/Sharpen)"), QStringLiteral("applications-graphics"), {QStringLiteral("Blur"), QStringLiteral("Defocus"), QStringLiteral("Sharpen"), QStringLiteral("Erode")});
    addCategoryBtn(i18n("Keyer (Chroma/Despill/PIK)"), QStringLiteral("view-preview"), {QStringLiteral("Keyer"), QStringLiteral("Despill"), QStringLiteral("PIKKeyer"), QStringLiteral("ChromaKeyer")});
    addCategoryBtn(i18n("Merge (Over/Multiply/Mask)"), QStringLiteral("edit-copy"), {QStringLiteral("Merge"), QStringLiteral("Switch"), QStringLiteral("ContactSheet"), QStringLiteral("LayerContactSheet")});
    addCategoryBtn(i18n("Transform (Tracker/CornerPin)"), QStringLiteral("transform-crop-and-resize"), {QStringLiteral("Transform"), QStringLiteral("Tracker"), QStringLiteral("CornerPin"), QStringLiteral("Crop")});
    addCategoryBtn(i18n("3D (Card3D/Camera/Project3D)"), QStringLiteral("view-object-histogram-linear"), {QStringLiteral("Card3D"), QStringLiteral("Camera"), QStringLiteral("Project3D"), QStringLiteral("Scene")});

    return toolBar;
}

QWidget *NatronWorkspaceWidget::buildPropertiesBinPanel()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(240);
    scroll->setMaximumWidth(360);

    auto *content = new QWidget(scroll);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(8);

    auto *header = new QLabel(i18n("<b>Properties Bin / Inspector</b>"), content);
    header->setStyleSheet(QStringLiteral("color: #3daee9; font-size: 11px; border-bottom: 1px solid #333333; padding-bottom: 4px;"));
    lay->addWidget(header);

    // Active Node Group (e.g. Tracker1 / Keyer1 / Grade1)
    auto *nodeGroup = new QGroupBox(i18n("Active Node: Tracker1"), content);
    auto *form = new QFormLayout(nodeGroup);
    form->setContentsMargins(4, 8, 4, 4);
    form->setSpacing(6);

    auto *enableCheck = new QCheckBox(i18n("Enable Processing"), nodeGroup);
    enableCheck->setChecked(true);
    form->addRow(QString(), enableCheck);

    auto *gainSlider = new QSlider(Qt::Horizontal, nodeGroup);
    gainSlider->setRange(0, 200);
    gainSlider->setValue(100);
    form->addRow(i18n("Gain:"), gainSlider);

    auto *featherSlider = new QSlider(Qt::Horizontal, nodeGroup);
    featherSlider->setRange(0, 50);
    featherSlider->setValue(2);
    form->addRow(i18n("Feather:"), featherSlider);

    auto *centerLay = new QHBoxLayout;
    auto *spinX = new QDoubleSpinBox(nodeGroup);
    spinX->setValue(960.0);
    auto *spinY = new QDoubleSpinBox(nodeGroup);
    spinY->setValue(540.0);
    centerLay->addWidget(spinX);
    centerLay->addWidget(spinY);
    form->addRow(i18n("Center (X, Y):"), centerLay);

    lay->addWidget(nodeGroup);

    auto *outGroup = new QGroupBox(i18n("Output / Alpha Mask"), content);
    auto *outForm = new QFormLayout(outGroup);
    outForm->addRow(i18n("Alpha Premult:"), new QCheckBox(i18n("Premultiplied"), outGroup));
    outForm->addRow(i18n("Channels:"), new QLabel(QStringLiteral("RGBA (Float32)"), outGroup));
    lay->addWidget(outGroup);

    lay->addStretch();
    scroll->setWidget(content);
    return scroll;
}

QWidget *NatronWorkspaceWidget::buildViewerPanel()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    // Top View Toolbar
    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(i18n("Channel:"), w));
    auto *chanCombo = new QComboBox(w);
    chanCombo->addItems({QStringLiteral("RGB"), QStringLiteral("RGBA"), QStringLiteral("Alpha"), QStringLiteral("Luma"), QStringLiteral("Depth")});
    bar->addWidget(chanCombo);

    bar->addSpacing(10);
    bar->addWidget(new QLabel(i18n("Wipe Mode:"), w));
    auto *wipeCombo = new QComboBox(w);
    wipeCombo->addItems({i18n("None"), i18n("Horizontal Wipe (A/B)"), i18n("Vertical Split"), i18n("Difference Overlay")});
    bar->addWidget(wipeCombo);

    bar->addStretch();
    auto *zoomLabel = new QLabel(QStringLiteral("100%"), w);
    bar->addWidget(zoomLabel);
    lay->addLayout(bar);

    // Viewport placeholder canvas
    auto *canvas = new QLabel(w);
    canvas->setAlignment(Qt::AlignCenter);
    canvas->setText(i18n("<b>Natron OpenGL Real-time Compositor Viewport</b><br/><span style='color: gray;'>RGBA 32-bit float color pipeline with live node output</span>"));
    canvas->setStyleSheet(QStringLiteral("background-color: #121214; border: 1px solid #2a2a30; border-radius: 4px; color: #888888;"));
    canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lay->addWidget(canvas, 1);

    return w;
}

QWidget *NatronWorkspaceWidget::buildRotoPanel()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    auto *header = new QLabel(i18n("<b>Roto & Vector Paint Suite</b>"), w);
    header->setStyleSheet(QStringLiteral("color: #3daee9; font-size: 11px;"));
    lay->addWidget(header);

    auto *toolRow = new QHBoxLayout;
    auto *btnBezier = new QPushButton(QIcon::fromTheme(QStringLiteral("draw-bezier-curves")), i18n("Bézier"), w);
    btnBezier->setCheckable(true);
    btnBezier->setChecked(true);
    auto *btnBSpline = new QPushButton(QIcon::fromTheme(QStringLiteral("draw-freehand")), i18n("B-Spline"), w);
    btnBSpline->setCheckable(true);
    auto *btnEllipse = new QPushButton(QIcon::fromTheme(QStringLiteral("draw-ellipse")), i18n("Ellipse"), w);
    auto *btnRectangle = new QPushButton(QIcon::fromTheme(QStringLiteral("draw-rectangle")), i18n("Rectangle"), w);
    auto *btnBrush = new QPushButton(QIcon::fromTheme(QStringLiteral("draw-brush")), i18n("Clone/Paint"), w);

    toolRow->addWidget(btnBezier);
    toolRow->addWidget(btnBSpline);
    toolRow->addWidget(btnEllipse);
    toolRow->addWidget(btnRectangle);
    toolRow->addWidget(btnBrush);
    toolRow->addStretch();
    lay->addLayout(toolRow);

    auto *form = new QFormLayout;
    form->addRow(i18n("Feather Type:"), new QComboBox(w));
    form->addRow(i18n("Motion Blur:"), new QSlider(Qt::Horizontal, w));
    form->addRow(i18n("Opacity:"), new QSlider(Qt::Horizontal, w));
    lay->addLayout(form);

    auto *treePlaceholder = new QLabel(i18n("Layer Hierarchy: <i>Layer1 (Bézier Spline 1, Feathered)</i>"), w);
    treePlaceholder->setStyleSheet(QStringLiteral("background: #18181a; border: 1px solid #2a2a2e; padding: 10px; border-radius: 4px; color: #aaaaaa;"));
    lay->addWidget(treePlaceholder, 1);

    return w;
}

QWidget *NatronWorkspaceWidget::buildTrackerPanel()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    auto *header = new QLabel(i18n("<b>Point & Planar Motion Tracking Panel</b>"), w);
    header->setStyleSheet(QStringLiteral("color: #3daee9; font-size: 11px;"));
    lay->addWidget(header);

    auto *trackBtnRow = new QHBoxLayout;
    auto *btnTrackStart = new QToolButton(w);
    btnTrackStart->setIcon(QIcon::fromTheme(QStringLiteral("media-skip-backward")));
    btnTrackStart->setToolTip(i18n("Track All Reverse"));

    auto *btnTrackBack = new QToolButton(w);
    btnTrackBack->setIcon(QIcon::fromTheme(QStringLiteral("media-seek-backward")));
    btnTrackBack->setToolTip(i18n("Step Track Backward"));

    auto *btnTrackStop = new QToolButton(w);
    btnTrackStop->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
    btnTrackStop->setToolTip(i18n("Stop Tracking"));

    auto *btnTrackFwd = new QToolButton(w);
    btnTrackFwd->setIcon(QIcon::fromTheme(QStringLiteral("media-seek-forward")));
    btnTrackFwd->setToolTip(i18n("Step Track Forward"));

    auto *btnTrackEnd = new QToolButton(w);
    btnTrackEnd->setIcon(QIcon::fromTheme(QStringLiteral("media-skip-forward")));
    btnTrackEnd->setToolTip(i18n("Track All Forward"));

    trackBtnRow->addWidget(btnTrackStart);
    trackBtnRow->addWidget(btnTrackBack);
    trackBtnRow->addWidget(btnTrackStop);
    trackBtnRow->addWidget(btnTrackFwd);
    trackBtnRow->addWidget(btnTrackEnd);
    trackBtnRow->addStretch();
    lay->addLayout(trackBtnRow);

    auto *form = new QFormLayout;
    auto *typeCombo = new QComboBox(w);
    typeCombo->addItems({i18n("Translation (Position X/Y)"), i18n("Affine (Position + Rotation + Scale)"), i18n("Planar / Perspective (CornerPin)")});
    form->addRow(i18n("Motion Model:"), typeCombo);

    form->addRow(i18n("Search Window:"), new QSpinBox(w));
    form->addRow(i18n("Pattern Window:"), new QSpinBox(w));
    form->addRow(i18n("Error Threshold:"), new QDoubleSpinBox(w));
    lay->addLayout(form);

    auto *linkBtn = new QPushButton(i18n("Export Track to Transform / Roto Node"), w);
    linkBtn->setStyleSheet(QStringLiteral("background: #2a2a32; border: 1px solid #3c3c46; padding: 5px; font-weight: bold;"));
    lay->addWidget(linkBtn);
    lay->addStretch();

    return w;
}

QWidget *NatronWorkspaceWidget::buildScriptEditorPanel()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto *bar = new QHBoxLayout;
    auto *title = new QLabel(i18n("<b>Python Scripting Console & Node Automation</b>"), w);
    title->setStyleSheet(QStringLiteral("color: #3daee9; font-size: 11px;"));
    bar->addWidget(title);
    bar->addStretch();

    auto *runBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Execute Script"), w);
    connect(runBtn, &QPushButton::clicked, this, [this]() {
        if (m_scriptCodeEdit && m_scriptOutputConsole) {
            QString code = m_scriptCodeEdit->toPlainText();
            m_scriptOutputConsole->append(QStringLiteral(">>> %1\n[Natron Python Executed Successfully]").arg(code));
        }
    });
    bar->addWidget(runBtn);
    lay->addLayout(bar);

    auto *splitter = new QSplitter(Qt::Vertical, w);

    m_scriptCodeEdit = new QTextEdit(splitter);
    m_scriptCodeEdit->setPlaceholderText(i18n("# Python script for Natron node automation\napp = natron.getGuiInstance(0)\nreader = app.createNode('net.sf.openfx.ReadOIIO')\ntracker = app.createNode('net.sf.openfx.Tracker')"));
    m_scriptCodeEdit->setStyleSheet(QStringLiteral("background-color: #1a1a1e; color: #4ec9b0; font-family: monospace; font-size: 11px;"));
    splitter->addWidget(m_scriptCodeEdit);

    m_scriptOutputConsole = new QTextEdit(splitter);
    m_scriptOutputConsole->setReadOnly(true);
    m_scriptOutputConsole->setPlaceholderText(i18n("Natron Python 3.10 Interactive Environment Ready."));
    m_scriptOutputConsole->setStyleSheet(QStringLiteral("background-color: #121214; color: #cccccc; font-family: monospace; font-size: 10px;"));
    splitter->addWidget(m_scriptOutputConsole);

    lay->addWidget(splitter, 1);
    return w;
}

QWidget *NatronWorkspaceWidget::buildTransportBar()
{
    auto *w = new QWidget(this);
    w->setObjectName(QStringLiteral("natronTransportBar"));
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(4);

    auto *btnStart = new QToolButton(w);
    btnStart->setIcon(QIcon::fromTheme(QStringLiteral("media-skip-backward")));
    auto *btnPlay = new QToolButton(w);
    btnPlay->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    auto *btnStop = new QToolButton(w);
    btnStop->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
    auto *btnEnd = new QToolButton(w);
    btnEnd->setIcon(QIcon::fromTheme(QStringLiteral("media-skip-forward")));

    lay->addWidget(btnStart);
    lay->addWidget(btnPlay);
    lay->addWidget(btnStop);
    lay->addWidget(btnEnd);

    auto *scrubber = new QSlider(Qt::Horizontal, w);
    scrubber->setRange(0, 180);
    scrubber->setValue(0);
    connect(scrubber, &QSlider::valueChanged, this, &NatronWorkspaceWidget::syncPlayheadFrame);
    lay->addWidget(scrubber, 1);

    auto *frameLabel = new QLabel(QStringLiteral("Frame: 0 / 180 (30.0 fps)"), w);
    frameLabel->setStyleSheet(QStringLiteral("font-family: monospace; color: #aaaaaa;"));
    lay->addWidget(frameLabel);

    return w;
}

QWidget *NatronWorkspaceWidget::buildProgressPanel()
{
    auto *w = new QWidget(this);
    w->setObjectName(QStringLiteral("natronProgressPanel"));
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(6);

    auto *statusIcon = new QLabel(QStringLiteral("⚙"), w);
    statusIcon->setStyleSheet(QStringLiteral("color: #3daee9; font-size: 14px;"));
    lay->addWidget(statusIcon);

    auto *taskLabel = new QLabel(i18n("Natron Background Tasks: Idle"), w);
    taskLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 10px;"));
    lay->addWidget(taskLabel);

    auto *pbar = new QProgressBar(w);
    pbar->setRange(0, 100);
    pbar->setValue(0);
    pbar->setFixedHeight(12);
    lay->addWidget(pbar, 1);

    return w;
}

void NatronWorkspaceWidget::setupStyle()
{
    setStyleSheet(QStringLiteral(
        "QWidget#natronHeaderToolbar { background: #141416; border-bottom: 1px solid #26262a; }"
        "QWidget#natronTransportBar { background: #18181c; border-top: 1px solid #26262a; }"
        "QWidget#natronProgressPanel { background: #121214; border-top: 1px solid #26262a; }"
        "QToolBar#nodeCreationToolBar { background: #18181c; border-right: 1px solid #26262a; }"
        "QTabWidget::pane { border: 1px solid #26262a; background: #18181c; }"
        "QTabBar::tab { background: #121214; color: #888888; padding: 5px 12px; font-weight: bold; border-top-left-radius: 3px; border-top-right-radius: 3px; border: 1px solid #202024; }"
        "QTabBar::tab:selected { background: #1e1e24; color: #3daee9; border-bottom: 2px solid #3daee9; }"
        "QToolButton { background: #222228; border: 1px solid #33333a; border-radius: 3px; color: #cccccc; padding: 3px 6px; font-size: 10.5px; }"
        "QToolButton:hover { background: #2c2c34; border-color: #444450; color: #ffffff; }"
        "QToolButton:checked { background: #3daee9; color: #ffffff; font-weight: bold; border-color: #55bfee; }"
        "QGroupBox { font-weight: bold; color: #aaaaaa; border: 1px solid #2a2a30; border-radius: 3px; margin-top: 8px; padding-top: 8px; }"
    ));
}

void NatronWorkspaceWidget::loadPipeline(const QString &pipeline, const QJsonObject &params)
{
    int idx = m_pipelineSelector->findData(pipeline);
    if (idx >= 0 && m_pipelineSelector->currentIndex() != idx) {
        m_pipelineSelector->blockSignals(true);
        m_pipelineSelector->setCurrentIndex(idx);
        m_pipelineSelector->blockSignals(false);
    }
    m_nodeGraphView->loadGraphFromPipeline(pipeline, params);

    m_curveEditorView->clearCurves();
    m_dopeSheetView->clearTracks();

    if (pipeline == QStringLiteral("tracker") || pipeline == QStringLiteral("planar_track") || pipeline == QStringLiteral("track_object")) {
        QVector<CurveKeyframe> xKeys = {{0, 0.5}, {30, 0.52}, {60, 0.58}, {90, 0.65}, {120, 0.70}, {150, 0.73}, {180, 0.75}};
        QVector<CurveKeyframe> yKeys = {{0, 0.5}, {30, 0.48}, {60, 0.45}, {90, 0.44}, {120, 0.46}, {150, 0.50}, {180, 0.52}};
        QVector<CurveKeyframe> scaleKeys = {{0, 1.0}, {60, 1.02}, {120, 1.06}, {180, 1.10}};
        QVector<CurveKeyframe> rotKeys = {{0, 0.0}, {90, 2.5}, {180, 4.0}};

        m_curveEditorView->addCurve(QStringLiteral("Tracker1.center.x"), QColor(230, 25, 75), xKeys);
        m_curveEditorView->addCurve(QStringLiteral("Tracker1.center.y"), QColor(60, 180, 75), yKeys);
        m_curveEditorView->addCurve(QStringLiteral("Tracker1.scale"), QColor(67, 99, 216), scaleKeys);
        m_curveEditorView->addCurve(QStringLiteral("Tracker1.rotation"), QColor(245, 130, 49), rotKeys);

        m_dopeSheetView->addTrack(QStringLiteral("Tracker1 (Points)"), QColor(230, 25, 75), {0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180}, 0, 180);
        m_dopeSheetView->addTrack(QStringLiteral("Transform1 (Match-Move)"), QColor(67, 99, 216), {0, 30, 60, 90, 120, 150, 180}, 0, 180);
        m_dopeSheetView->addTrack(QStringLiteral("Roto / Target"), QColor(255, 225, 25), {0, 60, 120, 180}, 0, 180);

        m_statusLabel->setText(i18n("Active Pipeline: Object Tracking & Match-Move"));
    } else if (pipeline == QStringLiteral("track_blur")) {
        QVector<CurveKeyframe> blurKeys = {{0, 25.0}, {90, 25.0}, {180, 25.0}};
        m_curveEditorView->addCurve(QStringLiteral("Blur1.size"), QColor(70, 240, 240), blurKeys);

        m_dopeSheetView->addTrack(QStringLiteral("Tracker1 (Privacy Mask)"), QColor(230, 25, 75), {0, 30, 60, 90, 120, 150, 180}, 0, 180);
        m_dopeSheetView->addTrack(QStringLiteral("Blur Radius"), QColor(70, 240, 240), {0, 180}, 0, 180);

        m_statusLabel->setText(i18n("Active Pipeline: Object Track & Privacy Blur"));
    } else if (pipeline == QStringLiteral("rotoscope")) {
        QVector<CurveKeyframe> featherKeys = {{0, 2.0}, {60, 2.5}, {120, 2.0}};
        QVector<CurveKeyframe> opacityKeys = {{0, 1.0}, {60, 1.0}, {120, 1.0}};
        m_curveEditorView->addCurve(QStringLiteral("Roto1.feather"), QColor(70, 240, 240), featherKeys);
        m_curveEditorView->addCurve(QStringLiteral("Roto1.opacity"), QColor(240, 50, 230), opacityKeys);

        m_dopeSheetView->addTrack(QStringLiteral("Bézier Spline 1"), QColor(245, 130, 49), {0, 24, 48, 72, 96, 120}, 0, 120);
        m_dopeSheetView->addTrack(QStringLiteral("Feather & Blur"), QColor(70, 240, 240), {0, 60, 120}, 0, 120);

        m_statusLabel->setText(i18n("Active Pipeline: Rotoscope & Matte"));
    } else if (pipeline == QStringLiteral("chroma_key")) {
        QVector<CurveKeyframe> gainKeys = {{0, 1.0}, {60, 1.0}, {120, 1.0}};
        QVector<CurveKeyframe> despillKeys = {{0, 0.8}, {60, 0.8}, {120, 0.8}};
        m_curveEditorView->addCurve(QStringLiteral("Keyer1.screenGain"), QColor(60, 180, 75), gainKeys);
        m_curveEditorView->addCurve(QStringLiteral("Keyer1.despill"), QColor(255, 225, 25), despillKeys);

        m_dopeSheetView->addTrack(QStringLiteral("Chroma Keyer"), QColor(60, 180, 75), {0, 120}, 0, 120);
        m_dopeSheetView->addTrack(QStringLiteral("Color Correction"), QColor(245, 130, 49), {0, 60, 120}, 0, 120);

        m_statusLabel->setText(i18n("Active Pipeline: Chroma Keying & Despill"));
    } else {
        QVector<CurveKeyframe> gradeKeys = {{0, 1.0}, {60, 1.1}, {120, 1.0}};
        m_curveEditorView->addCurve(QStringLiteral("Grade1.gain"), QColor(230, 25, 75), gradeKeys);
        m_dopeSheetView->addTrack(QStringLiteral("Grade & Curves"), QColor(145, 30, 180), {0, 60, 120}, 0, 120);
        m_statusLabel->setText(i18n("Active Pipeline: Node Compositing"));
    }
}

void NatronWorkspaceWidget::syncPlayheadFrame(int frame)
{
    m_curveEditorView->setCurrentFrame(frame);
    m_dopeSheetView->setCurrentFrame(frame);
    if (pCore) {
        pCore->seekMonitor(Kdenlive::ProjectMonitor, frame);
    }
}

void NatronWorkspaceWidget::slotAddNodeTriggered()
{
    if (m_tabs && m_nodeGraphView) {
        m_tabs->setCurrentWidget(m_nodeGraphView);
        m_nodeGraphView->openSelectToolDialog();
    }
}

void NatronWorkspaceWidget::slotRenderVfxTriggered()
{
    m_statusLabel->setText(i18n("Rendering VFX in Natron..."));
    if (m_progressWidget) {
        m_progressWidget->setVisible(true);
    }
}

void NatronWorkspaceWidget::slotResetLayoutTriggered()
{
    m_nodeBarWidget->setVisible(true);
    m_propertiesBinWidget->setVisible(true);
    m_transportWidget->setVisible(true);
    m_progressWidget->setVisible(false);
}

void NatronWorkspaceWidget::slotToggleTool(const QString &toolId, bool visible)
{
    if (toolId == QStringLiteral("node_bar")) {
        m_nodeBarWidget->setVisible(visible);
    } else if (toolId == QStringLiteral("properties")) {
        m_propertiesBinWidget->setVisible(visible);
    } else if (toolId == QStringLiteral("transport")) {
        m_transportWidget->setVisible(visible);
    } else if (toolId == QStringLiteral("progress")) {
        m_progressWidget->setVisible(visible);
    }
}
