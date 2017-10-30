#pragma once

#include <QWidget>
#include <QSharedPointer>
#include <QMouseEvent>
#include <QPainter>
#include <QDebug>

#include "ui/Theme.h"
#include "Menu.h"
#include "Community.h"

class CommunitiesListItem : public QWidget
{
    Q_OBJECT

public:
    CommunitiesListItem(QSharedPointer<Community> community,
                        QString community_id,
                        QWidget *parent = nullptr);

    ~CommunitiesListItem();

    void setCommunity(QSharedPointer<Community> community);

    inline bool isPressed() const;
    inline void setAvatar(const QImage &avatar_image);

signals:
    void clicked(const QString &community_id);

public slots:
    void setPressedState(bool state);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    const int IconSize = 55;

    QSharedPointer<Community> community_;
    QString communityId_;
    QString communityName_;
    QString communityShortDescription;

    QPixmap communityAvatar_;

    Menu *menu_;
    bool isPressed_ = false;
};

inline bool
CommunitiesListItem::isPressed() const
{
    return isPressed_;
}

inline void
CommunitiesListItem::setAvatar(const QImage &avatar_image)
{
    communityAvatar_ = QPixmap::fromImage(
                avatar_image.scaled(IconSize,
                                    IconSize,
                                    Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation));
    update();
}

class WorldCommunityListItem : public CommunitiesListItem
{
    Q_OBJECT
public:
    WorldCommunityListItem(QWidget *parent = nullptr);
    ~WorldCommunityListItem();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
private:
    const int IconSize = 55;
};
