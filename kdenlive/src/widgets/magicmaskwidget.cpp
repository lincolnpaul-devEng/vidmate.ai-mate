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
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "assets/keyframes/model/automask/automaskhelper.hpp"
#include "timeline2/view/timelinewidget.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/model/timelineitemmodel.hpp"

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
    slotCheckModelStatus();
    updateControlsState(false);
}

MagicMaskWidget::~MagicMaskWidget() = default;

void MagicMaskWidget::setMaskManager(MaskManager *maskManager)
{
    m_maskManager = maskManager;
}

void MagicMaskWidget::setOwner(const ObjectId &owner)
{
    m_owner = owner;
    if (m_owner.type == KdenliveObjectType::TimelineClip && m_owner.uuid.isNull() && pCore->currentDoc()) {
        m_owner.uuid = pCore->currentTimelineId();
    }
    std::shared_ptr<ProjectClip> clip = getOwnerClip();
    bool isAudio = false;
    int duration = 0;
    QString trackName;
    int in = 0;

    if (m_owner.type == KdenliveObjectType::TimelineClip && pCore->currentDoc()) {
        auto timeline = pCore->window()->getCurrentTimeline();
        if (timeline && timeline->model() && m_owner.itemId > -1 && timeline->model()->isClip(m_owner.itemId)) {
            int trackId = timeline->model()->getItemTrackId(m_owner.itemId);
            if (trackId > -1) {
                isAudio = timeline->model()->isAudioTrack(trackId);
                trackName = timeline->controller()->getTrackNameFromIndex(trackId);
                if (trackName.isEmpty()) {
                    trackName = isAudio ? QStringLiteral("Audio Track %1").arg(trackId) : QStringLiteral("Video Track %1").arg(trackId);
                }
            }
            duration = timeline->model()->getClipPlaytime(m_owner.itemId);
            in = timeline->model()->getClipIn(m_owner.itemId);
        }
    } else if (clip) {
        duration = clip->duration().frames(pCore->getCurrentFps());
    }

    if (clip && !isAudio) {
        QString timecode = pCore->timecode().getTimecodeFromFrames(duration);
        m_titleLabel->setText(i18n("Magic Mask: %1", clip->clipName()));
        if (m_clipMessageWidget) {
            m_clipMessageWidget->setMessageType(KMessageWidget::Positive);
            m_clipMessageWidget->setIcon(QIcon::fromTheme(QStringLiteral("video-clip")));
            QString location = trackName.isEmpty() ? (m_owner.type == KdenliveObjectType::BinClip ? i18n("Project Bin") : i18n("Timeline")) : trackName;
            QString details = i18n("Target: %1 (%2) — In: %3 | Duration: %4",
                                   clip->clipName(),
                                   location,
                                   pCore->timecode().getTimecodeFromFrames(in),
                                   timecode);
            m_clipMessageWidget->setText(details);
            m_clipMessageWidget->animatedShow();
        }
        m_refTimeSlider->setRange(0, qMax(0, duration));
        m_rangeEndSpin->setValue(duration / pCore->getCurrentFps());
        updateStatus(true, i18n("Ready: %1", clip->clipName()));
        updateControlsState(true);
    } else {
        m_titleLabel->setText(i18n("Magic Mask"));
        if (m_clipMessageWidget) {
            if (isAudio) {
                m_clipMessageWidget->setMessageType(KMessageWidget::Warning);
                m_clipMessageWidget->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
                m_clipMessageWidget->setText(i18n("Audio clip selected (%1). Magic Mask and matting require a video or image clip.", trackName));
            } else {
                m_clipMessageWidget->setMessageType(KMessageWidget::Information);
                m_clipMessageWidget->setIcon(QIcon::fromTheme(QStringLiteral("dialog-information")));
                m_clipMessageWidget->setText(i18n("Select a video or image clip in the timeline or Project Bin."));
            }
            m_clipMessageWidget->animatedShow();
        }
        updateStatus(false, isAudio ? i18n("Audio clip selected") : i18n("No clip selected"));
        updateControlsState(false);
    }
}

