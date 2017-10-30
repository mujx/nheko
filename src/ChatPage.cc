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
#include <QSettings>

#include "AvatarProvider.h"
#include "ChatPage.h"
#include "MainWindow.h"
#include "Splitter.h"
#include "Sync.h"
#include "Theme.h"
#include "TimelineViewManager.h"
#include "UserInfoWidget.h"

#include "StateEvent.h"

namespace events = matrix::events;

ChatPage::ChatPage(QSharedPointer<MatrixClient> client, QWidget *parent)
  : QWidget(parent)
  , client_(client)
{
        setStyleSheet("background-color: #fff;");

        topLayout_ = new QHBoxLayout(this);
        topLayout_->setSpacing(0);
        topLayout_->setMargin(0);

        communitiesSideBar_ = new QWidget(this);
        communitiesSideBar_->setFixedWidth(ui::sidebar::CommunitiesSidebarSize);
        communitiesSideBarLayout_ = new QVBoxLayout(communitiesSideBar_);
        communitiesSideBarLayout_->setSpacing(0);
        communitiesSideBarLayout_->setMargin(0);

        communitiesList_ = new CommunitiesList(client, this);
        communitiesSideBarLayout_->addWidget(communitiesList_);
        // communitiesSideBarLayout_->addStretch(1);
        topLayout_->addWidget(communitiesSideBar_);

        auto splitter = new Splitter(this);
        splitter->setHandleWidth(0);

        topLayout_->addWidget(splitter);

        // SideBar
        sideBar_ = new QWidget(this);
        sideBar_->setMinimumSize(QSize(ui::sidebar::NormalSize, 0));
        sideBarLayout_ = new QVBoxLayout(sideBar_);
        sideBarLayout_->setSpacing(0);
        sideBarLayout_->setMargin(0);

        sideBarTopLayout_ = new QVBoxLayout();
        sideBarTopLayout_->setSpacing(0);
        sideBarTopLayout_->setMargin(0);
        sideBarMainLayout_ = new QVBoxLayout();
        sideBarMainLayout_->setSpacing(0);
        sideBarMainLayout_->setMargin(0);

        sideBarLayout_->addLayout(sideBarTopLayout_);
        sideBarLayout_->addLayout(sideBarMainLayout_);

        sideBarTopWidget_ = new QWidget(sideBar_);
        sideBarTopWidget_->setStyleSheet("background-color: #d6dde3; color: #ebebeb;");

        sideBarTopLayout_->addWidget(sideBarTopWidget_);

        sideBarTopWidgetLayout_ = new QVBoxLayout(sideBarTopWidget_);
        sideBarTopWidgetLayout_->setSpacing(0);
        sideBarTopWidgetLayout_->setMargin(0);

        // Content
        content_       = new QWidget(this);
        contentLayout_ = new QVBoxLayout(content_);
        contentLayout_->setSpacing(0);
        contentLayout_->setMargin(0);

        topBarLayout_ = new QHBoxLayout();
        topBarLayout_->setSpacing(0);
        mainContentLayout_ = new QVBoxLayout();
        mainContentLayout_->setSpacing(0);
        mainContentLayout_->setMargin(0);

        contentLayout_->addLayout(topBarLayout_);
        contentLayout_->addLayout(mainContentLayout_);

        // Splitter
        splitter->addWidget(sideBar_);
        splitter->addWidget(content_);

        room_list_ = new RoomList(client, sideBar_);
        sideBarMainLayout_->addWidget(room_list_);

        top_bar_ = new TopRoomBar(this);
        topBarLayout_->addWidget(top_bar_);

        view_manager_ = new TimelineViewManager(client, this);
        mainContentLayout_->addWidget(view_manager_);

        text_input_    = new TextInputWidget(this);
        typingDisplay_ = new TypingDisplay(this);
        contentLayout_->addWidget(typingDisplay_);
        contentLayout_->addWidget(text_input_);

        user_info_widget_ = new UserInfoWidget(sideBarTopWidget_);
        sideBarTopWidgetLayout_->addWidget(user_info_widget_);

        syncTimer_ = new QTimer(this);
        syncTimer_->setSingleShot(true);
        connect(syncTimer_, SIGNAL(timeout()), this, SLOT(startSync()));

        connect(user_info_widget_, SIGNAL(logout()), client_.data(), SLOT(logout()));
        connect(client_.data(), SIGNAL(loggedOut()), this, SLOT(logout()));

        connect(
          top_bar_, &TopRoomBar::leaveRoom, this, [=]() { client_->leaveRoom(current_room_); });

        connect(room_list_, &RoomList::roomChanged, this, [=](const QString &roomid) {
                QStringList users;

                if (typingUsers_.contains(roomid))
                        users = typingUsers_[roomid];

                typingDisplay_->setUsers(users);
        });

        connect(room_list_, &RoomList::roomChanged, this, &ChatPage::changeTopRoomInfo);
        connect(room_list_, &RoomList::roomChanged, text_input_, &TextInputWidget::focusLineEdit);
        connect(
          room_list_, &RoomList::roomChanged, view_manager_, &TimelineViewManager::setHistoryView);

        connect(view_manager_,
                &TimelineViewManager::unreadMessages,
                this,
                [=](const QString &roomid, int count) {
                        if (!settingsManager_.contains(roomid)) {
                                qWarning() << "RoomId does not have settings" << roomid;
                                room_list_->updateUnreadMessageCount(roomid, count);
                                return;
                        }

                        if (settingsManager_[roomid]->isNotificationsEnabled())
                                room_list_->updateUnreadMessageCount(roomid, count);
                });

        connect(view_manager_,
                &TimelineViewManager::updateRoomsLastMessage,
                room_list_,
                &RoomList::updateRoomDescription);

        connect(room_list_,
                SIGNAL(totalUnreadMessageCountUpdated(int)),
                this,
                SLOT(showUnreadMessageNotification(int)));

        connect(text_input_,
                SIGNAL(sendTextMessage(const QString &)),
                view_manager_,
                SLOT(sendTextMessage(const QString &)));

        connect(text_input_,
                SIGNAL(sendEmoteMessage(const QString &)),
                view_manager_,
                SLOT(sendEmoteMessage(const QString &)));

        connect(text_input_,
                &TextInputWidget::sendJoinRoomRequest,
                client_.data(),
                &MatrixClient::joinRoom);

        connect(text_input_, &TextInputWidget::uploadImage, this, [=](QString filename) {
                client_->uploadImage(current_room_, filename);
        });

        connect(client_.data(), &MatrixClient::joinFailed, this, &ChatPage::showNotification);
        connect(client_.data(),
                &MatrixClient::imageUploaded,
                this,
                [=](QString roomid, QString filename, QString url) {
                        text_input_->hideUploadSpinner();
                        view_manager_->sendImageMessage(roomid, filename, url);
                });

        connect(client_.data(),
                SIGNAL(roomAvatarRetrieved(const QString &, const QPixmap &)),
                this,
                SLOT(updateTopBarAvatar(const QString &, const QPixmap &)));

        connect(client_.data(),
                SIGNAL(initialSyncCompleted(const SyncResponse &)),
                this,
                SLOT(initialSyncCompleted(const SyncResponse &)));
        connect(client_.data(),
                SIGNAL(syncCompleted(const SyncResponse &)),
                this,
                SLOT(syncCompleted(const SyncResponse &)));
        connect(client_.data(),
                SIGNAL(syncFailed(const QString &)),
                this,
                SLOT(syncFailed(const QString &)));
        connect(client_.data(),
                SIGNAL(getOwnProfileResponse(const QUrl &, const QString &)),
                this,
                SLOT(updateOwnProfileInfo(const QUrl &, const QString &)));
        connect(client_.data(),
                SIGNAL(getOwnCommunitiesResponse(QList<QString>)),
                this,
                SLOT(updateOwnCommunitiesInfo(QList<QString>)));
        connect(client_.data(),
                &MatrixClient::communityProfileRetrieved,
                this,
                [=](QString communityId, QJsonObject profile) {
                        communityManager_[communityId]->parseProfile(profile);
                });
        connect(client_.data(),
                &MatrixClient::communityRoomsRetrieved,
                this,
                [=](QString communityId, QJsonObject rooms) {
                        communityManager_[communityId]->parseRooms(rooms);

                        if (communityId == current_community_) {
                                if (communityId == "world") {
                                        room_list_->setFilterRooms(false);
                                } else {
                                        room_list_->setRoomFilter(
                                          communityManager_[communityId]->getRoomList());
                                }
                        }
                });
        connect(client_.data(),
                SIGNAL(ownAvatarRetrieved(const QPixmap &)),
                this,
                SLOT(setOwnAvatar(const QPixmap &)));
        connect(client_.data(), &MatrixClient::joinedRoom, this, [=]() {
                emit showNotification("You joined the room.");
        });
        connect(client_.data(),
                SIGNAL(leftRoom(const QString &)),
                this,
                SLOT(removeRoom(const QString &)));

        showContentTimer_ = new QTimer(this);
        showContentTimer_->setSingleShot(true);
        connect(showContentTimer_, &QTimer::timeout, this, [=]() {
                consensusTimer_->stop();
                emit contentLoaded();
        });

        consensusTimer_ = new QTimer(this);
        connect(consensusTimer_, &QTimer::timeout, this, [=]() {
                if (view_manager_->hasLoaded()) {
                        // Remove the spinner overlay.
                        emit contentLoaded();
                        showContentTimer_->stop();
                        consensusTimer_->stop();
                }
        });

        connect(communitiesList_,
                &CommunitiesList::communityChanged,
                this,
                [=](const QString &communityId) {
                        current_community_ = communityId;
                        if (communityId == "world") {
                                room_list_->setFilterRooms(false);
                        } else {
                                room_list_->setRoomFilter(
                                  communityManager_[communityId]->getRoomList());
                        }
                });

        AvatarProvider::init(client);
}

