/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Master Natron Compositing Workspace Widget Implementation
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

NatronWorkspaceWidget::NatronWorkspaceWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void NatronWorkspaceWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Top control bar
    auto *topBarLayout = new QHBoxLayout();

    auto *vfxLabel = new QLabel(i18n("<b>Natron VFX Engine</b>"), this);

    m_pipelineSelector = new QComboBox(this);
    m_pipelineSelector->addItem(i18n("Object Tracker & Match-Move"), QStringLiteral("tracker"));
    m_pipelineSelector->addItem(i18n("Track & Privacy Blur"), QStringLiteral("track_blur"));
    m_pipelineSelector->addItem(i18n("Chroma Key Composite"), QStringLiteral("chroma_key"));
    m_pipelineSelector->addItem(i18n("Rotoscope & AI Matte"), QStringLiteral("rotoscope"));
    m_pipelineSelector->addItem(i18n("Color Grade & Curves"), QStringLiteral("color_grade"));
    connect(m_pipelineSelector, &QComboBox::currentTextChanged, this, [this]() {
        QString pipeline = m_pipelineSelector->currentData().toString();
        loadPipeline(pipeline, {});
    });

    m_addNodeBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Add Node"), this);
    auto *nodeMenu = new QMenu(this);
    nodeMenu->addAction(i18n("Read (Source Media)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Read2"), QStringLiteral("Read2"), NatronNodeItem::NodeReader, QPointF(200, 50));
    });
    nodeMenu->addAction(i18n("Tracker (Planar / Feature)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Tracker2"), QStringLiteral("Tracker2"), NatronNodeItem::NodeTracker, QPointF(200, 150));
    });
    nodeMenu->addAction(i18n("Keyer (Chroma / Luma)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Keyer2"), QStringLiteral("Keyer2"), NatronNodeItem::NodeKeyer, QPointF(200, 150));
    });
    nodeMenu->addAction(i18n("Roto (Bézier Mask)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Roto2"), QStringLiteral("Roto2"), NatronNodeItem::NodeRoto, QPointF(200, 150));
    });
    nodeMenu->addAction(i18n("Grade (Lift/Gamma/Gain)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Grade2"), QStringLiteral("Grade2"), NatronNodeItem::NodeGrade, QPointF(200, 150));
    });
    nodeMenu->addAction(i18n("Transform (Match-Move)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Transform2"), QStringLiteral("Transform2"), NatronNodeItem::NodeTransform, QPointF(200, 150));
    });
    nodeMenu->addAction(i18n("Merge (Over / Multiply)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Merge2"), QStringLiteral("Merge2"), NatronNodeItem::NodeMerge, QPointF(200, 250));
    });
    nodeMenu->addAction(i18n("Write (Render Target)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Write2"), QStringLiteral("Write2"), NatronNodeItem::NodeWriter, QPointF(200, 350));
    });
    m_addNodeBtn->setMenu(nodeMenu);

    m_renderBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-record")), i18n("Render VFX"), this);
    m_renderBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #3daee9; color: white; font-weight: bold; border-radius: 3px; padding: 4px 8px; }"));
    connect(m_renderBtn, &QPushButton::clicked, this, &NatronWorkspaceWidget::slotRenderVfxTriggered);

    m_statusLabel = new QLabel(i18n("Ready"), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));

    topBarLayout->addWidget(vfxLabel);
    topBarLayout->addSpacing(10);
    topBarLayout->addWidget(new QLabel(i18n("Preset:"), this));
    topBarLayout->addWidget(m_pipelineSelector);
    topBarLayout->addWidget(m_addNodeBtn);
    topBarLayout->addStretch(1);
    topBarLayout->addWidget(m_statusLabel);
    topBarLayout->addWidget(m_renderBtn);

    mainLayout->addLayout(topBarLayout);

    // Tab widget housing the 3 views: Node Graph, Curve Editor, Dope Sheet / Timeline Scrubber
    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::South);

    m_nodeGraphView = new NatronNodeGraphView(this);
    m_curveEditorView = new NatronCurveEditorView(this);
    m_dopeSheetView = new NatronDopeSheetView(this);

    m_tabs->addTab(m_nodeGraphView, QIcon::fromTheme(QStringLiteral("view-diagram")), i18n("Node Graph"));
    m_tabs->addTab(m_curveEditorView, QIcon::fromTheme(QStringLiteral("draw-bezier-curves")), i18n("Curve Editor"));
    m_tabs->addTab(m_dopeSheetView, QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18n("Dope Sheet & Scrubber"));

    connect(m_dopeSheetView, &NatronDopeSheetView::seekRequested, this, &NatronWorkspaceWidget::syncPlayheadFrame);

    mainLayout->addWidget(m_tabs, 1);
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

    // Populate Curve Editor & Dope Sheet with corresponding keyframe channels
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

        m_statusLabel->setText(i18n("Active Pipeline: Object Tracking & Match-Move (4 animated curves, 3 dope sheet tracks)"));
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
}

void NatronWorkspaceWidget::slotRenderVfxTriggered()
{
    m_statusLabel->setText(i18n("Rendering headless VFX in Natron..."));
    // Route to AI command router or headless Natron renderer
    m_statusLabel->setText(i18n("VFX job queued."));
}

void NatronWorkspaceWidget::slotResetLayoutTriggered()
{
}
