/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Magic Mask Inspector Widget — DaVinci Resolve-style AI Subject Masking &
 * Background Removal inspector panel for Kdenlive.
 */

#pragma once

#include "definitions.h"
#include <QWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QToolButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QLineEdit>
#include <QDir>
#include <memory>

class AutomaskHelper;
class MaskManager;
class ProjectClip;

class MagicMaskWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MagicMaskWidget(QWidget *parent = nullptr);
    ~MagicMaskWidget() override;

    void setOwner(const ObjectId &owner);
    void setMaskManager(MaskManager *maskManager);
    void updateStatus(bool active, const QString &statusText = QString());

public Q_SLOTS:
    void slotLaunchTrackingForward();
    void slotLaunchTrackingBackward();
    void slotLaunchTrackingBidirectional();
    void slotStopTracking();
    void slotTrackToStart();
    void slotTrackToEnd();
    void slotGoToStart();
    void slotGoToReference();
    void slotGoToEnd();
    void slotClearCurrentStrokes();
    void slotClearRangeStrokes();
    void slotClearAllStrokes();
    void slotRegenerateCache();
    void slotClearCache();
    void slotApplyMask();
    void slotRunRvmHumanMatting();
    void slotUpdateProgress(int progress);
    void slotCheckModelStatus();

Q_SIGNALS:
    void trackingRequested(bool forward, bool bidirectional);
    void abortRequested();
    void maskApplied(const ObjectId &owner);

private:
    void buildUI();
    void setupStyle();
    QWidget *buildHeader();
    QWidget *buildTrackingTab();
    QWidget *buildMatteTab();
    QWidget *buildSettingsTab();
    std::shared_ptr<ProjectClip> getOwnerClip();

    ObjectId m_owner{KdenliveObjectType::NoItem, {}};
    MaskManager *m_maskManager{nullptr};
    AutomaskHelper *m_maskHelper{nullptr};

    // Header widgets
    QLabel *m_statusLed{nullptr};
    QLabel *m_titleLabel{nullptr};
    QToolButton *m_presetBtn{nullptr};
    QToolButton *m_resetBtn{nullptr};
    QToolButton *m_pinBtn{nullptr};
    QToolButton *m_lockBtn{nullptr};

    // Tab widget
    QTabWidget *m_tabWidget{nullptr};

    // Tracking Tab widgets
    QToolButton *m_btnTrackStart{nullptr};
    QToolButton *m_btnTrackBack{nullptr};
    QToolButton *m_btnTrackStop{nullptr};
    QToolButton *m_btnTrackBidi{nullptr};
    QToolButton *m_btnTrackFwd{nullptr};
    QToolButton *m_btnTrackEnd{nullptr};

    QToolButton *m_btnGoStart{nullptr};
    QToolButton *m_btnGoRef{nullptr};
    QToolButton *m_btnGoEnd{nullptr};

    QPushButton *m_strokeAddBtn{nullptr};
    QPushButton *m_strokeSubBtn{nullptr};
    QPushButton *m_strokeSelectBtn{nullptr};
    QPushButton *m_strokeDeleteBtn{nullptr};
    QButtonGroup *m_strokeModeGroup{nullptr};

    QPushButton *m_clearCurrentBtn{nullptr};
    QPushButton *m_clearRangeBtn{nullptr};
    QPushButton *m_clearAllBtn{nullptr};

    QPushButton *m_regenCacheBtn{nullptr};
    QPushButton *m_clearCacheBtn{nullptr};

    QSlider *m_refTimeSlider{nullptr};
    QLabel *m_refTimeLabel{nullptr};
    QLabel *m_processedRangeLabel{nullptr};
    QDoubleSpinBox *m_rangeStartSpin{nullptr};
    QDoubleSpinBox *m_rangeEndSpin{nullptr};

    QPushButton *m_modeFasterBtn{nullptr};
    QPushButton *m_modeBetterBtn{nullptr};
    QButtonGroup *m_qualityGroup{nullptr};

    QCheckBox *m_postMultiplyCheck{nullptr};
    QCheckBox *m_autoApplyCheck{nullptr};

    QPushButton *m_generateBtn{nullptr};
    QProgressBar *m_progressBar{nullptr};
    QLabel *m_statusMsgLabel{nullptr};

    // Matte Tab widgets
    QCheckBox *m_invertMaskCheck{nullptr};
    QSlider *m_radiusSlider{nullptr};
    QSpinBox *m_radiusSpin{nullptr};
    QSlider *m_featherSlider{nullptr};
    QSpinBox *m_featherSpin{nullptr};
    QSlider *m_cleanBlackSlider{nullptr};
    QDoubleSpinBox *m_cleanBlackSpin{nullptr};
    QSlider *m_cleanWhiteSlider{nullptr};
    QDoubleSpinBox *m_cleanWhiteSpin{nullptr};
    QSlider *m_blurSlider{nullptr};
    QDoubleSpinBox *m_blurSpin{nullptr};
    QSlider *m_despillSlider{nullptr};
    QSpinBox *m_despillSpin{nullptr};
    QComboBox *m_previewModeCombo{nullptr};

    // Settings Tab widgets
    QComboBox *m_modelCombo{nullptr};
    QComboBox *m_deviceCombo{nullptr};
    QCheckBox *m_offloadCpuCheck{nullptr};
    QLabel *m_cacheDirLabel{nullptr};
    QPushButton *m_openCacheBtn{nullptr};
    QLabel *m_backendStatusLabel{nullptr};
    QPushButton *m_configBackendBtn{nullptr};
};
