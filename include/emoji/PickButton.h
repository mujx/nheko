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

#include <QEvent>
#include <QWidget>

#include "FlatButton.h"

namespace emoji {

class Panel;

class PickButton : public FlatButton
{
        Q_OBJECT
public:
        explicit PickButton(QWidget *parent = nullptr);

signals:
        void emojiSelected(const QString &emoji);

protected:
        void enterEvent(QEvent *e) override;

private:
        // Vertical distance from panel's bottom.
        int vertical_distance_ = 10;

        // Horizontal distance from panel's bottom right corner.
        int horizontal_distance_ = 70;

        QSharedPointer<Panel> panel_;
};
} // namespace emoji
