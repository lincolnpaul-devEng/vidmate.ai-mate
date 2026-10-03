/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>

/**
 * @class AuthManager
 * @brief Manages Supabase user authentication, session persistence, and token lifecycle.
 *
 * Provides singleton access to current user credentials, ensuring all AI proxy calls
 * include valid user JWT authorization headers.
 */
class AuthManager : public QObject
{
    Q_OBJECT

public:
    static AuthManager *instance();

    bool isLoggedIn() const;
    QString accessToken() const;
    QString refreshToken() const;
    QString userEmail() const;
    QString userId() const;
    QString supabaseUrl() const { return m_supabaseUrl; }
    QString supabaseAnonKey() const { return m_supabaseAnonKey; }

    void signInWithEmail(const QString &email, const QString &password);
    void signUpWithEmail(const QString &email, const QString &password);
    void signInWithWebPortal();
    void handleOAuthCallbackUrl(const QString &url);
    void signOut();
    void refreshSession();

    void setSupabaseCredentials(const QString &url, const QString &anonKey);
    QString webAppUrl() const { return m_webAppUrl; }

Q_SIGNALS:
    void authStateChanged(bool isLoggedIn, const QString &userEmail);
    void authError(const QString &errorMessage);
    void authSuccess(const QString &userEmail);
    void signUpSuccess(const QString &message);
    void webAuthStarted(const QString &portalUrl);

private:
    explicit AuthManager(QObject *parent = nullptr);
    ~AuthManager() override = default;

    void loadCredentialsFromEnv();
    void loadPersistedSession();
    void savePersistedSession();
    void clearPersistedSession();
    void handleAuthResponse(const QByteArray &data, bool isSignUp = false);
    void startPollingForDesktopAuth(const QString &txnId);
    void exchangeWebAuthCode(const QString &code);

    static AuthManager *s_instance;

    QNetworkAccessManager *m_nam{nullptr};
    QTimer *m_pollTimer{nullptr};
    QString m_supabaseUrl;
    QString m_supabaseAnonKey;
    QString m_webAppUrl{QStringLiteral("https://velo.tedoraltd.com")};

    QString m_accessToken;
    QString m_refreshToken;
    QString m_userEmail;
    QString m_userId;
    QDateTime m_expiresAt;
};
