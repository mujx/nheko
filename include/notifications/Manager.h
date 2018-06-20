#pragma once

#include <QObject>
#include <QImage>
#include <QString>

#if defined(Q_OS_LINUX)
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusInterface>
#endif

struct roomEventId {
        QString roomId;
        QString eventId;
};

class NotificationsManager : public QObject
{
        Q_OBJECT
public:
        NotificationsManager(QObject *parent = nullptr);

        void postNotification(
                const QString &roomId,
                const QString &eventId,
                const QString &roomName,
                const QString &senderName,
                const QString &text,
                const QImage &icon);

signals:
        void notificationClicked(
                const QString roomId,
                const QString eventId);

#if defined(Q_OS_LINUX)
private:
        QDBusInterface dbus;
        uint showNotification(
                const QString summary,
                const QString text,
                const QImage image);

        // notification ID to (room ID, event ID)
        QMap<uint, roomEventId> notificationIds;

slots:
        void actionInvoked(
                uint id,
                QString action);
        void notificationClosed(
                uint id,
        uint reason);
#endif
};

#if defined(Q_OS_LINUX)
QDBusArgument& operator<<(QDBusArgument& arg, const QImage& image);
const QDBusArgument& operator>>(const QDBusArgument& arg, QImage&);
#endif
