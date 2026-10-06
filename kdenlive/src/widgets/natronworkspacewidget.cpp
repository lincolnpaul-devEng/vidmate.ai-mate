/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Master Natron Compositing Workspace Widget Implementation — Houses the native,
 * authentic Natron GUI (TimeLine, NodeGraph, CurveEditor, DopeSheet, GL Viewport)
 * directly embedded in Kdenlive with live bidirectional IPC.
 */

#include "natronworkspacewidget.h"
#include "natronguihostwidget.h"
#include "natronselecttooldialog.h"
#include "natronscriptgenerator.h"
#include "core.h"
#include "mainwindow.h"

#include <KLocalizedString>
#include <QIcon>
#include <QMenu>
#include <QCursor>

NatronWorkspaceWidget::NatronWorkspaceWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setupStyle();
}

void NatronWorkspaceWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Top View Header Toolbar (Presets, Select Tool, Render, Timeline Switcher)
    mainLayout->addWidget(buildViewHeaderToolbar());

    // 2. Main Live Authentic Natron Host Widget (Embeds genuine Natron Window)
    m_natronHostWidget = new NatronGuiHostWidget(this);
    mainLayout->addWidget(m_natronHostWidget, 1);

    // Connect frame sync
    connect(m_natronHostWidget, &NatronGuiHostWidget::frameChangedInNatron, this, &NatronWorkspaceWidget::syncPlayheadFrame);
    connect(m_natronHostWidget, &NatronGuiHostWidget::renderFinished, this, [this](const QString &outputPath) {
        m_statusLabel->setText(i18n("Natron Render complete: %1", outputPath));
        if (m_progressBar) m_progressBar->setVisible(false);
    });
    connect(m_natronHostWidget, &NatronGuiHostWidget::statusMessageChanged, this, [this](const QString &msg) {
        m_statusLabel->setText(msg);
    });
}

QWidget *NatronWorkspaceWidget::buildViewHeaderToolbar()
{
    auto *topBar = new QWidget(this);
    topBar->setObjectName(QStringLiteral("natronHeaderToolbar"));
    auto *hLay = new QHBoxLayout(topBar);
    hLay->setContentsMargins(6, 4, 6, 4);
    hLay->setSpacing(8);

    auto *iconLabel = new QLabel(topBar);
    iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("view-diagram")).pixmap(18, 18));
    hLay->addWidget(iconLabel);

    auto *titleLabel = new QLabel(i18n("<b>Natron VFX Studio (Native Engine)</b>"), topBar);
    titleLabel->setStyleSheet(QStringLiteral("color: #3daee9; font-size: 11px;"));
    hLay->addWidget(titleLabel);

    hLay->addSpacing(12);

    // Preset VFX Pipeline Selector
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

    // Select Tool Popup Button
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

    // Render VFX Button
    m_renderBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-record")), i18n("Render VFX"), topBar);
    m_renderBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #3daee9; color: white; font-weight: bold; border-radius: 3px; padding: 4px 10px; } QPushButton:hover { background-color: #4dbff9; }"));
    connect(m_renderBtn, &QPushButton::clicked, this, &NatronWorkspaceWidget::slotRenderVfxTriggered);
    hLay->addWidget(m_renderBtn);

    // Timeline Switcher (F10)
    auto *timelineBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18n("Timeline (F10)"), topBar);
    timelineBtn->setToolTip(i18n("Switch back to Kdenlive Multi-Track Timeline (F10)"));
    timelineBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #2e3440; color: #eceff4; font-weight: bold; border-radius: 3px; padding: 4px 8px; border: 1px solid #4c566a; } QPushButton:hover { background-color: #3b4252; }"));
    connect(timelineBtn, &QPushButton::clicked, this, []() {
        if (pCore && pCore->window()) {
            auto *mw = static_cast<MainWindow *>(pCore->window());
            mw->slotToggleNatronTimeline();
        }
    });
    hLay->addWidget(timelineBtn);

    hLay->addStretch();

    m_statusLabel = new QLabel(i18n("Ready"), topBar);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 11px;"));
    hLay->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(topBar);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFixedWidth(100);
    m_progressBar->setFixedHeight(12);
    m_progressBar->setVisible(false);
    hLay->addWidget(m_progressBar);

    auto *restartBtn = new QToolButton(topBar);
    restartBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    restartBtn->setToolTip(i18n("Restart Natron GUI Engine"));
    connect(restartBtn, &QToolButton::clicked, this, &NatronWorkspaceWidget::slotRestartEngine);
    hLay->addWidget(restartBtn);

    return topBar;
}

void NatronWorkspaceWidget::setupStyle()
{
    setStyleSheet(QStringLiteral(
        "QWidget#natronHeaderToolbar { background: #141416; border-bottom: 1px solid #26262a; }"
        "QToolButton { background: #222228; border: 1px solid #33333a; border-radius: 3px; color: #cccccc; padding: 3px 6px; font-size: 10.5px; }"
        "QToolButton:hover { background: #2c2c34; border-color: #444450; color: #ffffff; }"
        "QToolButton:checked { background: #3daee9; color: #ffffff; font-weight: bold; border-color: #55bfee; }"
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

    if (!m_natronHostWidget) return;

    m_natronHostWidget->loadPresetPipeline(pipeline, params);
    m_statusLabel->setText(i18n("Loaded Pipeline: %1", pipeline));
}

void NatronWorkspaceWidget::syncPlayheadFrame(int frame)
{
    if (m_natronHostWidget) {
        m_natronHostWidget->seekFrame(frame);
    }
    if (pCore) {
        pCore->seekMonitor(Kdenlive::ProjectMonitor, frame);
    }
}

void NatronWorkspaceWidget::slotAddNodeTriggered()
{
    NatronToolEntry tool;
    QPoint globalPos = QCursor::pos();
    if (NatronSelectToolDialog::selectTool(this, tool, globalPos)) {
        if (m_natronHostWidget) {
            m_natronHostWidget->createNode(tool.id);
            m_statusLabel->setText(i18n("Created Natron node: %1", tool.name));
        }
    }
}

void NatronWorkspaceWidget::slotRenderVfxTriggered()
{
    m_statusLabel->setText(i18n("Rendering VFX in Natron..."));
    if (m_progressBar) {
        m_progressBar->setVisible(true);
        m_progressBar->setValue(0);
    }
    if (m_natronHostWidget) {
        m_natronHostWidget->renderVfxSequence(QStringLiteral("output.mov"));
    }
}

void NatronWorkspaceWidget::slotRestartEngine()
{
    if (m_natronHostWidget) {
        QString pipeline = m_pipelineSelector ? m_pipelineSelector->currentData().toString() : QStringLiteral("tracker");
        m_natronHostWidget->loadPresetPipeline(pipeline);
        m_statusLabel->setText(i18n("Natron GUI Engine ready"));
    }
}
