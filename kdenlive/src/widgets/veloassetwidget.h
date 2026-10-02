/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Velo Stock Media & AI Voiceover Studio Widget
 * Provides unified access to Pexels, Pixabay, Giphy, Freesound, and ElevenLabs
 * text-to-speech via Supabase Edge Functions.
 */

#pragma once

#include <QWidget>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSlider>
#include <QLabel>
#include <QProgressBar>
#include <QTabWidget>
#include <functional>

struct StockAssetItem {
    QString id;
    QString title;
    QString previewUrl;
    QString downloadUrl;
    QString kind; // video, image, sfx, gif
    QString provider; // pexels, pixabay, giphy, freesound
    int width{0};
    int height{0};
    double duration{0.0};
};

/**
 * @class VeloAssetWidget
 * @brief Multi-provider stock media browser and ElevenLabs voiceover studio.
 */
class VeloAssetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VeloAssetWidget(QWidget *parent = nullptr);
    ~VeloAssetWidget() override = default;

    /** @brief Triggers search programmatically (for AI copilot tools) */
    void searchStock(const QString &category, const QString &query, int page = 1);

    /** @brief Generates ElevenLabs voiceover programmatically */
    void generateVoiceover(const QString &text, const QString &voiceNameOrId,
                           double speed = 1.0, double stability = 0.5,
                           std::function<void(const QString &localPath, double duration)> onComplete = nullptr);

    /** @brief Downloads a media URL to local storage, imports to Bin, and optionally inserts to timeline */
    void downloadAndIngest(const QString &url, const QString &name, const QString &kind,
                           bool insertToTimeline = false, int trackId = -1, int targetFrame = -1,
                           std::function<void(const QString &clipId, const QString &localPath)> onComplete = nullptr);

Q_SIGNALS:
    void assetAddedToBin(const QString &clipId, const QString &localPath);
    void assetInsertedToTimeline(const QString &clipId, int trackId, int frame);
    void statusMessage(const QString &msg);

private Q_SLOTS:
    void slotSearchClicked();
    void slotCategoryChanged(int index);
    void slotGenerateVoiceClicked();
    void slotAssetDoubleClicked(QListWidgetItem *item);
    void slotAssetSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void slotAddSelectedToBin();
    void slotInsertSelectedToTimeline();

private:
    void setupUi();
    void loadEnvCredentials();
    void fetchAvailableVoices();
    void parseAndDisplaySearchResults(const QByteArray &data, const QString &category);
    void parseAndDisplayVoices(const QByteArray &data);
    void fetchThumbnailAsync(const QString &assetId, const QString &thumbUrl, QListWidgetItem *item);
    QNetworkRequest createSupabaseRequest(const QString &functionPath) const;

    QTabWidget *m_tabs{nullptr};

    // Stock Browser Tab
    QLineEdit *m_searchEdit{nullptr};
    QComboBox *m_categoryCombo{nullptr};
    QPushButton *m_searchBtn{nullptr};
    QListWidget *m_resultsList{nullptr};
    QLabel *m_stockStatusLabel{nullptr};

    // Asset Preview Panel
    QFrame *m_previewPanel{nullptr};
    QLabel *m_previewImageLabel{nullptr};
    QLabel *m_previewTitleLabel{nullptr};
    QLabel *m_previewDetailsLabel{nullptr};
    QPushButton *m_addToBinBtn{nullptr};
    QPushButton *m_insertTimelineBtn{nullptr};

    // Voice Studio Tab
    QComboBox *m_voiceCombo{nullptr};
    QPlainTextEdit *m_voiceTextEdit{nullptr};
    QSlider *m_speedSlider{nullptr};
    QSlider *m_stabilitySlider{nullptr};
    QLabel *m_speedLabel{nullptr};
    QLabel *m_stabilityLabel{nullptr};
    QPushButton *m_generateVoiceBtn{nullptr};
    QProgressBar *m_voiceProgressBar{nullptr};
    QLabel *m_voiceStatusLabel{nullptr};

    QNetworkAccessManager *m_nam{nullptr};
    QString m_supabaseUrl;
    QString m_supabaseAnonKey;
    QString m_supabaseServiceKey;

    QList<StockAssetItem> m_currentAssets;
    QMap<QString, QString> m_voiceMap; // Name -> ID
};
