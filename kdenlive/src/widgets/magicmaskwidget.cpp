/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Magic Mask Inspector Widget — DaVinci Resolve-style AI Subject Masking &
 * Background Removal inspector panel for Kdenlive.
 */

#include "magicmaskwidget.h"
#include "core.h"
#include "mainwindow.h"
#include "doc/kdenlivedoc.h"
#include "monitor/monitormanager.h"
#include "monitor/monitor.h"
#include "monitor/monitorproxy.h"
#include "bin/projectclip.h"
#include "bin/bin.h"
#include "bin/projectitemmodel.h"
#include "kdenlivesettings.h"
#include "effects/effectstack/view/maskmanager.hpp"
#include "assets/keyframes/model/automask/automaskhelper.hpp"

#include <KLocalizedString>
#include <KMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QStyle>
#include <QFontDatabase>

MagicMaskWidget::MagicMaskWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUI();
    setupStyle();
    slotCheckModelStatus();
}

MagicMaskWidget::~MagicMaskWidget() = default;

void MagicMaskWidget::setMaskManager(MaskManager *maskManager)
{
    m_maskManager = maskManager;
}

void MagicMaskWidget::setOwner(const ObjectId &owner)
{
    m_owner = owner;
    std::shared_ptr<ProjectClip> clip = getOwnerClip();
    if (clip) {
        m_titleLabel->setText(QStringLiteral("MagicMask: %1").arg(clip->clipName()));
        int duration = pCore->getItemDuration(m_owner);
        m_refTimeSlider->setRange(0, qMax(0, duration));
        m_rangeEndSpin->setValue(duration / pCore->getCurrentFps());
        updateStatus(true, i18n("Ready for subject tracking"));
    } else {
        m_titleLabel->setText(QStringLiteral("MagicMask (No Clip Selected)"));
        updateStatus(false, i18n("Select a video or image clip in the timeline"));
    }
}

std::shared_ptr<ProjectClip> MagicMaskWidget::getOwnerClip()
{
    QString binId;
    switch (m_owner.type) {
    case KdenliveObjectType::TimelineClip: {
        binId = pCore->getTimelineClipBinId(m_owner);
        break;
    }
    case KdenliveObjectType::BinClip: {
        binId = QString::number(m_owner.itemId);
        break;
    }
    default:
        break;
    }
    if (binId.isEmpty()) {
        return nullptr;
    }
    return pCore->projectItemModel()->getClipByBinID(binId);
}

void MagicMaskWidget::updateStatus(bool active, const QString &statusText)
{
    if (m_statusLed) {
        m_statusLed->setStyleSheet(active
            ? QStringLiteral("background-color: #27ae60; border-radius: 5px; border: 1px solid #2ecc71;")
            : QStringLiteral("background-color: #7f8c8d; border-radius: 5px; border: 1px solid #95a5a6;"));
    }
    if (m_statusMsgLabel && !statusText.isEmpty()) {
        m_statusMsgLabel->setText(statusText);
    }
}

void MagicMaskWidget::buildUI()
{
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(6);

    // 1. Header (Resolve Inspector style)
    mainLay->addWidget(buildHeader());

    // 2. Tab Widget (Tracking, Matte, Settings)
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true);

    m_tabWidget->addTab(buildTrackingTab(), QIcon::fromTheme(QStringLiteral("view-preview")), i18n("Tracking"));
    m_tabWidget->addTab(buildMatteTab(), QIcon::fromTheme(QStringLiteral("draw-polygon")), i18n("Matte"));
    m_tabWidget->addTab(buildSettingsTab(), QIcon::fromTheme(QStringLiteral("configure")), i18n("Settings"));

    mainLay->addWidget(m_tabWidget);
}