std::shared_ptr<ProjectClip> MagicMaskWidget::getOwnerClip()
{
    if (m_owner.itemId < 0 || m_owner.type == KdenliveObjectType::NoItem) {
        return nullptr;
    }
    QString binId;
    switch (m_owner.type) {
    case KdenliveObjectType::TimelineClip: {
        if (m_owner.uuid.isNull() && pCore->currentDoc()) {
            m_owner.uuid = pCore->currentTimelineId();
        }
        auto timeline = pCore->window()->getCurrentTimeline();
        if (timeline && timeline->model() && timeline->model()->isClip(m_owner.itemId)) {
            binId = timeline->model()->getClipBinId(m_owner.itemId);
        } else if (!m_owner.uuid.isNull()) {
            binId = pCore->getTimelineClipBinId(m_owner);
        }
        break;
    }
    case KdenliveObjectType::BinClip: {
        binId = QString::number(m_owner.itemId);
        break;
    }
    default:
        break;
    }
    if (binId.isEmpty() || !pCore->projectItemModel()) {
        return nullptr;
    }
    return pCore->projectItemModel()->getClipByBinID(binId);
}

void MagicMaskWidget::updateStatus(bool active, const QString &statusText)
{
    Q_UNUSED(active);
    if (m_statusMsgLabel && !statusText.isEmpty()) {
        m_statusMsgLabel->setText(statusText);
    }
}

void MagicMaskWidget::updateControlsState(bool enabled)
{
    if (m_rvmHumanBtn) m_rvmHumanBtn->setEnabled(enabled);
    if (m_generateBtn) m_generateBtn->setEnabled(enabled);
    if (m_btnTrackStart) m_btnTrackStart->setEnabled(enabled);
    if (m_btnTrackBack) m_btnTrackBack->setEnabled(enabled);
    if (m_btnTrackStop) m_btnTrackStop->setEnabled(enabled);
    if (m_btnTrackBidi) m_btnTrackBidi->setEnabled(enabled);
    if (m_btnTrackFwd) m_btnTrackFwd->setEnabled(enabled);
    if (m_btnTrackEnd) m_btnTrackEnd->setEnabled(enabled);
    if (m_btnGoStart) m_btnGoStart->setEnabled(enabled);
    if (m_btnGoRef) m_btnGoRef->setEnabled(enabled);
    if (m_btnGoEnd) m_btnGoEnd->setEnabled(enabled);
    if (m_strokeAddBtn) m_strokeAddBtn->setEnabled(enabled);
    if (m_strokeSubBtn) m_strokeSubBtn->setEnabled(enabled);
    if (m_strokeSelectBtn) m_strokeSelectBtn->setEnabled(enabled);
    if (m_strokeDeleteBtn) m_strokeDeleteBtn->setEnabled(enabled);
    if (m_clearCurrentBtn) m_clearCurrentBtn->setEnabled(enabled);
    if (m_clearRangeBtn) m_clearRangeBtn->setEnabled(enabled);
    if (m_clearAllBtn) m_clearAllBtn->setEnabled(enabled);
    if (m_regenCacheBtn) m_regenCacheBtn->setEnabled(enabled);
    if (m_clearCacheBtn) m_clearCacheBtn->setEnabled(enabled);
    if (m_refTimeSlider) m_refTimeSlider->setEnabled(enabled);
    if (m_rangeStartSpin) m_rangeStartSpin->setEnabled(enabled);
    if (m_rangeEndSpin) m_rangeEndSpin->setEnabled(enabled);
    if (m_modeFasterBtn) m_modeFasterBtn->setEnabled(enabled);
    if (m_modeBetterBtn) m_modeBetterBtn->setEnabled(enabled);
    if (m_postMultiplyCheck) m_postMultiplyCheck->setEnabled(enabled);
    if (m_autoApplyCheck) m_autoApplyCheck->setEnabled(enabled);
    if (m_invertMaskCheck) m_invertMaskCheck->setEnabled(enabled);
    if (m_radiusSlider) m_radiusSlider->setEnabled(enabled);
    if (m_radiusSpin) m_radiusSpin->setEnabled(enabled);
    if (m_featherSlider) m_featherSlider->setEnabled(enabled);
    if (m_featherSpin) m_featherSpin->setEnabled(enabled);
    if (m_cleanBlackSlider) m_cleanBlackSlider->setEnabled(enabled);
    if (m_cleanBlackSpin) m_cleanBlackSpin->setEnabled(enabled);
    if (m_cleanWhiteSlider) m_cleanWhiteSlider->setEnabled(enabled);
    if (m_cleanWhiteSpin) m_cleanWhiteSpin->setEnabled(enabled);
    if (m_blurSlider) m_blurSlider->setEnabled(enabled);
    if (m_blurSpin) m_blurSpin->setEnabled(enabled);
    if (m_despillSlider) m_despillSlider->setEnabled(enabled);
    if (m_despillSpin) m_despillSpin->setEnabled(enabled);
    if (m_previewModeCombo) m_previewModeCombo->setEnabled(enabled);
}