void
ChatPage::logout()
{
        syncTimer_->stop();

        // Delete all config parameters.
        QSettings settings;
        settings.beginGroup("auth");
        settings.remove("");
        settings.endGroup();
        settings.beginGroup("client");
        settings.remove("");
        settings.endGroup();
        settings.beginGroup("notifications");
        settings.remove("");
        settings.endGroup();

        cache_->deleteData();

        // Clear the environment.
        room_list_->clear();
        view_manager_->clearAll();

        top_bar_->reset();
        user_info_widget_->reset();
        client_->reset();

        state_manager_.clear();
        settingsManager_.clear();
        room_avatars_.clear();

        AvatarProvider::clear();

        emit close();
}

void
ChatPage::bootstrap(QString userid, QString homeserver, QString token)
{
        client_->setServer(homeserver);
        client_->setAccessToken(token);
        client_->getOwnProfile();
        client_->getOwnCommunities();

        cache_ = QSharedPointer<Cache>(new Cache(userid));

        try {
                cache_->setup();

                if (cache_->isInitialized()) {
                        loadStateFromCache();
                        return;
                }
        } catch (const lmdb::error &e) {
                qCritical() << "Cache failure" << e.what();
                cache_->unmount();
                cache_->deleteData();
                qInfo() << "Falling back to initial sync ...";
        }

        client_->initialSync();
}