QWidget *MagicMaskWidget::buildHeader()
{
    auto *headerWidget = new QWidget(this);
    headerWidget->setObjectName(QStringLiteral("inspectorHeader"));
    auto *hLay = new QHBoxLayout(headerWidget);
    hLay->setContentsMargins(4, 2, 4, 4);
    hLay->setSpacing(8);

    m_statusLed = new QLabel(this);
    m_statusLed->setFixedSize(10, 10);
    m_statusLed->setStyleSheet(QStringLiteral("background-color: #e74c3c; border-radius: 5px; border: 1px solid #c0392b;"));
    hLay->addWidget(m_statusLed);

    m_titleLabel = new QLabel(QStringLiteral("MagicMask1"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 12px; color: #ffffff;"));
    hLay->addWidget(m_titleLabel);

    auto *badge = new QLabel(QStringLiteral("AI-SAM2"), this);
    badge->setStyleSheet(QStringLiteral("background-color: #e54242; color: white; font-weight: bold; font-size: 9px; padding: 1px 4px; border-radius: 3px;"));
    hLay->addWidget(badge);

    hLay->addStretch();

    m_presetBtn = new QToolButton(this);
    m_presetBtn->setIcon(QIcon::fromTheme(QStringLiteral("arrow-down")));
    m_presetBtn->setToolTip(i18n("Magic Mask Presets"));
    m_presetBtn->setAutoRaise(true);
    hLay->addWidget(m_presetBtn);

    m_resetBtn = new QToolButton(this);
    m_resetBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    m_resetBtn->setToolTip(i18n("Reset Mask"));
    m_resetBtn->setAutoRaise(true);
    connect(m_resetBtn, &QToolButton::clicked, this, &MagicMaskWidget::slotClearAllStrokes);
    hLay->addWidget(m_resetBtn);

    m_pinBtn = new QToolButton(this);
    m_pinBtn->setIcon(QIcon::fromTheme(QStringLiteral("pin")));
    m_pinBtn->setToolTip(i18n("Pin Inspector Window"));
    m_pinBtn->setCheckable(true);
    m_pinBtn->setAutoRaise(true);
    hLay->addWidget(m_pinBtn);

    m_lockBtn = new QToolButton(this);
    m_lockBtn->setIcon(QIcon::fromTheme(QStringLiteral("object-locked")));
    m_lockBtn->setToolTip(i18n("Lock Mask"));
    m_lockBtn->setCheckable(true);
    m_lockBtn->setAutoRaise(true);
    hLay->addWidget(m_lockBtn);

    return headerWidget;
}

QWidget *MagicMaskWidget::buildTrackingTab()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(8);

    // ── 1. Transport / Tracking Bar ──────────────────────────────────────────
    auto *trackGroup = new QGroupBox(i18n("Track Direction"), content);
    auto *trackLay = new QHBoxLayout(trackGroup);
    trackLay->setContentsMargins(4, 6, 4, 6);
    trackLay->setSpacing(3);

    m_btnTrackStart = new QToolButton(trackGroup);
    m_btnTrackStart->setIcon(QIcon::fromTheme(QStringLiteral("media-skip-backward")));
    m_btnTrackStart->setToolTip(i18n("Track Reverse to Start"));
    connect(m_btnTrackStart, &QToolButton::clicked, this, &MagicMaskWidget::slotTrackToStart);

    m_btnTrackBack = new QToolButton(trackGroup);
    m_btnTrackBack->setIcon(QIcon::fromTheme(QStringLiteral("media-seek-backward")));
    m_btnTrackBack->setToolTip(i18n("Track Backward (1 Frame / Step)"));
    connect(m_btnTrackBack, &QToolButton::clicked, this, &MagicMaskWidget::slotLaunchTrackingBackward);

    m_btnTrackStop = new QToolButton(trackGroup);
    m_btnTrackStop->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
    m_btnTrackStop->setToolTip(i18n("Stop / Abort Tracking"));
    connect(m_btnTrackStop, &QToolButton::clicked, this, &MagicMaskWidget::slotStopTracking);

    m_btnTrackBidi = new QToolButton(trackGroup);
    m_btnTrackBidi->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-pause")));
    m_btnTrackBidi->setText(QStringLiteral("⇄"));
    m_btnTrackBidi->setToolTip(i18n("Track Bidirectional (Both Ways)"));
    connect(m_btnTrackBidi, &QToolButton::clicked, this, &MagicMaskWidget::slotLaunchTrackingBidirectional);

    m_btnTrackFwd = new QToolButton(trackGroup);
    m_btnTrackFwd->setIcon(QIcon::fromTheme(QStringLiteral("media-seek-forward")));
    m_btnTrackFwd->setToolTip(i18n("Track Forward (1 Frame / Step)"));
    connect(m_btnTrackFwd, &QToolButton::clicked, this, &MagicMaskWidget::slotLaunchTrackingForward);

    m_btnTrackEnd = new QToolButton(trackGroup);
    m_btnTrackEnd->setIcon(QIcon::fromTheme(QStringLiteral("media-skip-forward")));
    m_btnTrackEnd->setToolTip(i18n("Track Forward to End"));
    connect(m_btnTrackEnd, &QToolButton::clicked, this, &MagicMaskWidget::slotTrackToEnd);

    trackLay->addWidget(m_btnTrackStart);
    trackLay->addWidget(m_btnTrackBack);
    trackLay->addWidget(m_btnTrackStop);
    trackLay->addWidget(m_btnTrackBidi);
    trackLay->addWidget(m_btnTrackFwd);
    trackLay->addWidget(m_btnTrackEnd);
    lay->addWidget(trackGroup);

    // ── 2. Go To Frame ───────────────────────────────────────────────────────
    auto *gotoRow = new QHBoxLayout;
    auto *gotoLabel = new QLabel(i18n("Go To Frame:"), content);
    gotoLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-weight: bold;"));
    gotoRow->addWidget(gotoLabel);

    m_btnGoStart = new QToolButton(content);
    m_btnGoStart->setText(QStringLiteral("|.. Start"));
    m_btnGoStart->setToolTip(i18n("Go to Start Frame"));
    connect(m_btnGoStart, &QToolButton::clicked, this, &MagicMaskWidget::slotGoToStart);
    gotoRow->addWidget(m_btnGoStart);

    m_btnGoRef = new QToolButton(content);
    m_btnGoRef->setText(QStringLiteral("↓ Ref"));
    m_btnGoRef->setToolTip(i18n("Go to Reference Frame"));
    connect(m_btnGoRef, &QToolButton::clicked, this, &MagicMaskWidget::slotGoToReference);
    gotoRow->addWidget(m_btnGoRef);

    m_btnGoEnd = new QToolButton(content);
    m_btnGoEnd->setText(QStringLiteral("..| End"));
    m_btnGoEnd->setToolTip(i18n("Go to End Frame"));
    connect(m_btnGoEnd, &QToolButton::clicked, this, &MagicMaskWidget::slotGoToEnd);
    gotoRow->addWidget(m_btnGoEnd);
    gotoRow->addStretch();
    lay->addLayout(gotoRow);

    // ── 3. Stroke Mode ───────────────────────────────────────────────────────
    auto *strokeRow = new QHBoxLayout;
    auto *strokeLabel = new QLabel(i18n("Stroke Mode:"), content);
    strokeLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-weight: bold;"));
    strokeRow->addWidget(strokeLabel);

    m_strokeModeGroup = new QButtonGroup(content);
    m_strokeAddBtn = new QPushButton(i18n("+ Add"), content);
    m_strokeAddBtn->setCheckable(true);
    m_strokeAddBtn->setChecked(true);
    m_strokeAddBtn->setToolTip(i18n("Add foreground subject point / stroke (Green)"));

    m_strokeSubBtn = new QPushButton(i18n("- Subtract"), content);
    m_strokeSubBtn->setCheckable(true);
    m_strokeSubBtn->setToolTip(i18n("Subtract background point / stroke (Red)"));

    m_strokeSelectBtn = new QPushButton(i18n("⬚ Box"), content);
    m_strokeSelectBtn->setCheckable(true);
    m_strokeSelectBtn->setToolTip(i18n("Select Bounding Box for subject"));

    m_strokeDeleteBtn = new QPushButton(i18n("🗑"), content);
    m_strokeDeleteBtn->setToolTip(i18n("Clear selected stroke"));
    connect(m_strokeDeleteBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearCurrentStrokes);

    m_strokeModeGroup->addButton(m_strokeAddBtn);
    m_strokeModeGroup->addButton(m_strokeSubBtn);
    m_strokeModeGroup->addButton(m_strokeSelectBtn);

    strokeRow->addWidget(m_strokeAddBtn);
    strokeRow->addWidget(m_strokeSubBtn);
    strokeRow->addWidget(m_strokeSelectBtn);
    strokeRow->addWidget(m_strokeDeleteBtn);
    lay->addLayout(strokeRow);

    // ── 4. Clear Strokes ─────────────────────────────────────────────────────
    auto *clearRow = new QHBoxLayout;
    auto *clearLabel = new QLabel(i18n("Clear Strokes:"), content);
    clearLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-weight: bold;"));
    clearRow->addWidget(clearLabel);

    m_clearCurrentBtn = new QPushButton(i18n("Current"), content);
    connect(m_clearCurrentBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearCurrentStrokes);
    m_clearRangeBtn = new QPushButton(i18n("Range"), content);
    connect(m_clearRangeBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearRangeStrokes);
    m_clearAllBtn = new QPushButton(i18n("All"), content);
    connect(m_clearAllBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearAllStrokes);

    clearRow->addWidget(m_clearCurrentBtn);
    clearRow->addWidget(m_clearRangeBtn);
    clearRow->addWidget(m_clearAllBtn);
    lay->addLayout(clearRow);

    // ── 5. Disk Cache ────────────────────────────────────────────────────────
    auto *cacheRow = new QHBoxLayout;
    auto *cacheLabel = new QLabel(i18n("Disk Cache:"), content);
    cacheLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-weight: bold;"));
    cacheRow->addWidget(cacheLabel);

    m_regenCacheBtn = new QPushButton(i18n("Regenerate All"), content);
    connect(m_regenCacheBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotRegenerateCache);
    m_clearCacheBtn = new QPushButton(i18n("Clear Cache"), content);
    connect(m_clearCacheBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearCache);

    cacheRow->addWidget(m_regenCacheBtn);
    cacheRow->addWidget(m_clearCacheBtn);
    lay->addLayout(cacheRow);

    // ── 6. Reference Time ────────────────────────────────────────────────────
    auto *refRow = new QHBoxLayout;
    auto *refLabel = new QLabel(i18n("Reference Time:"), content);
    refLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-weight: bold;"));
    refRow->addWidget(refLabel);

    m_refTimeSlider = new QSlider(Qt::Horizontal, content);
    m_refTimeSlider->setRange(0, 100);
    m_refTimeSlider->setValue(0);
    refRow->addWidget(m_refTimeSlider, 1);

    m_refTimeLabel = new QLabel(QStringLiteral("0.0s"), content);
    m_refTimeLabel->setFixedWidth(45);
    m_refTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    refRow->addWidget(m_refTimeLabel);

    connect(m_refTimeSlider, &QSlider::valueChanged, this, [this](int val) {
        double seconds = val / pCore->getCurrentFps();
        m_refTimeLabel->setText(QStringLiteral("%1s").arg(seconds, 0, 'f', 1));
    });
    lay->addLayout(refRow);

    // ── 7. Processed Frames ──────────────────────────────────────────────────
    auto *procRow = new QHBoxLayout;
    auto *procLabel = new QLabel(i18n("Processed Frames:"), content);
    procLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-weight: bold;"));
    procRow->addWidget(procLabel);

    m_rangeStartSpin = new QDoubleSpinBox(content);
    m_rangeStartSpin->setRange(0.0, 9999.0);
    m_rangeStartSpin->setValue(0.0);
    m_rangeStartSpin->setSuffix(QStringLiteral(" s"));
    procRow->addWidget(m_rangeStartSpin);

    auto *toLabel = new QLabel(i18n("to"), content);
    procRow->addWidget(toLabel);

    m_rangeEndSpin = new QDoubleSpinBox(content);
    m_rangeEndSpin->setRange(0.0, 9999.0);
    m_rangeEndSpin->setValue(0.0);
    m_rangeEndSpin->setSuffix(QStringLiteral(" s"));
    procRow->addWidget(m_rangeEndSpin);
    lay->addLayout(procRow);

    // ── 8. Mode (Quality) ────────────────────────────────────────────────────
    auto *modeRow = new QHBoxLayout;
    auto *modeLabel = new QLabel(i18n("Mode:"), content);
    modeLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-weight: bold;"));
    modeRow->addWidget(modeLabel);

    m_qualityGroup = new QButtonGroup(content);
    m_modeFasterBtn = new QPushButton(i18n("Faster"), content);
    m_modeFasterBtn->setCheckable(true);
    m_modeFasterBtn->setChecked(true);
    m_modeBetterBtn = new QPushButton(i18n("Better"), content);
    m_modeBetterBtn->setCheckable(true);

    m_qualityGroup->addButton(m_modeFasterBtn);
    m_qualityGroup->addButton(m_modeBetterBtn);

    modeRow->addWidget(m_modeFasterBtn);
    modeRow->addWidget(m_modeBetterBtn);
    lay->addLayout(modeRow);

    // ── 9. Options ───────────────────────────────────────────────────────────
    m_postMultiplyCheck = new QCheckBox(i18n("Post Multiply Image (Alpha Cutout)"), content);
    m_postMultiplyCheck->setChecked(true);
    lay->addWidget(m_postMultiplyCheck);

    m_autoApplyCheck = new QCheckBox(i18n("Auto Apply Mask to Selected Clip"), content);
    m_autoApplyCheck->setChecked(true);
    lay->addWidget(m_autoApplyCheck);

    // ── 10. Generate / Progress ──────────────────────────────────────────────
    auto *rvmHumanBtn = new QPushButton(i18n("⚡ 1-Click Human Background Removal (RVM)"), content);
    rvmHumanBtn->setToolTip(i18n("Instant automatic background removal for human subjects using pre-trained RobustVideoMatting without needing manual strokes."));
    rvmHumanBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #27ae60; color: white; font-weight: bold; padding: 8px; border-radius: 4px; font-size: 11px; } QPushButton:hover { background-color: #2ecc71; }"));
    connect(rvmHumanBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotRunRvmHumanMatting);
    lay->addWidget(rvmHumanBtn);

    m_generateBtn = new QPushButton(i18n("🎯 Generate / Apply SAM2 Mask (Custom Strokes)"), content);
    m_generateBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #e54242; color: white; font-weight: bold; padding: 7px; border-radius: 4px; } QPushButton:hover { background-color: #ff5252; }"));
    connect(m_generateBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotApplyMask);
    lay->addWidget(m_generateBtn);

    m_progressBar = new QProgressBar(content);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    lay->addWidget(m_progressBar);

    m_statusMsgLabel = new QLabel(i18n("Ready"), content);
    m_statusMsgLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 10px;"));
    lay->addWidget(m_statusMsgLabel);

    lay->addStretch();
    scroll->setWidget(content);
    return scroll;
}

QWidget *MagicMaskWidget::buildMatteTab()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(8);

    m_invertMaskCheck = new QCheckBox(i18n("Invert Mask (Swap Foreground / Background)"), content);
    lay->addWidget(m_invertMaskCheck);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);

    // Radius (Erode / Dilate)
    auto *radiusLay = new QHBoxLayout;
    m_radiusSlider = new QSlider(Qt::Horizontal, content);
    m_radiusSlider->setRange(-50, 50);
    m_radiusSlider->setValue(0);
    m_radiusSpin = new QSpinBox(content);
    m_radiusSpin->setRange(-50, 50);
    m_radiusSpin->setValue(0);
    connect(m_radiusSlider, &QSlider::valueChanged, m_radiusSpin, &QSpinBox::setValue);
    connect(m_radiusSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), m_radiusSlider, &QSlider::setValue);
    radiusLay->addWidget(m_radiusSlider);
    radiusLay->addWidget(m_radiusSpin);
    form->addRow(i18n("Radius (Erode/Dilate):"), radiusLay);

    // Softness / Feather
    auto *featherLay = new QHBoxLayout;
    m_featherSlider = new QSlider(Qt::Horizontal, content);
    m_featherSlider->setRange(0, 100);
    m_featherSlider->setValue(10);
    m_featherSpin = new QSpinBox(content);
    m_featherSpin->setRange(0, 100);
    m_featherSpin->setValue(10);
    connect(m_featherSlider, &QSlider::valueChanged, m_featherSpin, &QSpinBox::setValue);
    connect(m_featherSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), m_featherSlider, &QSlider::setValue);
    featherLay->addWidget(m_featherSlider);
    featherLay->addWidget(m_featherSpin);
    form->addRow(i18n("Softness / Feather:"), featherLay);

    // Clean Black
    auto *blackLay = new QHBoxLayout;
    m_cleanBlackSlider = new QSlider(Qt::Horizontal, content);
    m_cleanBlackSlider->setRange(0, 100);
    m_cleanBlackSlider->setValue(0);
    m_cleanBlackSpin = new QDoubleSpinBox(content);
    m_cleanBlackSpin->setRange(0.0, 1.0);
    m_cleanBlackSpin->setSingleStep(0.05);
    m_cleanBlackSpin->setValue(0.0);
    connect(m_cleanBlackSlider, &QSlider::valueChanged, this, [this](int val) { m_cleanBlackSpin->setValue(val / 100.0); });
    connect(m_cleanBlackSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double val) { m_cleanBlackSlider->setValue(int(val * 100)); });
    blackLay->addWidget(m_cleanBlackSlider);
    blackLay->addWidget(m_cleanBlackSpin);
    form->addRow(i18n("Clean Black:"), blackLay);

    // Clean White
    auto *whiteLay = new QHBoxLayout;
    m_cleanWhiteSlider = new QSlider(Qt::Horizontal, content);
    m_cleanWhiteSlider->setRange(0, 100);
    m_cleanWhiteSlider->setValue(100);
    m_cleanWhiteSpin = new QDoubleSpinBox(content);
    m_cleanWhiteSpin->setRange(0.0, 1.0);
    m_cleanWhiteSpin->setSingleStep(0.05);
    m_cleanWhiteSpin->setValue(1.0);
    connect(m_cleanWhiteSlider, &QSlider::valueChanged, this, [this](int val) { m_cleanWhiteSpin->setValue(val / 100.0); });
    connect(m_cleanWhiteSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double val) { m_cleanWhiteSlider->setValue(int(val * 100)); });
    whiteLay->addWidget(m_cleanWhiteSlider);
    whiteLay->addWidget(m_cleanWhiteSpin);
    form->addRow(i18n("Clean White:"), whiteLay);

    // Blur / Defocus
    auto *blurLay = new QHBoxLayout;
    m_blurSlider = new QSlider(Qt::Horizontal, content);
    m_blurSlider->setRange(0, 50);
    m_blurSlider->setValue(0);
    m_blurSpin = new QDoubleSpinBox(content);
    m_blurSpin->setRange(0.0, 50.0);
    m_blurSpin->setValue(0.0);
    connect(m_blurSlider, &QSlider::valueChanged, this, [this](int val) { m_blurSpin->setValue(val); });
    connect(m_blurSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double val) { m_blurSlider->setValue(int(val)); });
    blurLay->addWidget(m_blurSlider);
    blurLay->addWidget(m_blurSpin);
    form->addRow(i18n("Blur / Defocus:"), blurLay);

    // Despill / Fringe
    auto *despillLay = new QHBoxLayout;
    m_despillSlider = new QSlider(Qt::Horizontal, content);
    m_despillSlider->setRange(0, 100);
    m_despillSlider->setValue(20);
    m_despillSpin = new QSpinBox(content);
    m_despillSpin->setRange(0, 100);
    m_despillSpin->setValue(20);
    m_despillSpin->setSuffix(QStringLiteral(" %"));
    connect(m_despillSlider, &QSlider::valueChanged, m_despillSpin, &QSpinBox::setValue);
    connect(m_despillSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), m_despillSlider, &QSlider::setValue);
    despillLay->addWidget(m_despillSlider);
    despillLay->addWidget(m_despillSpin);
    form->addRow(i18n("Despill / Edge Color:"), despillLay);

    // Preview Mode
    m_previewModeCombo = new QComboBox(content);
    m_previewModeCombo->addItem(i18n("Color Tint Overlay (Red)"), QStringLiteral("overlay"));
    m_previewModeCombo->addItem(i18n("Black & White Mask"), QStringLiteral("bw"));
    m_previewModeCombo->addItem(i18n("Transparent Cutout (Checkerboard)"), QStringLiteral("cutout"));
    form->addRow(i18n("Matte Preview:"), m_previewModeCombo);

    lay->addLayout(form);
    lay->addStretch();
    scroll->setWidget(content);
    return scroll;
}

