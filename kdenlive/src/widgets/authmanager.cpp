/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 */

#include "authmanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QTimer>
#include <QStandardPaths>
#include <QSettings>
#include <QCoreApplication>
#include <QDebug>

AuthManager *AuthManager::s_instance = nullptr;

AuthManager *AuthManager::instance()
{
    if (!s_instance) {
        s_instance = new AuthManager(qApp);
    }
    return s_instance;
}

AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    loadCredentialsFromEnv();
    loadPersistedSession();
}

void AuthManager::loadCredentialsFromEnv()
{
    QStringList searchPaths;
    searchPaths << QDir::current().filePath(QStringLiteral(".env.local"))
                << QDir::current().filePath(QStringLiteral(".env"))
                << QDir::current().filePath(QStringLiteral("kdenlive/.env.local"))
                << QDir::current().filePath(QStringLiteral("kdenlive/.env"))
                << QCoreApplication::applicationDirPath() + QStringLiteral("/.env.local")
                << QCoreApplication::applicationDirPath() + QStringLiteral("/.env")
                << QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).filePath(QStringLiteral(".env.local"))
                << QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).filePath(QStringLiteral(".env"))
                << QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral(".env.local"))
                << QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral(".env"))
                << QStandardPaths::locate(QStandardPaths::ConfigLocation, QStringLiteral("kdenlive/.env.local"))
                << QStandardPaths::locate(QStandardPaths::ConfigLocation, QStringLiteral("kdenlive/.env"))
                << QDir::home().filePath(QStringLiteral(".config/kdenlive/.env.local"))
                << QDir::home().filePath(QStringLiteral(".config/kdenlive/.env"));

    for (const QString &path : searchPaths) {
        QFile file(path);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

                int eqIdx = line.indexOf(QLatin1Char('='));
                if (eqIdx <= 0) continue;

                QString key = line.left(eqIdx).trimmed();
                QString val = line.mid(eqIdx + 1).trimmed();
                if ((val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"'))) ||
                    (val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\'')))) {
                    val = val.mid(1, val.length() - 2).trimmed();
                }

                if (key == QStringLiteral("VITE_SUPABASE_URL") || key == QStringLiteral("SUPABASE_URL")) {
                    m_supabaseUrl = val;
                } else if (key == QStringLiteral("VITE_SUPABASE_ANON_KEY") || key == QStringLiteral("SUPABASE_ANON_KEY")) {
                    m_supabaseAnonKey = val;
                }
            }
            if (!m_supabaseUrl.isEmpty() && !m_supabaseAnonKey.isEmpty()) {
                qDebug() << "[AuthManager] Supabase credentials loaded from:" << path;
                break;
            }
        }
    }

    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        qDebug() << "[AuthManager] Supabase credentials not found in environment search paths.";
    }
}

void AuthManager::setSupabaseCredentials(const QString &url, const QString &anonKey)
{
    m_supabaseUrl = url;
    m_supabaseAnonKey = anonKey;
}

bool AuthManager::isLoggedIn() const
{
    return !m_accessToken.isEmpty();
}

QString AuthManager::accessToken() const
{
    return m_accessToken;
}

QString AuthManager::refreshToken() const
{
    return m_refreshToken;
}

QString AuthManager::userEmail() const
{
    return m_userEmail;
}

QString AuthManager::userId() const
{
    return m_userId;
}

