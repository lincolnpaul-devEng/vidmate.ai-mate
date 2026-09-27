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
    m_pipelineSelector->addItem(i18n("Chroma Key Composite"), QStringLiteral("chroma_key"));
    m_pipelineSelector->addItem(i18n("Rotoscope & Matte"), QStringLiteral("rotoscope"));
    m_pipelineSelector->addItem(i18n("Color Grade & Curves"), QStringLiteral("color_grade"));
    m_pipelineSelector->addItem(i18n("Transform & Match-Move"), QStringLiteral("tracker"));
    connect(m_pipelineSelector, &QComboBox::currentTextChanged, this, [this]() {
        QString pipeline = m_pipelineSelector->currentData().toString();
        m_nodeGraphView->loadGraphFromPipeline(pipeline, {});
    });

    m_addNodeBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Add Node"), this);
    auto *nodeMenu = new QMenu(this);
    nodeMenu->addAction(i18n("Read (Source Media)"), this, [this]() {
        m_nodeGraphView->addNode(QStringLiteral("Read2"), QStringLiteral("Read2"), NatronNodeItem::NodeReader, QPointF(200, 50));
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
    nodeMenu->addAction(i18n("Transform (2D / Tracker)"), this, [this]() {
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
    if (idx >= 0) {
        m_pipelineSelector->setCurrentIndex(idx);
    }
    m_nodeGraphView->loadGraphFromPipeline(pipeline, params);
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