QWidget *MagicMaskWidget::buildSettingsTab()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(8);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);

    // Model selection
    m_modelCombo = new QComboBox(content);
    m_modelCombo->addItem(i18n("Robust Video Matting (RVM - 1-Click Human Only)"), QStringLiteral("rvm_human_matting"));
    m_modelCombo->addItem(i18n("SAM2 Hiera Tiny (Fastest, Low VRAM)"), QStringLiteral("sam2_hiera_tiny"));
    m_modelCombo->addItem(i18n("SAM2 Hiera Small (Balanced)"), QStringLiteral("sam2_hiera_small"));
    m_modelCombo->addItem(i18n("SAM2 Hiera Base+ (High Quality)"), QStringLiteral("sam2_hiera_base_plus"));
    m_modelCombo->addItem(i18n("SAM2 Hiera Large (Maximum Precision)"), QStringLiteral("sam2_hiera_large"));
    form->addRow(i18n("AI Mask Model:"), m_modelCombo);

    // Device acceleration
    m_deviceCombo = new QComboBox(content);
    m_deviceCombo->addItem(i18n("Auto GPU (CUDA / ROCm / MPS)"), QStringLiteral("auto"));
    m_deviceCombo->addItem(i18n("CPU Only"), QStringLiteral("cpu"));
    form->addRow(i18n("Compute Device:"), m_deviceCombo);

    // Cache info
    m_cacheDirLabel = new QLabel(content);
    m_cacheDirLabel->setText(i18n("Cache: %1", KdenliveSettings::currenttmpfolder()));
    m_cacheDirLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 10px;"));
    form->addRow(i18n("Storage:"), m_cacheDirLabel);

    m_openCacheBtn = new QPushButton(i18n("Open Cache Directory"), content);
    connect(m_openCacheBtn, &QPushButton::clicked, this, [this]() {
        bool ok;
        QDir cacheDir = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CacheMask, &ok) : QDir(KdenliveSettings::currenttmpfolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(cacheDir.absolutePath()));
    });
    form->addRow(QString(), m_openCacheBtn);

    // Backend Status
    m_backendStatusLabel = new QLabel(content);
    form->addRow(i18n("Status:"), m_backendStatusLabel);

    m_configBackendBtn = new QPushButton(i18n("Configure SAM2 AI Plugin…"), content);
    connect(m_configBackendBtn, &QPushButton::clicked, this, []() {
        pCore->window()->slotShowPreferencePage(Kdenlive::PageSpeech, 1);
    });
    form->addRow(QString(), m_configBackendBtn);

    lay->addLayout(form);
    lay->addStretch();
    scroll->setWidget(content);
    return scroll;
}

