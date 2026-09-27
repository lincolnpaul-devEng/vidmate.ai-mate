/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Master Natron Compositing Workspace Widget — Houses the Node Graph Canvas,
 * Curve Editor, Dope Sheet, and Timeline Scrubber with tab switching.
 */

#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
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

private:
    void setupUi();

    QTabWidget *m_tabs{nullptr};
    NatronNodeGraphView *m_nodeGraphView{nullptr};
    NatronCurveEditorView *m_curveEditorView{nullptr};
    NatronDopeSheetView *m_dopeSheetView{nullptr};

    QComboBox *m_pipelineSelector{nullptr};
    QPushButton *m_renderBtn{nullptr};
    QPushButton *m_addNodeBtn{nullptr};
    QLabel *m_statusLabel{nullptr};
};