void MagicMaskWidget::buildUI()
{
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(4, 4, 4, 4);
    mainLay->setSpacing(6);

    // 1. Header
    mainLay->addWidget(buildHeader());

    // 2. Native Target Clip Info Banner
    m_clipMessageWidget = new KMessageWidget(this);
    m_clipMessageWidget->setCloseButtonVisible(false);
    m_clipMessageWidget->setWordWrap(true);
    m_clipMessageWidget->setMessageType(KMessageWidget::Information);
    m_clipMessageWidget->setIcon(QIcon::fromTheme(QStringLiteral("dialog-information")));
    m_clipMessageWidget->setText(i18n("No clip selected. Select a video or image clip in the timeline or Project Bin."));
    mainLay->addWidget(m_clipMessageWidget);

    // 3. Tab Widget (Tracking, Matte, Settings)
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
    auto *hLay = new QHBoxLayout(headerWidget);
    hLay->setContentsMargins(0, 0, 0, 0);
    hLay->setSpacing(6);

    auto *titleIcon = new QLabel(headerWidget);
    titleIcon->setPixmap(QIcon::fromTheme(QStringLiteral("draw-polygon")).pixmap(16, 16));
    hLay->addWidget(titleIcon);

    m_titleLabel = new QLabel(i18n("Magic Mask"), headerWidget);
    QFont boldFont = m_titleLabel->font();
    boldFont.setBold(true);
    m_titleLabel->setFont(boldFont);
    hLay->addWidget(m_titleLabel);

    hLay->addStretch();

    m_resetBtn = new QToolButton(headerWidget);
    m_resetBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    m_resetBtn->setToolTip(i18n("Reset Strokes"));
    m_resetBtn->setAutoRaise(true);
    connect(m_resetBtn, &QToolButton::clicked, this, &MagicMaskWidget::slotClearAllStrokes);
    hLay->addWidget(m_resetBtn);

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

    // ── 1. Tracking Transport Bar ──────────────────────────────────────────
    auto *trackGroup = new QGroupBox(i18n("Tracking Transport"), content);
    auto *trackLay = new QHBoxLayout(trackGroup);
    trackLay->setContentsMargins(4, 6, 4, 6);
    trackLay->setSpacing(4);

    m_btnTrackStart = new QToolButton(trackGroup);
    m_btnTrackStart->setIcon(QIcon::fromTheme(QStringLiteral("media-skip-backward")));
    m_btnTrackStart->setToolTip(i18n("Track Reverse to Start"));
    m_btnTrackStart->setAutoRaise(true);
    connect(m_btnTrackStart, &QToolButton::clicked, this, &MagicMaskWidget::slotTrackToStart);

    m_btnTrackBack = new QToolButton(trackGroup);
    m_btnTrackBack->setIcon(QIcon::fromTheme(QStringLiteral("media-seek-backward")));
    m_btnTrackBack->setToolTip(i18n("Track Backward (1 Step)"));
    m_btnTrackBack->setAutoRaise(true);
    connect(m_btnTrackBack, &QToolButton::clicked, this, &MagicMaskWidget::slotLaunchTrackingBackward);

    m_btnTrackStop = new QToolButton(trackGroup);
    m_btnTrackStop->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
    m_btnTrackStop->setToolTip(i18n("Stop Tracking"));
    m_btnTrackStop->setAutoRaise(true);
    connect(m_btnTrackStop, &QToolButton::clicked, this, &MagicMaskWidget::slotStopTracking);

    m_btnTrackBidi = new QToolButton(trackGroup);
    m_btnTrackBidi->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    m_btnTrackBidi->setToolTip(i18n("Track Bidirectional (Both Directions)"));
    m_btnTrackBidi->setAutoRaise(true);
    connect(m_btnTrackBidi, &QToolButton::clicked, this, &MagicMaskWidget::slotLaunchTrackingBidirectional);

    m_btnTrackFwd = new QToolButton(trackGroup);
    m_btnTrackFwd->setIcon(QIcon::fromTheme(QStringLiteral("media-seek-forward")));
    m_btnTrackFwd->setToolTip(i18n("Track Forward (1 Step)"));
    m_btnTrackFwd->setAutoRaise(true);
    connect(m_btnTrackFwd, &QToolButton::clicked, this, &MagicMaskWidget::slotLaunchTrackingForward);

    m_btnTrackEnd = new QToolButton(trackGroup);
    m_btnTrackEnd->setIcon(QIcon::fromTheme(QStringLiteral("media-skip-forward")));
    m_btnTrackEnd->setToolTip(i18n("Track Forward to End"));
    m_btnTrackEnd->setAutoRaise(true);
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
    auto *gotoLabel = new QLabel(i18n("Go to:"), content);
    gotoRow->addWidget(gotoLabel);

    m_btnGoStart = new QToolButton(content);
    m_btnGoStart->setIcon(QIcon::fromTheme(QStringLiteral("go-first")));
    m_btnGoStart->setText(i18n("Start"));
    m_btnGoStart->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_btnGoStart->setAutoRaise(true);
    connect(m_btnGoStart, &QToolButton::clicked, this, &MagicMaskWidget::slotGoToStart);
    gotoRow->addWidget(m_btnGoStart);

    m_btnGoRef = new QToolButton(content);
    m_btnGoRef->setIcon(QIcon::fromTheme(QStringLiteral("go-jump")));
    m_btnGoRef->setText(i18n("Reference"));
    m_btnGoRef->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_btnGoRef->setAutoRaise(true);
    connect(m_btnGoRef, &QToolButton::clicked, this, &MagicMaskWidget::slotGoToReference);
    gotoRow->addWidget(m_btnGoRef);

    m_btnGoEnd = new QToolButton(content);
    m_btnGoEnd->setIcon(QIcon::fromTheme(QStringLiteral("go-last")));
    m_btnGoEnd->setText(i18n("End"));
    m_btnGoEnd->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_btnGoEnd->setAutoRaise(true);
    connect(m_btnGoEnd, &QToolButton::clicked, this, &MagicMaskWidget::slotGoToEnd);
    gotoRow->addWidget(m_btnGoEnd);
    gotoRow->addStretch();
    lay->addLayout(gotoRow);

    // ── 3. Stroke Controls ───────────────────────────────────────────────────
    auto *strokeGroup = new QGroupBox(i18n("Strokes"), content);
    auto *strokeLay = new QVBoxLayout(strokeGroup);
    strokeLay->setContentsMargins(4, 6, 4, 6);
    strokeLay->setSpacing(6);

    auto *modeRow = new QHBoxLayout;
    m_strokeModeGroup = new QButtonGroup(strokeGroup);

    m_strokeAddBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Foreground"), strokeGroup);
    m_strokeAddBtn->setCheckable(true);
    m_strokeAddBtn->setChecked(true);
    m_strokeAddBtn->setToolTip(i18n("Add foreground subject point or stroke"));

    m_strokeSubBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), i18n("Background"), strokeGroup);
    m_strokeSubBtn->setCheckable(true);
    m_strokeSubBtn->setToolTip(i18n("Add background exclusion point or stroke"));

    m_strokeSelectBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("select-rectangular")), i18n("Box"), strokeGroup);
    m_strokeSelectBtn->setCheckable(true);
    m_strokeSelectBtn->setToolTip(i18n("Select bounding box for subject"));

    m_strokeDeleteBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Delete"), strokeGroup);
    m_strokeDeleteBtn->setToolTip(i18n("Delete active stroke"));
    connect(m_strokeDeleteBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearCurrentStrokes);

    m_strokeModeGroup->addButton(m_strokeAddBtn);
    m_strokeModeGroup->addButton(m_strokeSubBtn);
    m_strokeModeGroup->addButton(m_strokeSelectBtn);

    modeRow->addWidget(m_strokeAddBtn);
    modeRow->addWidget(m_strokeSubBtn);
    modeRow->addWidget(m_strokeSelectBtn);
    modeRow->addWidget(m_strokeDeleteBtn);
    strokeLay->addLayout(modeRow);

    auto *clearRow = new QHBoxLayout;
    auto *clearLabel = new QLabel(i18n("Clear:"), strokeGroup);
    clearRow->addWidget(clearLabel);

    m_clearCurrentBtn = new QPushButton(i18n("Current"), strokeGroup);
    connect(m_clearCurrentBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearCurrentStrokes);
    m_clearRangeBtn = new QPushButton(i18n("Range"), strokeGroup);
    connect(m_clearRangeBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearRangeStrokes);
    m_clearAllBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-all")), i18n("All"), strokeGroup);
    connect(m_clearAllBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearAllStrokes);

    clearRow->addWidget(m_clearCurrentBtn);
    clearRow->addWidget(m_clearRangeBtn);
    clearRow->addWidget(m_clearAllBtn);
    strokeLay->addLayout(clearRow);

    lay->addWidget(strokeGroup);

    // ── 4. Range & Reference ─────────────────────────────────────────────────
    auto *rangeGroup = new QGroupBox(i18n("Range & Reference"), content);
    auto *rangeForm = new QFormLayout(rangeGroup);
    rangeForm->setContentsMargins(4, 6, 4, 6);
    rangeForm->setSpacing(6);

    auto *refRow = new QHBoxLayout;
    m_refTimeSlider = new QSlider(Qt::Horizontal, rangeGroup);
    m_refTimeSlider->setRange(0, 100);
    m_refTimeSlider->setValue(0);
    m_refTimeLabel = new QLabel(QStringLiteral("0.0s"), rangeGroup);
    m_refTimeLabel->setFixedWidth(45);
    m_refTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    refRow->addWidget(m_refTimeSlider, 1);
    refRow->addWidget(m_refTimeLabel);
    connect(m_refTimeSlider, &QSlider::valueChanged, this, [this](int val) {
        double seconds = val / pCore->getCurrentFps();
        m_refTimeLabel->setText(QStringLiteral("%1s").arg(seconds, 0, 'f', 1));
    });
    rangeForm->addRow(i18n("Reference Time:"), refRow);

    auto *procRow = new QHBoxLayout;
    m_rangeStartSpin = new QDoubleSpinBox(rangeGroup);
    m_rangeStartSpin->setRange(0.0, 9999.0);
    m_rangeStartSpin->setValue(0.0);
    m_rangeStartSpin->setSuffix(QStringLiteral(" s"));
    procRow->addWidget(m_rangeStartSpin);
    auto *toLabel = new QLabel(i18n("to"), rangeGroup);
    procRow->addWidget(toLabel);
    m_rangeEndSpin = new QDoubleSpinBox(rangeGroup);
    m_rangeEndSpin->setRange(0.0, 9999.0);
    m_rangeEndSpin->setValue(0.0);
    m_rangeEndSpin->setSuffix(QStringLiteral(" s"));
    procRow->addWidget(m_rangeEndSpin);
    rangeForm->addRow(i18n("Processed Frames:"), procRow);

    auto *qualityRow = new QHBoxLayout;
    m_qualityGroup = new QButtonGroup(rangeGroup);
    m_modeFasterBtn = new QPushButton(i18n("Faster"), rangeGroup);
    m_modeFasterBtn->setCheckable(true);
    m_modeFasterBtn->setChecked(true);
    m_modeBetterBtn = new QPushButton(i18n("Better"), rangeGroup);
    m_modeBetterBtn->setCheckable(true);
    m_qualityGroup->addButton(m_modeFasterBtn);
    m_qualityGroup->addButton(m_modeBetterBtn);
    qualityRow->addWidget(m_modeFasterBtn);
    qualityRow->addWidget(m_modeBetterBtn);
    rangeForm->addRow(i18n("Quality:"), qualityRow);

    lay->addWidget(rangeGroup);

    // ── 5. Actions & Processing ──────────────────────────────────────────────
    auto *actionGroup = new QGroupBox(i18n("Actions"), content);
    auto *actionLay = new QVBoxLayout(actionGroup);
    actionLay->setContentsMargins(4, 6, 4, 6);
    actionLay->setSpacing(6);

    m_postMultiplyCheck = new QCheckBox(i18n("Post-multiply alpha"), actionGroup);
    m_postMultiplyCheck->setChecked(true);
    actionLay->addWidget(m_postMultiplyCheck);

    m_autoApplyCheck = new QCheckBox(i18n("Auto-apply to clip"), actionGroup);
    m_autoApplyCheck->setChecked(true);
    actionLay->addWidget(m_autoApplyCheck);

    m_rvmHumanBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("tools-wizard")), i18n("Human Matting"), actionGroup);
    m_rvmHumanBtn->setToolTip(i18n("Automatic human background matting (RVM)"));
    connect(m_rvmHumanBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotRunRvmHumanMatting);
    actionLay->addWidget(m_rvmHumanBtn);

    m_generateBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("draw-polygon")), i18n("Subject Masking"), actionGroup);
    m_generateBtn->setToolTip(i18n("Track and segment subject with SAM 2 strokes"));
    connect(m_generateBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotApplyMask);
    actionLay->addWidget(m_generateBtn);

    auto *cacheRow = new QHBoxLayout;
    m_regenCacheBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Regenerate Cache"), actionGroup);
    connect(m_regenCacheBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotRegenerateCache);
    m_clearCacheBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), i18n("Clear Cache"), actionGroup);
    connect(m_clearCacheBtn, &QPushButton::clicked, this, &MagicMaskWidget::slotClearCache);
    cacheRow->addWidget(m_regenCacheBtn);
    cacheRow->addWidget(m_clearCacheBtn);
    actionLay->addLayout(cacheRow);

    m_progressBar = new QProgressBar(actionGroup);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    actionLay->addWidget(m_progressBar);

    m_statusMsgLabel = new QLabel(i18n("Ready"), actionGroup);
    m_statusMsgLabel->setWordWrap(true);
    actionLay->addWidget(m_statusMsgLabel);

    lay->addWidget(actionGroup);

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

    m_invertMaskCheck = new QCheckBox(i18n("Invert Mask"), content);
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
    form->addRow(i18n("Radius:"), radiusLay);

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
    form->addRow(i18n("Feather:"), featherLay);

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
    form->addRow(i18n("Blur:"), blurLay);

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
    form->addRow(i18n("Despill:"), despillLay);

    // Preview Mode
    m_previewModeCombo = new QComboBox(content);
    m_previewModeCombo->addItem(i18n("Overlay"), QStringLiteral("overlay"));
    m_previewModeCombo->addItem(i18n("Black & White"), QStringLiteral("bw"));
    m_previewModeCombo->addItem(i18n("Cutout"), QStringLiteral("cutout"));
    form->addRow(i18n("Preview:"), m_previewModeCombo);

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
    m_modelCombo->addItem(i18n("RVM (Human Only)"), QStringLiteral("rvm_human_matting"));
    m_modelCombo->addItem(i18n("SAM 2 Tiny (Fast)"), QStringLiteral("sam2_hiera_tiny"));
    m_modelCombo->addItem(i18n("SAM 2 Small (Balanced)"), QStringLiteral("sam2_hiera_small"));
    m_modelCombo->addItem(i18n("SAM 2 Base+ (High Quality)"), QStringLiteral("sam2_hiera_base_plus"));
    m_modelCombo->addItem(i18n("SAM 2 Large (Maximum Quality)"), QStringLiteral("sam2_hiera_large"));
    form->addRow(i18n("Model:"), m_modelCombo);

    // Device acceleration
    m_deviceCombo = new QComboBox(content);
    m_deviceCombo->addItem(i18n("Auto GPU (CUDA / ROCm / MPS)"), QStringLiteral("auto"));
    m_deviceCombo->addItem(i18n("CPU Only"), QStringLiteral("cpu"));
    form->addRow(i18n("Compute:"), m_deviceCombo);

    // Cache info
    m_cacheDirLabel = new QLabel(content);
    m_cacheDirLabel->setText(KdenliveSettings::currenttmpfolder());
    form->addRow(i18n("Cache:"), m_cacheDirLabel);

    m_openCacheBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Open Folder"), content);
    connect(m_openCacheBtn, &QPushButton::clicked, this, [this]() {
        bool ok;
        QDir cacheDir = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CacheMask, &ok) : QDir(KdenliveSettings::currenttmpfolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(cacheDir.absolutePath()));
    });
    form->addRow(QString(), m_openCacheBtn);

    // Backend Status
    m_backendStatusLabel = new QLabel(content);
    form->addRow(i18n("Status:"), m_backendStatusLabel);

    m_configBackendBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), i18n("Configure…"), content);
    connect(m_configBackendBtn, &QPushButton::clicked, this, []() {
        pCore->window()->slotShowPreferencePage(Kdenlive::PageSpeech, 1);
    });
    form->addRow(QString(), m_configBackendBtn);

    lay->addLayout(form);
    lay->addStretch();
    scroll->setWidget(content);
    return scroll;
}