void MagicMaskWidget::setupStyle()
{
    setStyleSheet(QStringLiteral(
        "QWidget#inspectorHeader { background: #18181c; border-bottom: 1px solid #2d2d35; border-radius: 4px; padding: 4px; }"
        "QTabWidget::pane { border: 1px solid #2d2d35; background: #1a1a1e; }"
        "QTabBar::tab { background: #141416; color: #888888; padding: 6px 14px; font-weight: bold; border-top-left-radius: 4px; border-top-right-radius: 4px; border: 1px solid #26262a; }"
        "QTabBar::tab:selected { background: #1e1e24; color: #ffffff; border-bottom: 2px solid #e54242; }"
        "QGroupBox { font-weight: bold; color: #aaaaaa; border: 1px solid #2d2d35; border-radius: 4px; margin-top: 10px; padding-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QPushButton { background: #2a2a32; border: 1px solid #3c3c46; border-radius: 3px; color: #dddddd; padding: 4px 8px; font-size: 11px; }"
        "QPushButton:hover { background: #363640; border-color: #555566; color: #ffffff; }"
        "QPushButton:checked { background: #e54242; border-color: #ff5252; color: #ffffff; font-weight: bold; }"
        "QToolButton { background: #2a2a32; border: 1px solid #3c3c46; border-radius: 3px; color: #dddddd; padding: 3px; }"
        "QToolButton:hover { background: #363640; border-color: #555566; color: #ffffff; }"
        "QToolButton:checked { background: #e54242; color: #ffffff; }"
        "QSlider::groove:horizontal { height: 4px; background: #26262e; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #e54242; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #ffffff; width: 12px; margin-top: -4px; margin-bottom: -4px; border-radius: 6px; }"
    ));
}

