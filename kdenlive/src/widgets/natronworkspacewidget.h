/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Master Natron Compositing Workspace Widget — Houses all Natron editing tool
 * widgets (Node Graph, Curve Editor, Dope Sheet, Properties Bin, GL Viewer,
 * Roto/Paint Panel, Tracker Panel, Python Script Editor, Node Toolbar, Transport,
 * and Progress Panel) with View Header toggle buttons.
 */

#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QSplitter>
#include <QScrollArea>
#include <QProgressBar>
#include <QTextEdit>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

class NatronNodeGraphView;
class NatronCurveEditorView;
class NatronDopeSheetView;

/**
 * @class NatronWorkspaceWidget
 * @brief Unified Natron VFX Compositor Workspace embedded into Kdenlive
 */
class NatronWorkspaceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NatronWorkspaceWidget(QWidget *parent = nullptr);
    ~NatronWorkspaceWidget() override = default;

    NatronNodeGraphView *nodeGraph() const { return m_nodeGraphView; }
    NatronCurveEditorView *curveEditor() const { return m_curveEditorView; }
    NatronDopeSheetView *dopeSheet() const { return m_dopeSheetView; }

    void loadPipeline(const QString &pipeline, const QJsonObject &params);
    void syncPlayheadFrame(int frame);

public Q_SLOTS:
    void slotAddNodeTriggered();
    void slotRenderVfxTriggered();
    void slotResetLayoutTriggered();
    void slotToggleTool(const QString &toolId, bool visible);

private:
    void setupUi();
    void setupStyle();
    QWidget *buildViewHeaderToolbar();
    QWidget *buildNodeCreationToolBar();
    QWidget *buildPropertiesBinPanel();
    QWidget *buildViewerPanel();
    QWidget *buildRotoPanel();
    QWidget *buildTrackerPanel();
    QWidget *buildScriptEditorPanel();
    QWidget *buildTransportBar();
    QWidget *buildProgressPanel();

    // Core central tabs & views
    QTabWidget *m_tabs{nullptr};
    NatronNodeGraphView *m_nodeGraphView{nullptr};
    NatronCurveEditorView *m_curveEditorView{nullptr};
    NatronDopeSheetView *m_dopeSheetView{nullptr};
    QWidget *m_viewerPanel{nullptr};
    QWidget *m_rotoPanel{nullptr};
    QWidget *m_trackerPanel{nullptr};
    QWidget *m_scriptEditorPanel{nullptr};

    // Panels & Splitters
    QSplitter *m_mainSplitter{nullptr};
    QWidget *m_nodeBarWidget{nullptr};
    QWidget *m_propertiesBinWidget{nullptr};
    QWidget *m_transportWidget{nullptr};
    QWidget *m_progressWidget{nullptr};

    // Header controls & buttons
    QComboBox *m_pipelineSelector{nullptr};
    QPushButton *m_renderBtn{nullptr};
    QPushButton *m_addNodeBtn{nullptr};
    QLabel *m_statusLabel{nullptr};

    // View Header Tool Buttons
    QToolButton *m_btnToggleGraph{nullptr};
    QToolButton *m_btnToggleCurves{nullptr};
    QToolButton *m_btnToggleDopeSheet{nullptr};
    QToolButton *m_btnToggleProperties{nullptr};
    QToolButton *m_btnToggleViewer{nullptr};
    QToolButton *m_btnToggleRoto{nullptr};
    QToolButton *m_btnToggleTracker{nullptr};
    QToolButton *m_btnToggleScript{nullptr};
    QToolButton *m_btnToggleNodeBar{nullptr};
    QToolButton *m_btnToggleTransport{nullptr};
    QToolButton *m_btnToggleProgress{nullptr};

    // Python script editor widgets
    QTextEdit *m_scriptCodeEdit{nullptr};
    QTextEdit *m_scriptOutputConsole{nullptr};
};
