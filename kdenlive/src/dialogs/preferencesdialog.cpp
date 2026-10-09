/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Modern Preferences Dialog — Sleek dark UI with surfaces, accent colors,
 * performance, and AI plugin controls matching modern creative NLE designs.
 */

#include "preferencesdialog.h"
#include "kdenlive_debug.h"
#include "kdenlivesettings.h"

#include <QQuickWidget>
#include <QQuickItem>
#include <QQmlContext>
#include <QQmlEngine>
#include <QVBoxLayout>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QApplication>
#include <QPalette>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QAction>

PreferencesBackend::PreferencesBackend(QObject *parent)
    : QObject(parent)
{
    loadSettings();
}

void PreferencesBackend::loadSettings()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup grp = config->group(QStringLiteral("ModernUI"));

    m_brightness = grp.readEntry(QStringLiteral("brightness"), 42);
    m_contrast = grp.readEntry(QStringLiteral("contrast"), -70);
    m_borders = grp.readEntry(QStringLiteral("borders"), 48);
    m_textBrightness = grp.readEntry(QStringLiteral("textBrightness"), 51);
    m_panelGaps = grp.readEntry(QStringLiteral("panelGaps"), 10);
    m_neutralTone = grp.readEntry(QStringLiteral("neutralTone"), 0);

    m_accentColor = grp.readEntry(QStringLiteral("accentColor"), QStringLiteral("#f59e0b"));
    m_normalColor = grp.readEntry(QStringLiteral("normalColor"), QStringLiteral("#d97706"));
    m_animatedColor = grp.readEntry(QStringLiteral("animatedColor"), QStringLiteral("#eab308"));
    m_animModColor = grp.readEntry(QStringLiteral("animModColor"), QStringLiteral("#fca5a5"));
    m_modifiedColor = grp.readEntry(QStringLiteral("modifiedColor"), QStringLiteral("#f472b6"));

    m_hwAccel = !KdenliveSettings::hwDecoding().isEmpty();
    m_previewScale = KdenliveSettings::previewScaling();
    m_ramCache = grp.readEntry(QStringLiteral("ramCache"), 2048);
    m_threads = KdenliveSettings::processingthreads();
    m_autoScroll = KdenliveSettings::autoscroll();
    m_trackHeight = KdenliveSettings::trackheight();
    m_snapDistance = grp.readEntry(QStringLiteral("snapDistance"), 10);
}

void PreferencesBackend::saveSettings()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup grp = config->group(QStringLiteral("ModernUI"));

    grp.writeEntry(QStringLiteral("brightness"), m_brightness);
    grp.writeEntry(QStringLiteral("contrast"), m_contrast);
    grp.writeEntry(QStringLiteral("borders"), m_borders);
    grp.writeEntry(QStringLiteral("textBrightness"), m_textBrightness);
    grp.writeEntry(QStringLiteral("panelGaps"), m_panelGaps);
    grp.writeEntry(QStringLiteral("neutralTone"), m_neutralTone);

    grp.writeEntry(QStringLiteral("accentColor"), m_accentColor);
    grp.writeEntry(QStringLiteral("normalColor"), m_normalColor);
    grp.writeEntry(QStringLiteral("animatedColor"), m_animatedColor);
    grp.writeEntry(QStringLiteral("animModColor"), m_animModColor);
    grp.writeEntry(QStringLiteral("modifiedColor"), m_modifiedColor);
    grp.writeEntry(QStringLiteral("ramCache"), m_ramCache);
    grp.writeEntry(QStringLiteral("snapDistance"), m_snapDistance);

    grp.sync();
}

void PreferencesBackend::setBrightness(int val)
{
    if (m_brightness != val) {
        m_brightness = val;
        Q_EMIT brightnessChanged();
        saveSettings();
    }
}

void PreferencesBackend::setContrast(int val)
{
    if (m_contrast != val) {
        m_contrast = val;
        Q_EMIT contrastChanged();
        saveSettings();
    }
}

void PreferencesBackend::setBorders(int val)
{
    if (m_borders != val) {
        m_borders = val;
        Q_EMIT bordersChanged();
        saveSettings();
    }
}

void PreferencesBackend::setTextBrightness(int val)
{
    if (m_textBrightness != val) {
        m_textBrightness = val;
        Q_EMIT textBrightnessChanged();
        saveSettings();
    }
}

void PreferencesBackend::setPanelGaps(int val)
{
    if (m_panelGaps != val) {
        m_panelGaps = val;
        Q_EMIT panelGapsChanged();
        saveSettings();
    }
}

void PreferencesBackend::setNeutralTone(int val)
{
    if (m_neutralTone != val) {
        m_neutralTone = val;
        Q_EMIT neutralToneChanged();
        saveSettings();
    }
}

void PreferencesBackend::setAccentColor(const QString &color)
{
    if (m_accentColor != color) {
        m_accentColor = color;
        Q_EMIT accentColorChanged();
        saveSettings();
        applyAccentColorToApp(color);
    }
}

void PreferencesBackend::setNormalColor(const QString &color)
{
    if (m_normalColor != color) {
        m_normalColor = color;
        Q_EMIT normalColorChanged();
        saveSettings();
    }
}

void PreferencesBackend::setAnimatedColor(const QString &color)
{
    if (m_animatedColor != color) {
        m_animatedColor = color;
        Q_EMIT animatedColorChanged();
        saveSettings();
    }
}

