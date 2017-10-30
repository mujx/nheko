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

#pragma once

#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include "Cache.h"
#include "CommunitiesList.h"
#include "MatrixClient.h"
#include "QuickSwitcher.h"
#include "RoomList.h"
#include "RoomSettings.h"
#include "RoomState.h"
#include "SideBarActions.h"
#include "Splitter.h"
#include "TextInputWidget.h"
#include "TimelineViewManager.h"
#include "TopRoomBar.h"
#include "TypingDisplay.h"
#include "UserInfoWidget.h"

constexpr int CONSENSUS_TIMEOUT    = 1000;
constexpr int SHOW_CONTENT_TIMEOUT = 3000;

class ChatPage : public QWidget
{
        Q_OBJECT

public:
        ChatPage(QSharedPointer<MatrixClient> client, QWidget *parent = 0);
        ~ChatPage();

        // Initialize all the components of the UI.
        void bootstrap(QString userid, QString homeserver, QString token);

signals:
        void contentLoaded();
        void close();
        void changeWindowTitle(const QString &msg);
        void unreadMessages(int count);
        void showNotification(const QString &msg);

private slots:
        void showUnreadMessageNotification(int count);
        void updateTopBarAvatar(const QString &roomid, const QPixmap &img);
        void updateOwnProfileInfo(const QUrl &avatar_url, const QString &display_name);
        void updateOwnCommunitiesInfo(const QList<QString> &own_communities);
        void setOwnAvatar(const QPixmap &img);
        void initialSyncCompleted(const SyncResponse &response);
        void syncCompleted(const SyncResponse &response);
        void syncFailed(const QString &msg);
        void changeTopRoomInfo(const QString &room_id);
        void logout();
        void addRoom(const QString &room_id);
        void removeRoom(const QString &room_id);

protected:
        void keyPressEvent(QKeyEvent *event) override;

private:
        void updateTypingUsers(const QString &roomid, const QList<QString> &user_ids);
        void updateDisplayNames(const RoomState &state);
        void loadStateFromCache();
        void showQuickSwitcher();

        QHBoxLayout *topLayout_;
        Splitter *splitter;

        QWidget *sideBar_;
        QWidget *communitiesSideBar_;
        QVBoxLayout *communitiesSideBarLayout_;
        QVBoxLayout *sideBarLayout_;
        QVBoxLayout *sideBarTopLayout_;
        QVBoxLayout *sideBarMainLayout_;
        QWidget *sideBarTopWidget_;
        QVBoxLayout *sideBarTopWidgetLayout_;

        QWidget *content_;
        QVBoxLayout *contentLayout_;
        QHBoxLayout *topBarLayout_;
        QVBoxLayout *mainContentLayout_;

        CommunitiesList *communitiesList_;
        RoomList *room_list_;

        TimelineViewManager *view_manager_;
        SideBarActions *sidebarActions_;

        TopRoomBar *top_bar_;
        TextInputWidget *text_input_;
        TypingDisplay *typingDisplay_;

        // Safety net if consensus is not possible or too slow.
        QTimer *showContentTimer_;
        QTimer *consensusTimer_;

        QString current_room_;
        QString current_community_;

        QMap<QString, QPixmap> room_avatars_;
        QMap<QString, QPixmap> community_avatars_;

        UserInfoWidget *user_info_widget_;

        QMap<QString, RoomState> state_manager_;
        QMap<QString, QSharedPointer<RoomSettings>> settingsManager_;

        QMap<QString, QSharedPointer<Community>> communityManager_;

        // Keeps track of the users currently typing on each room.
        QMap<QString, QList<QString>> typingUsers_;

        QSharedPointer<QuickSwitcher> quickSwitcher_;
        QSharedPointer<OverlayModal> quickSwitcherModal_;

        // Matrix Client API provider.
        QSharedPointer<MatrixClient> client_;

        // LMDB wrapper.
        QSharedPointer<Cache> cache_;
};