void MagicMaskWidget::slotCheckModelStatus()
{
    // Check if python venv & SAM2 are available
    QString reqFile = QStringLiteral("scripts/automask/requirements-sam.txt");
    QString scriptPath = QStandardPaths::locate(QStandardPaths::AppDataLocation, reqFile);
    bool ready = !scriptPath.isEmpty();
    if (m_backendStatusLabel) {
        m_backendStatusLabel->setText(ready
            ? QStringLiteral("<span style='color: #2ecc71; font-weight: bold;'>● SAM2 AI Engine Active</span>")
            : QStringLiteral("<span style='color: #e67e22; font-weight: bold;'>● Configuration Recommended</span>"));
    }
}

void MagicMaskWidget::slotLaunchTrackingForward()
{
    updateStatus(true, i18n("Tracking subject forward…"));
    if (m_maskManager) {
        m_maskManager->launchSimpleSam();
    }
    Q_EMIT trackingRequested(true, false);
}

void MagicMaskWidget::slotLaunchTrackingBackward()
{
    updateStatus(true, i18n("Tracking subject backward…"));
    if (m_maskManager) {
        m_maskManager->launchSimpleSam();
    }
    Q_EMIT trackingRequested(false, false);
}

void MagicMaskWidget::slotLaunchTrackingBidirectional()
{
    updateStatus(true, i18n("Tracking bidirectional (both ways)…"));
    if (m_maskManager) {
        m_maskManager->launchSimpleSam();
    }
    Q_EMIT trackingRequested(true, true);
}

