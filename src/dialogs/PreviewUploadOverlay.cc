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
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMimeDatabase>
#include <QVBoxLayout>

#include "Config.h"
#include "Logging.hpp"
#include "Utils.h"

#include "dialogs/PreviewUploadOverlay.h"

using namespace dialogs;

constexpr const char *DEFAULT = "Upload %1?";
constexpr const char *ERR_MSG = "Failed to load image type '%1'. Continue upload?";

PreviewUploadOverlay::PreviewUploadOverlay(QWidget *parent)
  : QWidget{parent}
  , titleLabel_{this}
  , fileName_{this}
  , upload_{tr("Upload"), this}
  , cancel_{tr("Cancel"), this}
{
        auto hlayout = new QHBoxLayout;
        hlayout->addWidget(&upload_);
        hlayout->addWidget(&cancel_);

        auto vlayout = new QVBoxLayout{this};
        vlayout->addWidget(&titleLabel_);
        vlayout->addWidget(&infoLabel_);
        vlayout->addWidget(&fileName_);
        vlayout->addLayout(hlayout);

        connect(&upload_, &QPushButton::clicked, [this]() {
                emit confirmUpload(data_, mediaType_, fileName_.text());
                close();
        });
        connect(&cancel_, &QPushButton::clicked, this, &PreviewUploadOverlay::close);
}

void
PreviewUploadOverlay::init()
{
        auto window  = QApplication::activeWindow();
        auto winsize = window->frameGeometry().size();
        auto center  = window->frameGeometry().center();

        fileName_.setText(QFileInfo{filePath_}.fileName());

        setAutoFillBackground(true);
        setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        setWindowModality(Qt::WindowModal);

        titleLabel_.setStyleSheet(
          QString{"font-weight: bold; font-size: %1px;"}.arg(conf::headerFontSize));
        titleLabel_.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        titleLabel_.setAlignment(Qt::AlignCenter);
        infoLabel_.setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        fileName_.setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        fileName_.setAlignment(Qt::AlignCenter);
        upload_.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cancel_.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        upload_.setFontSize(conf::btn::fontSize);
        cancel_.setFontSize(conf::btn::fontSize);

        if (isImage_) {
                infoLabel_.setAlignment(Qt::AlignCenter);

                const auto maxWidth  = winsize.width() * 0.8;
                const auto maxHeight = winsize.height() * 0.8;

                // Scale image preview to fit into the application window.
                infoLabel_.setPixmap(utils::scaleDown<QPixmap>(maxWidth, maxHeight, image_));
                move(center.x() - (width() * 0.5), center.y() - (height() * 0.5));
        } else {
                infoLabel_.setAlignment(Qt::AlignLeft);
        }
        infoLabel_.setScaledContents(false);

        show();
}

void
PreviewUploadOverlay::setLabels(const QString &type, const QString &mime, uint64_t upload_size)
{
        if (mediaType_ == "image") {
                if (!image_.loadFromData(data_)) {
                        titleLabel_.setText(QString{tr(ERR_MSG)}.arg(type));
                } else {
                        titleLabel_.setText(QString{tr(DEFAULT)}.arg(mediaType_));
                }
                isImage_ = true;
        } else {
                auto const info = QString{tr("Media type: %1\n"
                                             "Media size: %2\n")}
                                    .arg(mime)
                                    .arg(utils::humanReadableFileSize(upload_size));

                titleLabel_.setText(QString{tr(DEFAULT)}.arg("file"));
                infoLabel_.setText(info);
        }
}

void
PreviewUploadOverlay::setPreview(const QByteArray previewdata, const QString &mime)
{
        auto const &split = mime.split('/');
        auto const &type  = split[1];

        data_      = previewdata;
        mediaType_ = split[0];
        filePath_  = "clipboard." + type;
        isImage_   = false;

        setLabels(type, mime, data_.size());
        init();
}

void
PreviewUploadOverlay::setPreview(const QString &path)
{
        QFile file{path};

        if (!file.open(QIODevice::ReadOnly)) {
                nhlog::ui()->warn("Failed to open file ({}): {}",
                                  path.toStdString(),
                                  file.errorString().toStdString());
                close();
                return;
        }

        QMimeDatabase db;
        auto mime = db.mimeTypeForFileNameAndData(path, &file);

        if ((data_ = file.readAll()).isEmpty()) {
                nhlog::ui()->warn("Failed to read media: {}", file.errorString().toStdString());
                close();
                return;
        }

        auto const &split = mime.name().split('/');

        mediaType_ = split[0];
        filePath_  = file.fileName();
        isImage_   = false;

        setLabels(split[1], mime.name(), data_.size());
        init();
}
