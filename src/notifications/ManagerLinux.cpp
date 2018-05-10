#define QT_NO_KEYWORDS
#include "notifications/Manager.h"
extern "C" {
    #include <libnotify/notify.h>
}

void
NotificationsManager::postNotification(const QString &roomName, const QString &userName, const QString &message)
{
        QString msg(roomName);
        msg.append('\n');
        msg.append(message);

        NotifyNotification *n;

        notify_init("nheko");

        n = notify_notification_new(userName.toStdString().c_str(), msg.toStdString().c_str(), "");
        notify_notification_set_timeout(n, 10000);

        notify_notification_show(n, NULL);

        g_object_unref(G_OBJECT(n));
}