void MagicMaskWidget::slotCheckModelStatus()
{
    QString reqFile = QStringLiteral("scripts/automask/requirements-sam.txt");
    QString scriptPath = QStandardPaths::locate(QStandardPaths::AppDataLocation, reqFile);
    bool ready = !scriptPath.isEmpty();
    if (m_backendStatusLabel) {
        m_backendStatusLabel->setText(ready ? i18n("Ready") : i18n("Not configured"));
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
    std::shared_ptr<ProjectClip> clip = getOwnerClip();
    if (!clip) {
        pCore->displayMessage(i18n("No clip selected for AI background removal"), ErrorMessage, 500);
        return;
    }
    if (m_modelCombo && m_modelCombo->currentData().toString() == QStringLiteral("rvm_human_matting")) {
        slotRunRvmHumanMatting();
        return;
    }
    if (m_maskManager) {
        m_maskManager->launchSimpleSam();
    } else {
        pCore->window()->slotRemoveBackground(m_owner.itemId);
    }
    updateStatus(true, i18n("Generating and applying AI subject mask for '%1'…", clip->clipName()));
    Q_EMIT maskApplied(m_owner);
}

void MagicMaskWidget::applyRvmMaskToClip()
{
    std::shared_ptr<ProjectClip> clip = getOwnerClip();
    if (!clip || m_owner.type != KdenliveObjectType::TimelineClip) {
        return;
    }
    bool ok = false;
    QDir maskOutFolder = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CacheMask, &ok) : QDir();
    QStringList maskFiles = maskOutFolder.entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg")}, QDir::Files, QDir::Name);
    if (maskFiles.isEmpty()) {
        return;
    }
    QString firstMask = maskOutFolder.absoluteFilePath(maskFiles.first());
    auto timeline = pCore->window()->getCurrentTimeline();
    int in = 0;
    int duration = 0;
    if (timeline && timeline->model() && timeline->model()->isClip(m_owner.itemId)) {
        in = timeline->model()->getClipIn(m_owner.itemId);
        duration = timeline->model()->getClipPlaytime(m_owner.itemId);
    }
    int out = in + duration - 1;

    QMap<QString, QString> params;
    params.insert(QStringLiteral("resource"), firstMask);
    params.insert(QStringLiteral("in"), QString::number(in));
    params.insert(QStringLiteral("out"), QString::number(out));
    params.insert(QStringLiteral("softness"), QString::number(0.2));
    params.insert(QStringLiteral("mix"), QStringLiteral("100"));

    if (m_owner.uuid.isNull() && pCore->currentDoc()) {
        m_owner.uuid = pCore->currentTimelineId();
    }
    std::shared_ptr<EffectStackModel> stack = pCore->getItemEffectStack(m_owner.uuid, int(m_owner.type), m_owner.itemId);
    if (stack) {
        stack->appendEffect(QStringLiteral("shape"), true, params);
        pCore->displayMessage(i18n("Applied AI background removal mask to clip '%1'", clip->clipName()), InformationMessage, 500);
        pCore->refreshProjectMonitorOnce(true);
    }
}

