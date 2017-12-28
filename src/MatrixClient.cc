/*
 * nheko Copyright (C) 2017  Konstantinos Sideris <siderisk@auth.gr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QDebug>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSettings>
#include <QUrlQuery>

#include "Login.h"
#include "MatrixClient.h"
#include "Register.h"

MatrixClient::MatrixClient(QString server, QObject *parent)
  : QNetworkAccessManager(parent)
  , clientApiUrl_{"/_matrix/client/r0"}
  , mediaApiUrl_{"/_matrix/media/r0"}
  , server_{"https://" + server}
{
        QSettings settings;
        txn_id_ = settings.value("client/transaction_id", 1).toInt();

        connect(this,
                &QNetworkAccessManager::networkAccessibleChanged,
                this,
                [=](NetworkAccessibility status) {
                        if (status != NetworkAccessibility::Accessible)
                                setNetworkAccessible(NetworkAccessibility::Accessible);
                });
}

void
MatrixClient::reset() noexcept
{
        next_batch_.clear();
        server_.clear();
        token_.clear();

        txn_id_ = 0;
}

void
MatrixClient::login(const QString &username, const QString &password) noexcept
{
        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + "/login");

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        LoginRequest body(username, password);

        auto reply = post(request, body.serialize());
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status_code =
                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status_code == 403) {
                        emit loginError(tr("Wrong username or password"));
                        return;
                }

                if (status_code == 404) {
                        emit loginError(tr("Login endpoint was not found on the server"));
                        return;
                }

                if (status_code >= 400) {
                        qWarning() << "Login error: " << reply->errorString();
                        emit loginError(tr("An unknown error occured. Please try again."));
                        return;
                }

                try {
                        mtx::responses::Login login =
                          nlohmann::json::parse(reply->readAll().data());

                        auto hostname = server_.host();

                        if (server_.port() > 0)
                                hostname = QString("%1:%2").arg(server_.host()).arg(server_.port());

                        emit loginSuccess(QString::fromStdString(login.user_id.toString()),
                                          hostname,
                                          QString::fromStdString(login.access_token));
                } catch (std::exception &e) {
                        qWarning() << "Malformed JSON response" << e.what();
                        emit loginError(tr("Malformed response. Possibly not a Matrix server"));
                }
        });
}

void
MatrixClient::logout() noexcept
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + "/logout");
        endpoint.setQuery(query);

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject body{};
        auto reply = post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status != 200) {
                        qWarning() << "Logout error: " << reply->errorString();
                        return;
                }

                emit loggedOut();
        });
}

void
MatrixClient::registerUser(const QString &user, const QString &pass, const QString &server) noexcept
{
        setServer(server);

        QUrlQuery query;
        query.addQueryItem("kind", "user");

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + "/register");
        endpoint.setQuery(query);

        QNetworkRequest request(QString(endpoint.toEncoded()));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        RegisterRequest body(user, pass);
        auto reply = post(request, body.serialize());

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                auto data = reply->readAll();
                auto json = QJsonDocument::fromJson(data);

                if (status == 0 || status >= 400) {
                        if (json.isObject() && json.object().contains("error"))
                                emit registerError(json.object().value("error").toString());
                        else
                                emit registerError(reply->errorString());

                        return;
                }

                RegisterResponse response;

                try {
                        response.deserialize(json);
                        emit registerSuccess(response.getUserId(),
                                             response.getHomeServer(),
                                             response.getAccessToken());
                } catch (DeserializationException &e) {
                        qWarning() << "Register" << e.what();
                        emit registerError("Received malformed response.");
                }
        });
}

void
MatrixClient::sync() noexcept
{
        QJsonObject filter{
          {"room",
           QJsonObject{
             {"include_leave", true},
           }},
        };

        QUrlQuery query;
        query.addQueryItem("set_presence", "online");
        query.addQueryItem("filter", QJsonDocument(filter).toJson(QJsonDocument::Compact));
        query.addQueryItem("timeout", "15000");
        query.addQueryItem("access_token", token_);

        if (next_batch_.isEmpty()) {
                qDebug() << "Sync requires a valid next_batch token. Initial sync should "
                            "be performed.";
                return;
        }

        query.addQueryItem("since", next_batch_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + "/sync");
        endpoint.setQuery(query);

        QNetworkRequest request(QString(endpoint.toEncoded()));
        request.setRawHeader("Connection", "keep-alive");

        auto reply = get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        emit syncFailed(reply->errorString());
                        return;
                }

                try {
                        mtx::responses::Sync response = nlohmann::json::parse(reply->readAll());
                        emit syncCompleted(response);
                } catch (std::exception &e) {
                        qWarning() << "Sync malformed response: " << e.what();
                }
        });
}

void
MatrixClient::sendRoomMessage(mtx::events::MessageType ty,
                              int txnId,
                              const QString &roomid,
                              const QString &msg,
                              const QFileInfo &fileinfo,
                              const QString &url) noexcept
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ +
                         QString("/rooms/%1/send/m.room.message/%2").arg(roomid).arg(txnId));
        endpoint.setQuery(query);

        QString msgType("");

        QMimeDatabase db;
        QMimeType mime =
          db.mimeTypeForFile(fileinfo.absoluteFilePath(), QMimeDatabase::MatchContent);

        QJsonObject body;
        QJsonObject info = {{"size", fileinfo.size()}, {"mimetype", mime.name()}};

        switch (ty) {
        case mtx::events::MessageType::Text:
                body = {{"msgtype", "m.text"}, {"body", msg}};
                break;
        case mtx::events::MessageType::Emote:
                body = {{"msgtype", "m.emote"}, {"body", msg}};
                break;
        case mtx::events::MessageType::Image:
                body = {{"msgtype", "m.image"}, {"body", msg}, {"url", url}, {"info", info}};
                break;
        case mtx::events::MessageType::File:
                body = {{"msgtype", "m.file"}, {"body", msg}, {"url", url}, {"info", info}};
                break;
        case mtx::events::MessageType::Audio:
                body = {{"msgtype", "m.audio"}, {"body", msg}, {"url", url}, {"info", info}};
                break;
        default:
                qDebug() << "SendRoomMessage: Unknown message type for" << msg;
                return;
        }

        QNetworkRequest request(QString(endpoint.toEncoded()));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        auto reply = put(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

        connect(reply, &QNetworkReply::finished, this, [this, reply, roomid, txnId]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        emit messageSendFailed(roomid, txnId);
                        return;
                }

                auto data = reply->readAll();

                if (data.isEmpty()) {
                        emit messageSendFailed(roomid, txnId);
                        return;
                }

                auto json = QJsonDocument::fromJson(data);

                if (!json.isObject()) {
                        qDebug() << "Send message response is not a JSON object";
                        emit messageSendFailed(roomid, txnId);
                        return;
                }

                auto object = json.object();

                if (!object.contains("event_id")) {
                        qDebug() << "SendTextMessage: missing event_id from response";
                        emit messageSendFailed(roomid, txnId);
                        return;
                }

                emit messageSent(object.value("event_id").toString(), roomid, txnId);
        });
}

void
MatrixClient::initialSync() noexcept
{
        QUrlQuery query;
        query.addQueryItem("timeout", "0");
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + "/sync");
        endpoint.setQuery(query);

        QNetworkRequest request(QString(endpoint.toEncoded()));
        request.setRawHeader("Connection", "keep-alive");

        auto reply = get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        emit initialSyncFailed(reply->errorString());
                        return;
                }

                try {
                        mtx::responses::Sync response = nlohmann::json::parse(reply->readAll());
                        emit initialSyncCompleted(response);
                } catch (std::exception &e) {
                        qWarning() << "Sync malformed response" << e.what();
                        return;
                }

        });
}

void
MatrixClient::versions() noexcept
{
        QUrl endpoint(server_);
        endpoint.setPath("/_matrix/client/versions");

        QNetworkRequest request(endpoint);

        auto reply = get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status_code =
                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status_code == 404) {
                        emit versionError("Versions endpoint was not found on the server. Possibly "
                                          "not a Matrix server");
                        return;
                }

                if (status_code >= 400) {
                        emit versionError("An unknown error occured. Please try again.");
                        return;
                }

                try {
                        mtx::responses::Versions versions =
                          nlohmann::json::parse(reply->readAll().data());

                        emit versionSuccess();
                } catch (std::exception &e) {
                        emit versionError("Malformed response. Possibly not a Matrix server");
                }
        });
}

void
MatrixClient::getOwnProfile() noexcept
{
        // FIXME: Remove settings from the matrix client. The class should store the
        // user's matrix ID.
        QSettings settings;
        auto userid = settings.value("auth/user_id", "").toString();

        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + "/profile/" + userid);
        endpoint.setQuery(query);

        QNetworkRequest request(QString(endpoint.toEncoded()));

        QNetworkReply *reply = get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status >= 400) {
                        qWarning() << reply->errorString();
                        return;
                }

                try {
                        mtx::responses::Profile profile =
                          nlohmann::json::parse(reply->readAll().data());

                        emit getOwnProfileResponse(QUrl(QString::fromStdString(profile.avatar_url)),
                                                   QString::fromStdString(profile.display_name));
                } catch (std::exception &e) {
                        qWarning() << "Profile:" << e.what();
                }
        });
}

void
MatrixClient::fetchRoomAvatar(const QString &roomid, const QUrl &avatar_url)
{
        QList<QString> url_parts = avatar_url.toString().split("mxc://");

        if (url_parts.size() != 2) {
                qDebug() << "Invalid format for room avatar " << avatar_url.toString();
                return;
        }

        QUrlQuery query;
        query.addQueryItem("width", "512");
        query.addQueryItem("height", "512");
        query.addQueryItem("method", "crop");

        QString media_url =
          QString("%1/_matrix/media/r0/thumbnail/%2").arg(getHomeServer().toString(), url_parts[1]);

        QUrl endpoint(media_url);
        endpoint.setQuery(query);

        QNetworkRequest avatar_request(endpoint);

        QNetworkReply *reply = get(avatar_request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, roomid, avatar_url]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        qWarning() << reply->errorString();
                        return;
                }

                auto img = reply->readAll();

                if (img.size() == 0)
                        return;

                QPixmap pixmap;
                pixmap.loadFromData(img);

                emit roomAvatarRetrieved(roomid, pixmap, avatar_url.toString(), img);
        });
}

void
MatrixClient::fetchUserAvatar(const QString &userId, const QUrl &avatarUrl)
{
        QList<QString> url_parts = avatarUrl.toString().split("mxc://");

        if (url_parts.size() != 2) {
                qDebug() << "Invalid format for user avatar " << avatarUrl.toString();
                return;
        }

        QUrlQuery query;
        query.addQueryItem("width", "128");
        query.addQueryItem("height", "128");
        query.addQueryItem("method", "crop");

        QString media_url =
          QString("%1/_matrix/media/r0/thumbnail/%2").arg(getHomeServer().toString(), url_parts[1]);

        QUrl endpoint(media_url);
        endpoint.setQuery(query);

        QNetworkRequest avatar_request(endpoint);

        auto reply = get(avatar_request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, userId]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        qWarning() << reply->errorString();
                        return;
                }

                auto data = reply->readAll();

                if (data.size() == 0)
                        return;

                QImage img;
                img.loadFromData(data);

                emit userAvatarRetrieved(userId, img);
        });
}

void
MatrixClient::downloadImage(const QString &event_id, const QUrl &url)
{
        QNetworkRequest image_request(url);

        auto reply = get(image_request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, event_id]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        qWarning() << reply->errorString();
                        return;
                }

                auto img = reply->readAll();

                if (img.size() == 0)
                        return;

                QPixmap pixmap;
                pixmap.loadFromData(img);

                emit imageDownloaded(event_id, pixmap);
        });
}

void
MatrixClient::downloadFile(const QString &event_id, const QUrl &url)
{
        QNetworkRequest fileRequest(url);

        auto reply = get(fileRequest);
        connect(reply, &QNetworkReply::finished, this, [this, reply, event_id]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        // TODO: Handle error
                        qWarning() << reply->errorString();
                        return;
                }

                auto data = reply->readAll();

                if (data.size() == 0)
                        return;

                emit fileDownloaded(event_id, data);
        });
}

void
MatrixClient::fetchOwnAvatar(const QUrl &avatar_url)
{
        QList<QString> url_parts = avatar_url.toString().split("mxc://");

        if (url_parts.size() != 2) {
                qDebug() << "Invalid format for media " << avatar_url.toString();
                return;
        }

        QUrlQuery query;
        query.addQueryItem("width", "512");
        query.addQueryItem("height", "512");
        query.addQueryItem("method", "crop");

        QString media_url =
          QString("%1/_matrix/media/r0/thumbnail/%2").arg(getHomeServer().toString(), url_parts[1]);

        QUrl endpoint(media_url);
        endpoint.setQuery(query);

        QNetworkRequest avatar_request(endpoint);

        auto reply = get(avatar_request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        qWarning() << reply->errorString();
                        return;
                }

                auto img = reply->readAll();

                if (img.size() == 0)
                        return;

                QPixmap pixmap;
                pixmap.loadFromData(img);

                emit ownAvatarRetrieved(pixmap);
        });
}

void
MatrixClient::messages(const QString &roomid, const QString &from_token, int limit) noexcept
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);
        query.addQueryItem("from", from_token);
        query.addQueryItem("dir", "b");
        query.addQueryItem("limit", QString::number(limit));

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + QString("/rooms/%1/messages").arg(roomid));
        endpoint.setQuery(query);

        QNetworkRequest request(QString(endpoint.toEncoded()));

        auto reply = get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, roomid]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        qWarning() << reply->errorString();
                        return;
                }

                try {
                        mtx::responses::Messages messages =
                          nlohmann::json::parse(reply->readAll().data());

                        emit messagesRetrieved(roomid, messages);
                } catch (std::exception &e) {
                        qWarning() << "Room messages from" << roomid << e.what();
                        return;
                }
        });
}

void
MatrixClient::uploadImage(const QString &roomid,
                          const QSharedPointer<QIODevice> data,
                          const QString &filename)
{
        auto reply = makeUploadRequest(data);

        if (reply == nullptr)
                return;

        connect(reply, &QNetworkReply::finished, this, [this, reply, roomid, data, filename]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        emit syncFailed(reply->errorString());
                        return;
                }

                auto res_data = reply->readAll();

                if (res_data.isEmpty())
                        return;

                auto json = QJsonDocument::fromJson(res_data);

                if (!json.isObject()) {
                        qDebug() << "Media upload: Response is not a json object.";
                        return;
                }

                QJsonObject object = json.object();
                if (!object.contains("content_uri")) {
                        qDebug() << "Media upload: Missing content_uri key";
                        qDebug() << object;
                        return;
                }

                emit imageUploaded(roomid, data, filename, object.value("content_uri").toString());
        });
}

void
MatrixClient::uploadFile(const QString &roomid,
                         const QSharedPointer<QIODevice> data,
                         const QString &filename)
{
        auto reply = makeUploadRequest(data);

        connect(reply, &QNetworkReply::finished, this, [this, reply, roomid, filename]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        emit syncFailed(reply->errorString());
                        return;
                }

                auto data = reply->readAll();

                if (data.isEmpty())
                        return;

                auto json = QJsonDocument::fromJson(data);

                if (!json.isObject()) {
                        qDebug() << "Media upload: Response is not a json object.";
                        return;
                }

                QJsonObject object = json.object();
                if (!object.contains("content_uri")) {
                        qDebug() << "Media upload: Missing content_uri key";
                        qDebug() << object;
                        return;
                }

                emit fileUploaded(roomid, filename, object.value("content_uri").toString());
        });
}

void
MatrixClient::uploadAudio(const QString &roomid,
                          const QSharedPointer<QIODevice> data,
                          const QString &filename)
{
        auto reply = makeUploadRequest(data);

        connect(reply, &QNetworkReply::finished, this, [this, reply, roomid, filename]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        emit syncFailed(reply->errorString());
                        return;
                }

                auto data = reply->readAll();

                if (data.isEmpty())
                        return;

                auto json = QJsonDocument::fromJson(data);

                if (!json.isObject()) {
                        qDebug() << "Media upload: Response is not a json object.";
                        return;
                }

                QJsonObject object = json.object();
                if (!object.contains("content_uri")) {
                        qDebug() << "Media upload: Missing content_uri key";
                        qDebug() << object;
                        return;
                }

                emit audioUploaded(roomid, filename, object.value("content_uri").toString());
        });
}

void
MatrixClient::joinRoom(const QString &roomIdOrAlias)
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + QString("/join/%1").arg(roomIdOrAlias));
        endpoint.setQuery(query);

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader, "application/json");

        auto reply = post(request, "{}");
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        auto data     = reply->readAll();
                        auto response = QJsonDocument::fromJson(data);
                        auto json     = response.object();

                        if (json.contains("error"))
                                emit joinFailed(json["error"].toString());
                        else
                                qDebug() << reply->errorString();

                        return;
                }

                auto data     = reply->readAll();
                auto response = QJsonDocument::fromJson(data);
                auto room_id  = response.object()["room_id"].toString();

                emit joinedRoom(room_id);
        });
}

void
MatrixClient::leaveRoom(const QString &roomId)
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + QString("/rooms/%1/leave").arg(roomId));
        endpoint.setQuery(query);

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader, "application/json");

        auto reply = post(request, "{}");

        connect(reply, &QNetworkReply::finished, this, [this, reply, roomId]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        qWarning() << reply->errorString();
                        return;
                }

                emit leftRoom(roomId);
        });
}

void
MatrixClient::inviteUser(const QString &roomId, const QString &user)
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + QString("/rooms/%1/invite").arg(roomId));
        endpoint.setQuery(query);

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader, "application/json");

        QJsonObject body{{"user_id", user}};
        auto reply = post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

        connect(reply, &QNetworkReply::finished, this, [this, reply, roomId, user]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        // TODO: Handle failure.
                        qWarning() << reply->errorString();
                        return;
                }

                emit invitedUser(roomId, user);
        });
}

void
MatrixClient::createRoom(const mtx::requests::CreateRoom &create_room_request)
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + QString("/createRoom"));
        endpoint.setQuery(query);

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader, "application/json");

        nlohmann::json body = create_room_request;
        auto reply          = post(request, QString::fromStdString(body.dump()).toUtf8());

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        auto data     = reply->readAll();
                        auto response = QJsonDocument::fromJson(data);
                        auto json     = response.object();

                        if (json.contains("error"))
                                emit roomCreationFailed(json["error"].toString());
                        else
                                qDebug() << reply->errorString();

                        return;
                }

                auto data     = reply->readAll();
                auto response = QJsonDocument::fromJson(data);
                auto room_id  = response.object()["room_id"].toString();

                emit roomCreated(room_id);
        });
}

void
MatrixClient::sendTypingNotification(const QString &roomid, int timeoutInMillis)
{
        QSettings settings;
        QString user_id = settings.value("auth/user_id").toString();

        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + QString("/rooms/%1/typing/%2").arg(roomid).arg(user_id));

        endpoint.setQuery(query);

        QString msgType("");
        QJsonObject body;

        body = {{"typing", true}, {"timeout", timeoutInMillis}};

        QNetworkRequest request(QString(endpoint.toEncoded()));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        put(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void
MatrixClient::removeTypingNotification(const QString &roomid)
{
        QSettings settings;
        QString user_id = settings.value("auth/user_id").toString();

        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ + QString("/rooms/%1/typing/%2").arg(roomid).arg(user_id));

        endpoint.setQuery(query);

        QString msgType("");
        QJsonObject body;

        body = {{"typing", false}};

        QNetworkRequest request(QString(endpoint.toEncoded()));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        put(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void
MatrixClient::readEvent(const QString &room_id, const QString &event_id)
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(clientApiUrl_ +
                         QString("/rooms/%1/receipt/m.read/%2").arg(room_id).arg(event_id));
        endpoint.setQuery(query);

        QNetworkRequest request(QString(endpoint.toEncoded()));
        request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader, "application/json");

        auto reply = post(request, "{}");

        connect(reply, &QNetworkReply::finished, this, [reply]() {
                reply->deleteLater();

                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (status == 0 || status >= 400) {
                        qWarning() << reply->errorString();
                        return;
                }
        });
}

QNetworkReply *
MatrixClient::makeUploadRequest(QSharedPointer<QIODevice> iodev)
{
        QUrlQuery query;
        query.addQueryItem("access_token", token_);

        QUrl endpoint(server_);
        endpoint.setPath(mediaApiUrl_ + "/upload");
        endpoint.setQuery(query);

        if (!iodev->open(QIODevice::ReadWrite)) {
                qDebug() << "Error while reading buffer" << iodev.data();
                return nullptr;
        }

        QMimeDatabase db;
        QMimeType mime = db.mimeTypeForData(iodev.data());

        QNetworkRequest request(QString(endpoint.toEncoded()));
        request.setHeader(QNetworkRequest::ContentLengthHeader, iodev->size());
        request.setHeader(QNetworkRequest::ContentTypeHeader, mime.name());

        auto reply = post(request, iodev->readAll());

        return reply;
}