void
ChatPage::startSync()
{
        client_->sync();
}

void
ChatPage::setOwnAvatar(const QPixmap &img)
{
        user_info_widget_->setAvatar(img.toImage());
}

void
ChatPage::syncFailed(const QString &msg)
{
        // Stop if sync is not active. e.g user is logged out.
        if (client_->getHomeServer().isEmpty())
                return;

        qWarning() << "Sync error:" << msg;
        syncTimer_->start(SYNC_INTERVAL);
}

// TODO: Should be moved in another class that manages this global list.
void
ChatPage::updateDisplayNames(const RoomState &state)
{
        for (const auto member : state.memberships) {
                auto displayName = member.content().displayName();

                if (!displayName.isEmpty())
                        TimelineViewManager::DISPLAY_NAMES.insert(member.stateKey(), displayName);
        }
}

void
ChatPage::syncCompleted(const SyncResponse &response)
{
        auto joined = response.rooms().join();

        for (auto it = joined.constBegin(); it != joined.constEnd(); it++) {
                updateTypingUsers(it.key(), it.value().typingUserIDs());

                RoomState room_state;

                // Merge the new updates for rooms that we are tracking.
                if (state_manager_.contains(it.key())) {
                        room_state = state_manager_[it.key()];
                }

                room_state.updateFromEvents(it.value().state().events());
                room_state.updateFromEvents(it.value().timeline().events());

                updateDisplayNames(room_state);

                if (state_manager_.contains(it.key())) {
                        // TODO: Use pointers instead of copying.
                        auto oldState = state_manager_[it.key()];
                        oldState.update(room_state);
                        state_manager_.insert(it.key(), oldState);
                } else {
                        RoomState room_state;

                        // Build the current state from the timeline and state events.
                        room_state.updateFromEvents(it.value().state().events());
                        room_state.updateFromEvents(it.value().timeline().events());

                        // Remove redundant memberships.
                        room_state.removeLeaveMemberships();

                        // Resolve room name and avatar. e.g in case of one-to-one chats.
                        room_state.resolveName();
                        room_state.resolveAvatar();

                        updateDisplayNames(room_state);

                        state_manager_.insert(it.key(), room_state);
                        settingsManager_.insert(
                          it.key(), QSharedPointer<RoomSettings>(new RoomSettings(it.key())));

                        for (const auto membership : room_state.memberships) {
                                auto uid = membership.sender();
                                auto url = membership.content().avatarUrl();

                                if (!url.toString().isEmpty())
                                        AvatarProvider::setAvatarUrl(uid, url);
                        }

                        view_manager_->addRoom(it.value(), it.key());
                }

                if (it.key() == current_room_)
                        changeTopRoomInfo(it.key());
        }

        auto leave = response.rooms().leave();

        for (auto it = leave.constBegin(); it != leave.constEnd(); it++) {
                if (state_manager_.contains(it.key())) {
                        removeRoom(it.key());
                }
        }

        try {
                cache_->setState(response.nextBatch(), state_manager_);
        } catch (const lmdb::error &e) {
                qCritical() << "The cache couldn't be updated: " << e.what();
                // TODO: Notify the user.
                cache_->unmount();
                cache_->deleteData();
        }

        client_->setNextBatchToken(response.nextBatch());

        room_list_->sync(state_manager_);
        view_manager_->sync(response.rooms());

        syncTimer_->start(SYNC_INTERVAL);
}

