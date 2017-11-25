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
#include <QDebug>
#include <QDesktopWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "dialogs/PreviewImageOverlay.h"

using namespace dialogs;

PreviewImageOverlay::PreviewImageOverlay(QPixmap image, QWidget *parent)
  : QWidget{parent}
  , image_{image}
{
        init();
}

PreviewImageOverlay::PreviewImageOverlay(const QString &path, QWidget *parent)
  : QWidget{parent}
{
        if (!image_.load(path)) {
                qDebug() << "Failed to read image from:" << path;
                close();
                return;
        }

        init();
}

void
PreviewImageOverlay::init()
{
        auto window   = QApplication::activeWindow();
        auto winsize  = window->frameGeometry().size();
        auto center   = window->frameGeometry().center();
        auto img_size = image_.size();

        setAutoFillBackground(true);
        setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        setWindowModality(Qt::WindowModal);
        setPalette(QApplication::palette(window));

        titleLabel_ = new QLabel{tr("Upload image?"), this};
        imageLabel_ = new QLabel{this};
        imageName_  = new QLineEdit{tr("nheko_pasted_img.png"), this};
        upload_     = new QPushButton{tr("Upload"), this};
        cancel_     = new QPushButton{tr("Cancel"), this};

        titleLabel_->setStyleSheet("font-weight: bold; font-size: 22px;");
        titleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        titleLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageName_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        imageName_->setAlignment(Qt::AlignCenter);
        upload_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cancel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        // Scale image preview to the size of the current window if it is larger.
        if ((img_size.height() * img_size.width()) > (winsize.height() * winsize.width())) {
                imageLabel_->setPixmap(image_.scaled(winsize, Qt::KeepAspectRatio));
        } else {
                imageLabel_->setPixmap(image_);
                move(center.x() - (width() * 0.5), center.y() - (height() * 0.5));
        }
        imageLabel_->setScaledContents(false);

        auto hlayout = new QHBoxLayout;
        hlayout->addWidget(upload_);
        hlayout->addWidget(cancel_);

        auto vlayout = new QVBoxLayout{this};
        vlayout->addWidget(titleLabel_);
        vlayout->addWidget(imageLabel_);
        vlayout->addWidget(imageName_);
        vlayout->addLayout(hlayout);

        connect(upload_, &QPushButton::clicked, [=]() {
                emit confirmImageUpload(image_, imageName_->text());
        });
        connect(cancel_, &QPushButton::clicked, &dialogs::PreviewImageOverlay::close);

        raise();
}
