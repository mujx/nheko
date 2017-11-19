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

#include <QApplication>
#include <QDesktopWidget>
#include <QFile>
#include <QFontDatabase>
#include <QLibraryInfo>
#include <QNetworkProxy>
#include <QSettings>
#include <QTranslator>

#include "MainWindow.h"

void
setupProxy()
{
        QSettings settings;

        /**
          To set up a SOCKS proxy:
            [user]
            proxy\socks\host=<>
            proxy\socks\port=<>
            proxy\socks\user=<>
            proxy\socks\password=<>
          **/
        if (settings.contains("user/proxy/socks/host")) {
                QNetworkProxy proxy;
                proxy.setType(QNetworkProxy::Socks5Proxy);
                proxy.setHostName(settings.value("user/proxy/socks/host").toString());
                proxy.setPort(settings.value("user/proxy/socks/port").toInt());
                if (settings.contains("user/proxy/socks/user"))
                        proxy.setUser(settings.value("user/proxy/socks/user").toString());
                if (settings.contains("user/proxy/socks/password"))
                        proxy.setPassword(settings.value("user/proxy/socks/password").toString());
                QNetworkProxy::setApplicationProxy(proxy);
        }
}

int
main(int argc, char *argv[])
{
        QCoreApplication::setApplicationName("nheko");
        QCoreApplication::setApplicationVersion("0.1.0");
        QCoreApplication::setOrganizationName("nheko");
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

        QApplication app(argc, argv);

        QFontDatabase::addApplicationFont(":/fonts/fonts/OpenSans/OpenSans-Regular.ttf");
        QFontDatabase::addApplicationFont(":/fonts/fonts/OpenSans/OpenSans-Italic.ttf");
        QFontDatabase::addApplicationFont(":/fonts/fonts/OpenSans/OpenSans-Bold.ttf");
        QFontDatabase::addApplicationFont(":/fonts/fonts/OpenSans/OpenSans-Semibold.ttf");
        QFontDatabase::addApplicationFont(":/fonts/fonts/EmojiOne/emojione-android.ttf");

        app.setWindowIcon(QIcon(":/logos/nheko.png"));
        qSetMessagePattern("%{time process}: [%{type}] - %{message}");

        QSettings settings;

        QFile stylefile;

        if (!settings.contains("user/theme")) {
                settings.setValue("user/theme", "default");
        }

        if (settings.value("user/theme").toString() == "default") {
                stylefile.setFileName(":/styles/styles/nheko.qss");
        } else {
                stylefile.setFileName(":/styles/styles/system.qss");
        }
        stylefile.open(QFile::ReadOnly);
        QString stylesheet = QString(stylefile.readAll());

        app.setStyleSheet(stylesheet);
        // Set the default if a value has not been set.
        if (settings.value("font/size").toInt() == 0)
                settings.setValue("font/size", 12);

        QFont font("Open Sans", settings.value("font/size").toInt());
        app.setFont(font);

        QString lang = QLocale::system().name();

        QTranslator qtTranslator;
        qtTranslator.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
        app.installTranslator(&qtTranslator);

        QTranslator appTranslator;
        appTranslator.load("nheko_" + lang, ":/translations");
        app.installTranslator(&appTranslator);

        setupProxy();

        MainWindow w;

        // Move the MainWindow to the center
        QRect screenGeometry = QApplication::desktop()->screenGeometry();
        int x                = (screenGeometry.width() - w.width()) / 2;
        int y                = (screenGeometry.height() - w.height()) / 2;

        w.move(x, y);
        w.show();

        QObject::connect(&app, &QApplication::aboutToQuit, &w, &MainWindow::saveCurrentWindowSize);

        return app.exec();
}
