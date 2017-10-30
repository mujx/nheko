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

#include <QAction>
#include <QSharedPointer>
#include <QWidget>

#include "Menu.h"
#include "RippleOverlay.h"
#include "RoomSettings.h"
#include "RoomState.h"

struct DescInfo {
        QString username;
        QString userid;
        QString body;
        QString timestamp;
};

class RoomInfoListItem : public QWidget
{
        Q_OBJECT

public:
        RoomInfoListItem(QSharedPointer<RoomSettings> settings,
                         RoomState state,
                         QString room_id,
                         QWidget *parent = 0);

        ~RoomInfoListItem();

        void updateUnreadMessageCount(int count);
        void clearUnreadMessageCount();
        void setState(const RoomState &state);

        inline bool isPressed() const;
        inline RoomState state() const;
        inline void setAvatar(const QImage &avatar_image);
        inline int unreadMessageCount() const;
        inline void setDescriptionMessage(const DescInfo &info);
        inline QString roomId();

signals:
        void clicked(const QString &room_id);
        void leaveRoom(const QString &room_id);

public slots:
        void setPressedState(bool state);

protected:
        void mousePressEvent(QMouseEvent *event) override;
        void paintEvent(QPaintEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;
        void contextMenuEvent(QContextMenuEvent *event) override;

private:
        QString notificationText();

        const int Padding  = 7;
        const int IconSize = 48;

        RippleOverlay *ripple_overlay_;

        RoomState state_;

        QString roomId_;
        QString roomName_;

        DescInfo lastMsgInfo_;

        QPixmap roomAvatar_;

        Menu *menu_;
        QAction *toggleNotifications_;
        QAction *leaveRoom_;

        QSharedPointer<RoomSettings> roomSettings_;

        bool isPressed_ = false;

        int maxHeight_;
        int unreadMsgCount_ = 0;
};

inline int
RoomInfoListItem::unreadMessageCount() const
{
        return unreadMsgCount_;
}

inline bool
RoomInfoListItem::isPressed() const
{
        return isPressed_;
}

inline RoomState
RoomInfoListItem::state() const
{
        return state_;
}

inline void
RoomInfoListItem::setAvatar(const QImage &img)
{
        roomAvatar_ = QPixmap::fromImage(
          img.scaled(IconSize, IconSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        update();
}

inline void
RoomInfoListItem::setDescriptionMessage(const DescInfo &info)
{
        lastMsgInfo_ = info;
        update();
}

inline QString
RoomInfoListItem::roomId()
{
        return roomId_;
}
