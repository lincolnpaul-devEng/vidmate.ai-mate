/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Master Natron Compositing Workspace Widget — Houses the native, authentic
 * Natron GUI (TimeLine, NodeGraph, CurveEditor, DopeSheet, GL Viewport)
 * directly embedded in Kdenlive with live bidirectional IPC and preset routing.
 */

#pragma once

#include <QWidget>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>

class NatronGuiHostWidget;

/**
 * @class NatronWorkspaceWidget
 * @brief Unified Natron VFX Compositor Workspace embedding the genuine Natron GUI into Kdenlive
 */
class NatronWorkspaceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NatronWorkspaceWidget(QWidget *parent = nullptr);
    ~NatronWorkspaceWidget() override = default;

    NatronGuiHostWidget *natronHost() const { return m_natronHostWidget; }

    void loadPipeline(const QString &pipeline, const QJsonObject &params);
    void syncPlayheadFrame(int frame);

public Q_SLOTS:
    void slotAddNodeTriggered();
    void slotRenderVfxTriggered();
    void slotRestartEngine();

private:
    void setupUi();
    void setupStyle();
    QWidget *buildViewHeaderToolbar();

    // Native Natron Host Container
    NatronGuiHostWidget *m_natronHostWidget{nullptr};

    // Header controls & buttons
    QComboBox *m_pipelineSelector{nullptr};
    QPushButton *m_renderBtn{nullptr};
    QPushButton *m_addNodeBtn{nullptr};
    QLabel *m_statusLabel{nullptr};
    QProgressBar *m_progressBar{nullptr};
};