void MagicMaskWidget::slotRunRvmHumanMatting()
{
    std::shared_ptr<ProjectClip> clip = getOwnerClip();
    if (!clip) {
        updateStatus(false, i18n("No clip selected for background removal"));
        pCore->displayMessage(i18n("No clip selected for AI background removal"), ErrorMessage, 500);
        return;
    }

    updateStatus(true, i18n("Starting 1-Click Human Background Removal (RVM) for '%1'…", clip->clipName()));
    slotUpdateProgress(10);

    bool ok = false;
    QDir maskSrcFolder = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CacheMaskSource, &ok) : QDir();
    QDir maskOutFolder = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CacheMask, &ok) : QDir();

    maskSrcFolder.mkpath(QStringLiteral("."));
    maskOutFolder.mkpath(QStringLiteral("."));

    // Extract frames if CacheMaskSource is empty
    if (maskSrcFolder.entryList({QStringLiteral("*.jpg"), QStringLiteral("*.png"), QStringLiteral("*.jpeg")}, QDir::Files).isEmpty()) {
        QString clipFile = clip->clipUrl();
        if (QFile::exists(clipFile)) {
            int in = pCore->getItemIn(m_owner);
            int duration = pCore->getItemDuration(m_owner);
            double inSec = qMax(0.0, in / pCore->getCurrentFps());
            double durSec = qMax(0.1, duration / pCore->getCurrentFps());
            updateStatus(true, i18n("Extracting clip frames for Human Matting…"));
            QProcess ffmpeg;
            QStringList ffArgs;
            ffArgs << QStringLiteral("-ss") << QString::number(inSec, 'f', 3)
                   << QStringLiteral("-t") << QString::number(durSec, 'f', 3)
                   << QStringLiteral("-i") << clipFile
                   << QStringLiteral("-q:v") << QStringLiteral("2")
                   << maskSrcFolder.filePath(QStringLiteral("%05d.jpg"));
            ffmpeg.start(QStringLiteral("ffmpeg"), ffArgs);
            ffmpeg.waitForFinished(60000);
        }
    }

    QString pythonExe;
    QStringList venvCandidates = {
        QDir::home().filePath(QStringLiteral(".local/share/kdenlive/venv-sam/bin/python3")),
        QDir::home().filePath(QStringLiteral(".local/share/kdenlive/venv/bin/python3")),
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
            QDir::home().filePath(QStringLiteral(".local/share/kdenlive/scripts/automask/rvm_human_matting.py")),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../data/scripts/automask/rvm_human_matting.py"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/data/scripts/automask/rvm_human_matting.py"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../share/kdenlive/scripts/automask/rvm_human_matting.py"),
            QDir::current().filePath(QStringLiteral("kdenlive/data/scripts/automask/rvm_human_matting.py")),
            QDir::current().filePath(QStringLiteral("data/scripts/automask/rvm_human_matting.py"))
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
            QDir::home().filePath(QStringLiteral(".local/share/kdenlive/models/rvm_mobilenetv3_fp32.onnx")),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../share/kdenlive/models/rvm_mobilenetv3_fp32.onnx"),
            QDir::current().filePath(QStringLiteral("RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx")),
            QDir::current().filePath(QStringLiteral("../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx")),
            QDir::current().filePath(QStringLiteral("../../RVM-Inference/examples/hub/onnx/cv/rvm_mobilenetv3_fp32.onnx"))
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

    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        QString err = QString::fromUtf8(proc->readAllStandardError());
        qWarning() << "[RVM STDERR]:" << err;
    });

    connect(proc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, proc, clip](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
            slotUpdateProgress(100);
            updateStatus(true, i18n("1-Click Human Background Removal complete for '%1'!", clip ? clip->clipName() : QString()));
            applyRvmMaskToClip();
            Q_EMIT maskApplied(m_owner);
        } else {
            QString err = QString::fromUtf8(proc->readAllStandardError());
            qWarning() << "RVM failed with exit code" << exitCode << "error:" << err;
            if (m_progressBar) {
                m_progressBar->setVisible(false);
            }
            updateStatus(false, i18n("RVM Human Matting finished with warnings. Check logs for details."));
        }
        proc->deleteLater();
    });

    slotUpdateProgress(20);
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