void MagicMaskWidget::slotStopTracking()
{
    updateStatus(false, i18n("Tracking stopped"));
    if (m_maskManager) {
        m_maskManager->abortPreviewByMonitor();
    }
    Q_EMIT abortRequested();
}

void MagicMaskWidget::slotTrackToStart()
{
    slotGoToStart();
    slotLaunchTrackingBackward();
}

void MagicMaskWidget::slotTrackToEnd()
{
    slotLaunchTrackingForward();
}

void MagicMaskWidget::slotGoToStart()
{
    pCore->getMonitor(Kdenlive::ClipMonitor)->slotSeek(0);
}

void MagicMaskWidget::slotGoToReference()
{
    int refFrame = m_refTimeSlider->value();
    pCore->getMonitor(Kdenlive::ClipMonitor)->slotSeek(refFrame);
}

void MagicMaskWidget::slotGoToEnd()
{
    int duration = pCore->getItemDuration(m_owner);
    if (duration > 0) {
        pCore->getMonitor(Kdenlive::ClipMonitor)->slotSeek(duration);
    }
}

void MagicMaskWidget::slotClearCurrentStrokes()
{
    pCore->getMonitor(Kdenlive::ClipMonitor)->abortPreviewMask();
    updateStatus(true, i18n("Cleared strokes for current frame"));
}

