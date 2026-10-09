/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Modern Preferences Dialog — Sleek dark UI with surfaces, accent colors,
 * performance, and AI plugin controls matching modern creative NLE designs.
 */

#pragma once

#include <QDialog>
#include <QObject>
#include <QString>
#include <QColor>
#include <QVariantList>

class QQuickWidget;

class PreferencesBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(int contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(int borders READ borders WRITE setBorders NOTIFY bordersChanged)
    Q_PROPERTY(int textBrightness READ textBrightness WRITE setTextBrightness NOTIFY textBrightnessChanged)
    Q_PROPERTY(int panelGaps READ panelGaps WRITE setPanelGaps NOTIFY panelGapsChanged)
    Q_PROPERTY(int neutralTone READ neutralTone WRITE setNeutralTone NOTIFY neutralToneChanged)

    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(QString normalColor READ normalColor WRITE setNormalColor NOTIFY normalColorChanged)
    Q_PROPERTY(QString animatedColor READ animatedColor WRITE setAnimatedColor NOTIFY animatedColorChanged)
    Q_PROPERTY(QString animModColor READ animModColor WRITE setAnimModColor NOTIFY animModColorChanged)
    Q_PROPERTY(QString modifiedColor READ modifiedColor WRITE setModifiedColor NOTIFY modifiedColorChanged)

    Q_PROPERTY(bool hwAccel READ hwAccel WRITE setHwAccel NOTIFY hwAccelChanged)
    Q_PROPERTY(int previewScale READ previewScale WRITE setPreviewScale NOTIFY previewScaleChanged)
    Q_PROPERTY(int ramCache READ ramCache WRITE setRamCache NOTIFY ramCacheChanged)
    Q_PROPERTY(int threads READ threads WRITE setThreads NOTIFY threadsChanged)
    Q_PROPERTY(bool autoScroll READ autoScroll WRITE setAutoScroll NOTIFY autoScrollChanged)
    Q_PROPERTY(int trackHeight READ trackHeight WRITE setTrackHeight NOTIFY trackHeightChanged)
    Q_PROPERTY(int snapDistance READ snapDistance WRITE setSnapDistance NOTIFY snapDistanceChanged)

    Q_PROPERTY(QString sam2Status READ sam2Status NOTIFY sam2StatusChanged)
    Q_PROPERTY(QString rvmStatus READ rvmStatus NOTIFY rvmStatusChanged)
    Q_PROPERTY(QString whisperStatus READ whisperStatus NOTIFY whisperStatusChanged)

public:
    explicit PreferencesBackend(QObject *parent = nullptr);

    int brightness() const { return m_brightness; }
    void setBrightness(int val);

    int contrast() const { return m_contrast; }
    void setContrast(int val);

    int borders() const { return m_borders; }
    void setBorders(int val);

    int textBrightness() const { return m_textBrightness; }
    void setTextBrightness(int val);

    int panelGaps() const { return m_panelGaps; }
    void setPanelGaps(int val);

    int neutralTone() const { return m_neutralTone; }
    void setNeutralTone(int val);

    QString accentColor() const { return m_accentColor; }
    void setAccentColor(const QString &color);

    QString normalColor() const { return m_normalColor; }
    void setNormalColor(const QString &color);

    QString animatedColor() const { return m_animatedColor; }
    void setAnimatedColor(const QString &color);

    QString animModColor() const { return m_animModColor; }
    void setAnimModColor(const QString &color);

    QString modifiedColor() const { return m_modifiedColor; }
    void setModifiedColor(const QString &color);

    bool hwAccel() const { return m_hwAccel; }
    void setHwAccel(bool enabled);

    int previewScale() const { return m_previewScale; }
    void setPreviewScale(int scale);

    int ramCache() const { return m_ramCache; }
    void setRamCache(int mb);

    int threads() const { return m_threads; }
    void setThreads(int count);

    bool autoScroll() const { return m_autoScroll; }
    void setAutoScroll(bool scroll);

    int trackHeight() const { return m_trackHeight; }
    void setTrackHeight(int height);

    int snapDistance() const { return m_snapDistance; }
    void setSnapDistance(int dist);

    QString sam2Status() const;
    QString rvmStatus() const;
    QString whisperStatus() const;

    Q_INVOKABLE void resetSetting(const QString &key);
    Q_INVOKABLE void suggestFromAccent();
    Q_INVOKABLE void openClassicSettings();
    Q_INVOKABLE void closeDialog();

Q_SIGNALS:
    void brightnessChanged();
    void contrastChanged();
    void bordersChanged();
    void textBrightnessChanged();
    void panelGapsChanged();
    void neutralToneChanged();
    void accentColorChanged();
    void normalColorChanged();
    void animatedColorChanged();
    void animModColorChanged();
    void modifiedColorChanged();
    void hwAccelChanged();
    void previewScaleChanged();
    void ramCacheChanged();
    void threadsChanged();
    void autoScrollChanged();
    void trackHeightChanged();
    void snapDistanceChanged();
    void sam2StatusChanged();
    void rvmStatusChanged();
    void whisperStatusChanged();
    void requestClose();
    void requestClassicSettings();

private:
    void loadSettings();
    void saveSettings();
    void applyAccentColorToApp(const QString &colorHex);

    int m_brightness{42};
    int m_contrast{-70};
    int m_borders{48};
    int m_textBrightness{51};
    int m_panelGaps{10};
    int m_neutralTone{0};

    QString m_accentColor{QStringLiteral("#f59e0b")};
    QString m_normalColor{QStringLiteral("#d97706")};
    QString m_animatedColor{QStringLiteral("#eab308")};
    QString m_animModColor{QStringLiteral("#fca5a5")};
    QString m_modifiedColor{QStringLiteral("#f472b6")};

    bool m_hwAccel{true};
    int m_previewScale{1};
    int m_ramCache{2048};
    int m_threads{8};
    bool m_autoScroll{true};
    int m_trackHeight{50};
    int m_snapDistance{10};
};

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    ~PreferencesDialog() override;

    void setCurrentPage(const QString &pageName);

    static QQuickWidget *createQuickWidget(QWidget *parent, PreferencesBackend *backend, bool showSidebar = true);

private:
    QQuickWidget *m_quickWidget{nullptr};
    PreferencesBackend *m_backend{nullptr};
};