void
ChatPage::initialSyncCompleted(const SyncResponse &response)
{
        auto joined = response.rooms().join();

        for (auto it = joined.constBegin(); it != joined.constEnd(); it++) {
                RoomState room_state;

                // Build the current state from the timeline and state events.
                room_state.updateFromEvents(it.value().state().events());
                room_state.updateFromEvents(it.value().timeline().events());

                // Remove redundant memberships.
                room_state.removeLeaveMemberships();

                // Resolve room name and avatar. e.g in case of one-to-one chats.
                room_state.resolveName();
                room_state.resolveAvatar();

                updateDisplayNames(room_state);

                state_manager_.insert(it.key(), room_state);
                settingsManager_.insert(it.key(),
                                        QSharedPointer<RoomSettings>(new RoomSettings(it.key())));

                for (const auto membership : room_state.memberships) {
                        auto uid = membership.sender();
                        auto url = membership.content().avatarUrl();

                        if (!url.toString().isEmpty())
                                AvatarProvider::setAvatarUrl(uid, url);
                }
        }

        try {
                cache_->setState(response.nextBatch(), state_manager_);
        } catch (const lmdb::error &e) {
                qCritical() << "The cache couldn't be initialized: " << e.what();
                cache_->unmount();
                cache_->deleteData();
        }

        client_->setNextBatchToken(response.nextBatch());

        // Populate timelines with messages.
        view_manager_->initialize(response.rooms());

        // Initialize room list.
        room_list_->setInitialRooms(settingsManager_, state_manager_);

        syncTimer_->start(SYNC_INTERVAL);

        emit contentLoaded();
}

void
ChatPage::updateTopBarAvatar(const QString &roomid, const QPixmap &img)
{
        room_avatars_.insert(roomid, img);

        if (current_room_ != roomid)
                return;

        top_bar_->updateRoomAvatar(img.toImage());
}

void
ChatPage::updateOwnProfileInfo(const QUrl &avatar_url, const QString &display_name)
{
        QSettings settings;
        auto userid = settings.value("auth/user_id").toString();

        user_info_widget_->setUserId(userid);
        user_info_widget_->setDisplayName(display_name);

        if (avatar_url.isValid())
                client_->fetchOwnAvatar(avatar_url);
}

void
ChatPage::updateOwnCommunitiesInfo(const QList<QString> &own_communities)
{
        for (int i = 0; i < own_communities.size(); i++) {
                QSharedPointer<Community> community = QSharedPointer<Community>(new Community());

                communityManager_[own_communities[i]] = community;
        }

        communitiesList_->setCommunities(communityManager_);
}

void
ChatPage::changeTopRoomInfo(const QString &room_id)
{
        if (!state_manager_.contains(room_id))
                return;

        auto state = state_manager_[room_id];

        top_bar_->updateRoomName(state.getName());
        top_bar_->updateRoomTopic(state.getTopic());
        top_bar_->setRoomSettings(settingsManager_[room_id]);

        if (room_avatars_.contains(room_id))
                top_bar_->updateRoomAvatar(room_avatars_.value(room_id).toImage());
        else
                top_bar_->updateRoomAvatarFromName(state.getName());

        current_room_ = room_id;
}

void
ChatPage::showUnreadMessageNotification(int count)
{
        emit unreadMessages(count);

        // TODO: Make the default title a const.
        if (count == 0)
                emit changeWindowTitle("nheko");
        else
                emit changeWindowTitle(QString("nheko (%1)").arg(count));
}