void AuthManager::signInWithEmail(const QString &email, const QString &password)
{
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        loadCredentialsFromEnv();
    }
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        Q_EMIT authError(QStringLiteral("Supabase credentials not configured."));
        return;
    }

    QUrl url(QStringLiteral("%1/auth/v1/token?grant_type=password").arg(m_supabaseUrl));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("apikey", m_supabaseAnonKey.toUtf8());

    QJsonObject body;
    body[QStringLiteral("email")] = email.trimmed();
    body[QStringLiteral("password")] = password;

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray respData = reply->readAll();

        if (reply->error() == QNetworkReply::NoError) {
            handleAuthResponse(respData, false);
        } else {
            QJsonDocument doc = QJsonDocument::fromJson(respData);
            QString errMsg;
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj.contains(QStringLiteral("error_description"))) {
                    errMsg = obj[QStringLiteral("error_description")].toString();
                } else if (obj.contains(QStringLiteral("msg"))) {
                    errMsg = obj[QStringLiteral("msg")].toString();
                } else if (obj.contains(QStringLiteral("message"))) {
                    errMsg = obj[QStringLiteral("message")].toString();
                }
            }
            if (errMsg.isEmpty()) {
                errMsg = reply->errorString();
            }
            qWarning() << "[AuthManager] Sign in failed:" << errMsg;
            Q_EMIT authError(errMsg);
        }
    });
}

void AuthManager::signUpWithEmail(const QString &email, const QString &password)
{
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        loadCredentialsFromEnv();
    }
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        Q_EMIT authError(QStringLiteral("Supabase credentials not configured."));
        return;
    }

    QUrl url(QStringLiteral("%1/auth/v1/signup").arg(m_supabaseUrl));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("apikey", m_supabaseAnonKey.toUtf8());

    QJsonObject body;
    body[QStringLiteral("email")] = email.trimmed();
    body[QStringLiteral("password")] = password;

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray respData = reply->readAll();

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(respData);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                // Check if session was returned immediately (email confirmation disabled)
                if (obj.contains(QStringLiteral("access_token"))) {
                    handleAuthResponse(respData, true);
                } else {
                    QString msg = QStringLiteral("Registration successful! Please check your email to confirm your account.");
                    Q_EMIT signUpSuccess(msg);
                }
            }
        } else {
            QJsonDocument doc = QJsonDocument::fromJson(respData);
            QString errMsg;
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj.contains(QStringLiteral("error_description"))) {
                    errMsg = obj[QStringLiteral("error_description")].toString();
                } else if (obj.contains(QStringLiteral("msg"))) {
                    errMsg = obj[QStringLiteral("msg")].toString();
                } else if (obj.contains(QStringLiteral("message"))) {
                    errMsg = obj[QStringLiteral("message")].toString();
                }
            }
            if (errMsg.isEmpty()) {
                errMsg = reply->errorString();
            }
            qWarning() << "[AuthManager] Sign up failed:" << errMsg;
            Q_EMIT authError(errMsg);
        }
    });
}

void AuthManager::signInWithWebPortal()
{
    if (m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        loadCredentialsFromEnv();
    }

    const QString callbackUri = QStringLiteral("velo://auth/callback");
    QString portalUrl = QStringLiteral("%1/desktop/auth/authorize?client_id=velo-desktop&redirect_uri=%2")
                            .arg(m_webAppUrl, QUrl::toPercentEncoding(callbackUri));

    // Try pre-creating transaction for real-time polling
    QUrl authUrl(QStringLiteral("%1/api/desktop/auth/authorize").arg(m_webAppUrl));
    QNetworkRequest req(authUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body[QStringLiteral("client_id")] = QStringLiteral("velo-desktop");
    body[QStringLiteral("redirect_uri")] = callbackUri;

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, portalUrl]() {
        reply->deleteLater();
        QString finalPortalUrl = portalUrl;
        QString txnId;

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                txnId = obj[QStringLiteral("transaction_id")].toString();
                if (obj.contains(QStringLiteral("authorize_url"))) {
                    finalPortalUrl = obj[QStringLiteral("authorize_url")].toString();
                } else if (!txnId.isEmpty()) {
                    finalPortalUrl = QStringLiteral("%1/desktop/auth/authorize?txn=%2")
                                         .arg(m_webAppUrl, QUrl::toPercentEncoding(txnId));
                }
            }
        }

        // Open browser
        qDebug() << "[AuthManager] Opening web auth portal:" << finalPortalUrl;
        QDesktopServices::openUrl(QUrl(finalPortalUrl));
        Q_EMIT webAuthStarted(finalPortalUrl);

        if (!txnId.isEmpty()) {
            startPollingForDesktopAuth(txnId);
        }
    });
}

