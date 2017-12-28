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
#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "Config.h"

#include "dialogs/PreviewImageOverlay.h"

using namespace dialogs;

PreviewImageOverlay::PreviewImageOverlay(QWidget *parent)
  : QWidget{parent}
  , titleLabel_{tr("Upload image?"), this}
  , imageLabel_{this}
  , imageName_{tr("clipboard"), this}
  , upload_{tr("Upload"), this}
  , cancel_{tr("Cancel"), this}
{
        auto hlayout = new QHBoxLayout;
        hlayout->addWidget(&upload_);
        hlayout->addWidget(&cancel_);

        auto vlayout = new QVBoxLayout{this};
        vlayout->addWidget(&titleLabel_);
        vlayout->addWidget(&imageLabel_);
        vlayout->addWidget(&imageName_);
        vlayout->addLayout(hlayout);

        connect(&upload_, &QPushButton::clicked, [&]() {
                emit confirmImageUpload(imageData_, imageName_.text());
                close();
        });
        connect(&cancel_, &QPushButton::clicked, [&]() { close(); });
}

void
PreviewImageOverlay::init()
{
        auto window   = QApplication::activeWindow();
        auto winsize  = window->frameGeometry().size();
        auto center   = window->frameGeometry().center();
        auto img_size = image_.size();

        imageName_.setText(imagePath_);

        setAutoFillBackground(true);
        setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        setWindowModality(Qt::WindowModal);

        titleLabel_.setStyleSheet(
          QString{"font-weight: bold; font-size: %1px;"}.arg(conf::headerFontSize));
        titleLabel_.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        titleLabel_.setAlignment(Qt::AlignCenter);
        imageLabel_.setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        imageLabel_.setAlignment(Qt::AlignCenter);
        imageName_.setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        imageName_.setAlignment(Qt::AlignCenter);
        upload_.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cancel_.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        upload_.setFontSize(conf::btn::fontSize);
        cancel_.setFontSize(conf::btn::fontSize);

        // Scale image preview to the size of the current window if it is larger.
        if ((img_size.height() * img_size.width()) > (winsize.height() * winsize.width())) {
                imageLabel_.setPixmap(image_.scaled(winsize, Qt::KeepAspectRatio));
        } else {
                imageLabel_.setPixmap(image_);
                move(center.x() - (width() * 0.5), center.y() - (height() * 0.5));
        }
        imageLabel_.setScaledContents(false);

        raise();
}

void
PreviewImageOverlay::setImageAndCreate(const QByteArray &data, const QString &type)
{
        imageData_ = data;
        image_.loadFromData(imageData_);
        imagePath_ = "clipboard." + type;

        init();
}

void
PreviewImageOverlay::setImageAndCreate(const QString &path)
{
        QFile file{path};

        if (!file.open(QIODevice::ReadOnly)) {
                qDebug() << "Failed to read image from:" << path;
                close();
                return;
        }

        imageData_ = file.readAll();
        image_.loadFromData(imageData_);
        imagePath_ = path;

        init();
}