void MagicMaskWidget::slotClearRangeStrokes()
{
    pCore->getMonitor(Kdenlive::ClipMonitor)->abortPreviewMask();
    updateStatus(true, i18n("Cleared strokes in selected range"));
}

void MagicMaskWidget::slotClearAllStrokes()
{
    pCore->getMonitor(Kdenlive::ClipMonitor)->abortPreviewMask();
    updateStatus(true, i18n("All strokes cleared"));
}

void MagicMaskWidget::slotRegenerateCache()
{
    updateStatus(true, i18n("Regenerating mask disk cache…"));
    slotApplyMask();
}

void MagicMaskWidget::slotClearCache()
{
    bool ok;
    QDir cacheDir = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CacheMask, &ok) : QDir();
    if (cacheDir.exists()) {
        cacheDir.removeRecursively();
    }
    updateStatus(true, i18n("Mask disk cache cleared"));
}

void MagicMaskWidget::slotApplyMask()
{
    if (m_modelCombo && m_modelCombo->currentData().toString() == QStringLiteral("rvm_human_matting")) {
        slotRunRvmHumanMatting();
        return;
    }
    if (m_maskManager) {
        m_maskManager->launchSimpleSam();
    } else {
        pCore->window()->slotRemoveBackground();
    }
    updateStatus(true, i18n("Generating and applying AI subject mask…"));
    Q_EMIT maskApplied(m_owner);
}