void
ChatPage::loadStateFromCache()
{
        qDebug() << "Restoring state from cache";

        qDebug() << "Restored nextBatchToken" << cache_->nextBatchToken();
        client_->setNextBatchToken(cache_->nextBatchToken());

        // Fetch all the joined room's state.
        auto rooms = cache_->states();

        for (auto it = rooms.constBegin(); it != rooms.constEnd(); it++) {
                RoomState room_state = it.value();

                // Clean up and prepare state for use.
                room_state.removeLeaveMemberships();
                room_state.resolveName();
                room_state.resolveAvatar();

                // Update the global list with user's display names.
                updateDisplayNames(room_state);

                // Save the current room state.
                state_manager_.insert(it.key(), room_state);

                // Create or restore the settings for this room.
                settingsManager_.insert(it.key(),
                                        QSharedPointer<RoomSettings>(new RoomSettings(it.key())));

                // Resolve user avatars.
                for (const auto membership : room_state.memberships) {
                        auto uid = membership.sender();
                        auto url = membership.content().avatarUrl();

                        if (!url.toString().isEmpty())
                                AvatarProvider::setAvatarUrl(uid, url);
                }
        }

        // Initializing empty timelines.
        view_manager_->initialize(rooms.keys());

        // Initialize room list from the restored state and settings.
        room_list_->setInitialRooms(settingsManager_, state_manager_);

        // Check periodically if the timelines have been loaded.
        consensusTimer_->start(CONSENSUS_TIMEOUT);

        // Show the content if consensus can't be achieved.
        showContentTimer_->start(SHOW_CONTENT_TIMEOUT);

        // Start receiving events.
        syncTimer_->start(SYNC_INTERVAL);
}

void
ChatPage::keyPressEvent(QKeyEvent *event)
{
        if (event->key() == Qt::Key_K) {
                if (event->modifiers() == Qt::ControlModifier)
                        showQuickSwitcher();
        }
}

void
ChatPage::showQuickSwitcher()
{
        if (quickSwitcher_.isNull()) {
                quickSwitcher_ = QSharedPointer<QuickSwitcher>(
                  new QuickSwitcher(this),
                  [=](QuickSwitcher *switcher) { switcher->deleteLater(); });

                connect(quickSwitcher_.data(),
                        &QuickSwitcher::roomSelected,
                        room_list_,
                        &RoomList::highlightSelectedRoom);

                connect(quickSwitcher_.data(), &QuickSwitcher::closing, this, [=]() {
                        if (!this->quickSwitcherModal_.isNull())
                                this->quickSwitcherModal_->fadeOut();
                });
        }

        if (quickSwitcherModal_.isNull()) {
                quickSwitcherModal_ = QSharedPointer<OverlayModal>(
                  new OverlayModal(MainWindow::instance(), quickSwitcher_.data()),
                  [=](OverlayModal *modal) { modal->deleteLater(); });
                quickSwitcherModal_->setDuration(0);
                quickSwitcherModal_->setColor(QColor(30, 30, 30, 170));
        }

        QMap<QString, QString> rooms;

        for (auto it = state_manager_.constBegin(); it != state_manager_.constEnd(); ++it)
                rooms.insert(it.value().getName(), it.key());

        quickSwitcher_->setRoomList(rooms);
        quickSwitcherModal_->fadeIn();
}

void
ChatPage::addRoom(const QString &room_id)
{
        if (!state_manager_.contains(room_id)) {
                RoomState room_state;

                state_manager_.insert(room_id, room_state);
                settingsManager_.insert(room_id,
                                        QSharedPointer<RoomSettings>(new RoomSettings(room_id)));

                room_list_->addRoom(settingsManager_[room_id], state_manager_[room_id], room_id);
                room_list_->highlightSelectedRoom(room_id);

                changeTopRoomInfo(room_id);
        }
}

void
ChatPage::removeRoom(const QString &room_id)
{
        state_manager_.remove(room_id);
        settingsManager_.remove(room_id);
        try {
                cache_->removeRoom(room_id);
        } catch (const lmdb::error &e) {
                qCritical() << "The cache couldn't be updated: " << e.what();
                // TODO: Notify the user.
                cache_->unmount();
                cache_->deleteData();
        }
        room_list_->removeRoom(room_id, room_id == current_room_);
}

void
ChatPage::updateTypingUsers(const QString &roomid, const QList<QString> &user_ids)
{
        QStringList users;

        for (const auto uid : user_ids)
                users.append(TimelineViewManager::displayName(uid));

        users.sort();

        if (current_room_ == roomid)
                typingDisplay_->setUsers(users);

        typingUsers_.insert(roomid, users);
}

ChatPage::~ChatPage()
{
        syncTimer_->stop();
}