void AuthManager::startPollingForDesktopAuth(const QString &txnId)
{
    if (m_pollTimer) {
        m_pollTimer->stop();
        m_pollTimer->deleteLater();
        m_pollTimer = nullptr;
    }

    m_pollTimer = new QTimer(this);
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    const qint64 timeoutMs = 5 * 60 * 1000; // 5 minute max timeout

    connect(m_pollTimer, &QTimer::timeout, this, [this, txnId, startTime]() {
        if (QDateTime::currentMSecsSinceEpoch() - startTime > timeoutMs || isLoggedIn()) {
            if (m_pollTimer) {
                m_pollTimer->stop();
                m_pollTimer->deleteLater();
                m_pollTimer = nullptr;
            }
            return;
        }

        QUrl pollUrl(QStringLiteral("%1/api/desktop/auth/status?transaction_id=%2")
                         .arg(m_webAppUrl, QUrl::toPercentEncoding(txnId)));
        QNetworkRequest req(pollUrl);

        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    QString status = obj[QStringLiteral("status")].toString();
                    QString oneTimeCode = obj[QStringLiteral("one_time_code")].toString();

                    if (status == QStringLiteral("authorized") && !oneTimeCode.isEmpty()) {
                        if (m_pollTimer) {
                            m_pollTimer->stop();
                            m_pollTimer->deleteLater();
                            m_pollTimer = nullptr;
                        }
                        exchangeWebAuthCode(oneTimeCode);
                    } else if (status == QStringLiteral("expired") || status == QStringLiteral("revoked")) {
                        if (m_pollTimer) {
                            m_pollTimer->stop();
                            m_pollTimer->deleteLater();
                            m_pollTimer = nullptr;
                        }
                    }
                }
            }
        });
    });

    m_pollTimer->start(1500); // Poll every 1.5s
}

void AuthManager::exchangeWebAuthCode(const QString &code)
{
    QUrl exchangeUrl(QStringLiteral("%1/api/desktop/auth/exchange").arg(m_webAppUrl));
    QNetworkRequest req(exchangeUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body[QStringLiteral("code")] = code;

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            handleAuthResponse(data, false);
        } else {
            qWarning() << "[AuthManager] Code exchange failed:" << reply->errorString();
            Q_EMIT authError(QStringLiteral("Web authentication exchange failed."));
        }
    });
}

void AuthManager::handleOAuthCallbackUrl(const QString &urlStr)
{
    QString normalized = urlStr;
    normalized.replace(QLatin1Char('#'), QLatin1Char('?'));
    QUrl url(normalized);
    QUrlQuery query(url);

    QString code = query.queryItemValue(QStringLiteral("code"));
    QString access = query.queryItemValue(QStringLiteral("access_token"));
    QString refresh = query.queryItemValue(QStringLiteral("refresh_token"));

    if (!code.isEmpty()) {
        exchangeWebAuthCode(code);
    } else if (!access.isEmpty() && !refresh.isEmpty()) {
        QJsonObject sessionObj;
        sessionObj[QStringLiteral("access_token")] = access;
        sessionObj[QStringLiteral("refresh_token")] = refresh;
        sessionObj[QStringLiteral("expires_in")] = 3600;
        handleAuthResponse(QJsonDocument(sessionObj).toJson(), false);
    }
}