void PreferencesBackend::setAnimModColor(const QString &color)
{
    if (m_animModColor != color) {
        m_animModColor = color;
        Q_EMIT animModColorChanged();
        saveSettings();
    }
}

void PreferencesBackend::setModifiedColor(const QString &color)
{
    if (m_modifiedColor != color) {
        m_modifiedColor = color;
        Q_EMIT modifiedColorChanged();
        saveSettings();
    }
}

void PreferencesBackend::setHwAccel(bool enabled)
{
    if (m_hwAccel != enabled) {
        m_hwAccel = enabled;
        KdenliveSettings::setHwDecoding(enabled ? QStringLiteral("vaapi") : QString());
        Q_EMIT hwAccelChanged();
    }
}

void PreferencesBackend::setPreviewScale(int scale)
{
    if (m_previewScale != scale) {
        m_previewScale = scale;
        KdenliveSettings::setPreviewScaling(scale);
        Q_EMIT previewScaleChanged();
    }
}

void PreferencesBackend::setRamCache(int mb)
{
    if (m_ramCache != mb) {
        m_ramCache = mb;
        Q_EMIT ramCacheChanged();
        saveSettings();
    }
}

void PreferencesBackend::setThreads(int count)
{
    if (m_threads != count) {
        m_threads = count;
        KdenliveSettings::setProcessingthreads(count);
        Q_EMIT threadsChanged();
    }
}

void PreferencesBackend::setAutoScroll(bool scroll)
{
    if (m_autoScroll != scroll) {
        m_autoScroll = scroll;
        KdenliveSettings::setAutoscroll(scroll);
        Q_EMIT autoScrollChanged();
    }
}

void PreferencesBackend::setTrackHeight(int height)
{
    if (m_trackHeight != height) {
        m_trackHeight = height;
        KdenliveSettings::setTrackheight(height);
        Q_EMIT trackHeightChanged();
    }
}

void PreferencesBackend::setSnapDistance(int dist)
{
    if (m_snapDistance != dist) {
        m_snapDistance = dist;
        saveSettings();
        Q_EMIT snapDistanceChanged();
    }
}

QString PreferencesBackend::sam2Status() const
{
    QString model = KdenliveSettings::samModelFile();
    return !model.isEmpty() ? QStringLiteral("Ready") : QStringLiteral("Not configured");
}

QString PreferencesBackend::rvmStatus() const
{
    QString modelFile = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("models/rvm_mobilenetv3.onnx"));
    return !modelFile.isEmpty() ? QStringLiteral("Ready") : QStringLiteral("Ready (Internal)");
}

QString PreferencesBackend::whisperStatus() const
{
    return QStringLiteral("Active");
}

void PreferencesBackend::resetSetting(const QString &key)
{
    if (key == QStringLiteral("brightness")) {
        setBrightness(42);
    } else if (key == QStringLiteral("contrast")) {
        setContrast(-70);
    } else if (key == QStringLiteral("borders")) {
        setBorders(48);
    } else if (key == QStringLiteral("text")) {
        setTextBrightness(51);
    } else if (key == QStringLiteral("panelGaps")) {
        setPanelGaps(10);
    } else if (key == QStringLiteral("neutralTone")) {
        setNeutralTone(0);
    }
}

void PreferencesBackend::suggestFromAccent()
{
    QColor base(m_accentColor);
    setNormalColor(base.darker(120).name());
    setAnimatedColor(base.lighter(115).name());
    setAnimModColor(QColor(252, 165, 165).name());
    setModifiedColor(QColor(244, 114, 182).name());
}

void PreferencesBackend::openClassicSettings()
{
    Q_EMIT requestClassicSettings();
}

void PreferencesBackend::closeDialog()
{
    Q_EMIT requestClose();
}

void PreferencesBackend::applyAccentColorToApp(const QString &colorHex)
{
    QColor col(colorHex);
    if (!col.isValid()) {
        return;
    }
    // Update application-wide palette highlight color
    QPalette pal = qApp->palette();
    pal.setColor(QPalette::Highlight, col);
    pal.setColor(QPalette::Accent, col);
    qApp->setPalette(pal);

    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *w : widgets) {
        w->update();
    }
}

QQuickWidget *PreferencesDialog::createQuickWidget(QWidget *parent, PreferencesBackend *backend, bool showSidebar)
{
    auto *quickWidget = new QQuickWidget(parent);
    quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    KLocalization::setupLocalizedContext(quickWidget->engine());

    quickWidget->rootContext()->setContextProperty(QStringLiteral("backend"), backend);
    quickWidget->setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/kde/kdenlive/PreferencesDialog.qml")));
    if (quickWidget->rootObject()) {
        quickWidget->rootObject()->setProperty("showSidebar", showSidebar);
    }
    return quickWidget;
}

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowCloseButtonHint)
    , m_backend(new PreferencesBackend(this))
{
    setWindowTitle(i18n("Preferences"));
    resize(900, 640);
    setMinimumSize(800, 560);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_quickWidget = createQuickWidget(this, m_backend, true);

    connect(m_backend, &PreferencesBackend::requestClose, this, &QDialog::accept);
    connect(m_backend, &PreferencesBackend::requestClassicSettings, this, &QDialog::reject);

    lay->addWidget(m_quickWidget);
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::setCurrentPage(const QString &pageName)
{
    if (m_quickWidget && m_quickWidget->rootObject()) {
        m_quickWidget->rootObject()->setProperty("currentPage", pageName);
    }
}