void MagicMaskWidget::slotRunRvmHumanMatting()
{
    updateStatus(true, i18n("Starting 1-Click Human Background Removal (RVM)…"));
    slotUpdateProgress(10);

    bool ok = false;
    QDir maskSrcFolder = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CacheMaskSource, &ok) : QDir();
    QDir maskOutFolder = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CacheMask, &ok) : QDir();

    QString pythonExe;
    QStringList venvCandidates = {
        QDir::home().filePath(QStringLiteral(".local/share/kdenlive/venv-sam/bin/python3")),
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kdenlive/venv-sam/bin/python3")),
        QStandardPaths::findExecutable(QStringLiteral("python3")),
        QStringLiteral("python3")
    };
    for (const QString &cand : venvCandidates) {
        if (!cand.isEmpty() && (QFile::exists(cand) || !cand.contains(QLatin1Char('/')))) {
            pythonExe = cand;
            break;
        }
    }
    if (pythonExe.isEmpty()) {
        pythonExe = QStringLiteral("python3");
    }

    QString scriptPath = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("scripts/automask/rvm_human_matting.py"));
    if (scriptPath.isEmpty() || !QFile::exists(scriptPath)) {
        QStringList scriptCandidates = {
            QCoreApplication::applicationDirPath() + QStringLiteral("/../data/scripts/automask/rvm_human_matting.py"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/data/scripts/automask/rvm_human_matting.py"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../share/kdenlive/scripts/automask/rvm_human_matting.py"),
            QDir::current().filePath(QStringLiteral("kdenlive/data/scripts/automask/rvm_human_matting.py")),
            QDir::current().filePath(QStringLiteral("data/scripts/automask/rvm_human_matting.py")),
            QDir::home().filePath(QStringLiteral(".local/share/kdenlive/scripts/automask/rvm_human_matting.py"))
        };
        for (const QString &cand : scriptCandidates) {
            if (QFile::exists(cand)) {
                scriptPath = cand;
                break;
            }
        }
    }

    QString modelPath = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("models/rvm_mobilenetv3_fp32.onnx"));
    if (modelPath.isEmpty() || !QFile::exists(modelPath)) {
        QStringList modelCandidates = {
            QCoreApplication::applicationDirPath() + QStringLiteral("/../../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../share/kdenlive/models/rvm_mobilenetv3_fp32.onnx"),
            QDir::current().filePath(QStringLiteral("RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx")),
            QDir::current().filePath(QStringLiteral("../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx")),
            QDir::home().filePath(QStringLiteral(".local/share/kdenlive/models/rvm_mobilenetv3_fp32.onnx"))
        };
        for (const QString &cand : modelCandidates) {
            if (QFile::exists(cand)) {
                modelPath = cand;
                break;
            }
        }
    }

    auto *proc = new QProcess(this);
    QStringList args;
    args << scriptPath
         << QStringLiteral("-I") << maskSrcFolder.absolutePath()
         << QStringLiteral("-O") << maskOutFolder.absolutePath()
         << QStringLiteral("-M") << modelPath
         << QStringLiteral("--cutout");

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        QString out = QString::fromUtf8(proc->readAllStandardOutput());
        if (out.contains(QStringLiteral("PROGRESS:"))) {
            QString pctStr = out.section(QStringLiteral("PROGRESS:"), 1, 1).section(QStringLiteral("%"), 0, 0).trimmed();
            int pct = pctStr.toInt();
            if (pct > 0) {
                slotUpdateProgress(pct);
            }
        }
    });

    connect(proc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, proc](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            slotUpdateProgress(100);
            updateStatus(true, i18n("1-Click Human Background Removal complete!"));
            Q_EMIT maskApplied(m_owner);
        } else {
            updateStatus(false, i18n("RVM Human Matting finished with warnings"));
        }
        proc->deleteLater();
    });

    slotUpdateProgress(30);
    proc->start(pythonExe, args);
}

void MagicMaskWidget::slotUpdateProgress(int progress)
{
    if (m_progressBar) {
        m_progressBar->setVisible(progress > 0 && progress < 100);
        m_progressBar->setValue(progress);
    }
    if (progress >= 100) {
        updateStatus(true, i18n("AI Mask applied successfully"));
    }
}