void AuthManager::refreshSession()
{
    if (m_refreshToken.isEmpty() || m_supabaseUrl.isEmpty() || m_supabaseAnonKey.isEmpty()) {
        return;
    }

    QUrl url(QStringLiteral("%1/auth/v1/token?grant_type=refresh_token").arg(m_supabaseUrl));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("apikey", m_supabaseAnonKey.toUtf8());

    QJsonObject body;
    body[QStringLiteral("refresh_token")] = m_refreshToken;

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            handleAuthResponse(reply->readAll(), false);
        } else {
            qWarning() << "[AuthManager] Token refresh failed, clearing session.";
            signOut();
        }
    });
}

void AuthManager::signOut()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_userEmail.clear();
    m_userId.clear();
    m_expiresAt = QDateTime();

    clearPersistedSession();
    Q_EMIT authStateChanged(false, QString());
}

void AuthManager::handleAuthResponse(const QByteArray &data, bool isSignUp)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        Q_EMIT authError(QStringLiteral("Invalid auth response from server."));
        return;
    }

    QJsonObject root = doc.object();
    m_accessToken = root[QStringLiteral("access_token")].toString();
    m_refreshToken = root[QStringLiteral("refresh_token")].toString();

    int expiresIn = root[QStringLiteral("expires_in")].toInt(3600);
    m_expiresAt = QDateTime::currentDateTime().addSecs(expiresIn);

    if (root.contains(QStringLiteral("user"))) {
        QJsonObject userObj = root[QStringLiteral("user")].toObject();
        m_userId = userObj[QStringLiteral("id")].toString();
        m_userEmail = userObj[QStringLiteral("email")].toString();
    }

    savePersistedSession();

    qDebug() << "[AuthManager] Authentication successful for user:" << m_userEmail;
    Q_EMIT authSuccess(m_userEmail);
    Q_EMIT authStateChanged(true, m_userEmail);

    if (isSignUp) {
        Q_EMIT signUpSuccess(QStringLiteral("Account created and signed in successfully!"));
    }
}

void AuthManager::savePersistedSession()
{
    QSettings settings(QStringLiteral("VidMate"), QStringLiteral("VidMateAuth"));
    settings.setValue(QStringLiteral("access_token"), m_accessToken);
    settings.setValue(QStringLiteral("refresh_token"), m_refreshToken);
    settings.setValue(QStringLiteral("user_email"), m_userEmail);
    settings.setValue(QStringLiteral("user_id"), m_userId);
    settings.setValue(QStringLiteral("expires_at"), m_expiresAt.toSecsSinceEpoch());
}

void AuthManager::loadPersistedSession()
{
    QSettings settings(QStringLiteral("VidMate"), QStringLiteral("VidMateAuth"));
    m_accessToken = settings.value(QStringLiteral("access_token")).toString();
    m_refreshToken = settings.value(QStringLiteral("refresh_token")).toString();
    m_userEmail = settings.value(QStringLiteral("user_email")).toString();
    m_userId = settings.value(QStringLiteral("user_id")).toString();

    qint64 expSecs = settings.value(QStringLiteral("expires_at")).toLongLong();
    if (expSecs > 0) {
        m_expiresAt = QDateTime::fromSecsSinceEpoch(expSecs);
    }

    if (!m_accessToken.isEmpty()) {
        // If token has expired or is expiring in less than 5 minutes, refresh it
        if (m_expiresAt.isValid() && m_expiresAt <= QDateTime::currentDateTime().addSecs(300)) {
            refreshSession();
        } else {
            qDebug() << "[AuthManager] Restored active session for user:" << m_userEmail;
            Q_EMIT authStateChanged(true, m_userEmail);
        }
    }
}

void AuthManager::clearPersistedSession()
{
    QSettings settings(QStringLiteral("VidMate"), QStringLiteral("VidMateAuth"));
    settings.remove(QStringLiteral("access_token"));
    settings.remove(QStringLiteral("refresh_token"));
    settings.remove(QStringLiteral("user_email"));
    settings.remove(QStringLiteral("user_id"));
    settings.remove(QStringLiteral("expires_at"));
}
