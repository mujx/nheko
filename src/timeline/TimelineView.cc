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
#include <QFileInfo>
#include <QTimer>

#include "Config.h"
#include "FloatingButton.h"
#include "RoomMessages.h"
#include "Utils.h"

#include "timeline/TimelineView.h"
#include "timeline/widgets/AudioItem.h"
#include "timeline/widgets/FileItem.h"
#include "timeline/widgets/ImageItem.h"
#include "timeline/widgets/VideoItem.h"

using TimelineEvent = mtx::events::collections::TimelineEvents;

TimelineView::TimelineView(const mtx::responses::Timeline &timeline,
                           QSharedPointer<MatrixClient> client,
                           const QString &room_id,
                           QWidget *parent)
  : QWidget(parent)
  , room_id_{room_id}
  , client_{client}
{
        init();
        addEvents(timeline);
}

TimelineView::TimelineView(QSharedPointer<MatrixClient> client,
                           const QString &room_id,
                           QWidget *parent)
  : QWidget(parent)
  , room_id_{room_id}
  , client_{client}
{
        init();
        client_->messages(room_id_, "");
}

void
TimelineView::sliderRangeChanged(int min, int max)
{
        Q_UNUSED(min);

        if (!scroll_area_->verticalScrollBar()->isVisible()) {
                scroll_area_->verticalScrollBar()->setValue(max);
                return;
        }

        // If the scrollbar is close to the bottom and a new message
        // is added we move the scrollbar.
        if (max - scroll_area_->verticalScrollBar()->value() < SCROLL_BAR_GAP) {
                scroll_area_->verticalScrollBar()->setValue(max);
                return;
        }

        int currentHeight = scroll_widget_->size().height();
        int diff          = currentHeight - oldHeight_;
        int newPosition   = oldPosition_ + diff;

        // Keep the scroll bar to the bottom if it hasn't been activated yet.
        if (oldPosition_ == 0 && !scroll_area_->verticalScrollBar()->isVisible())
                newPosition = max;

        if (lastMessageDirection_ == TimelineDirection::Top)
                scroll_area_->verticalScrollBar()->setValue(newPosition);
}

void
TimelineView::fetchHistory()
{
        if (!isScrollbarActivated() && !isTimelineFinished) {
                if (!isVisible()) {
                        // Check again later if the timeline became visible.
                        // TODO: Use a backoff strategy.
                        paginationTimer_->start(3000);
                        return;
                }

                isPaginationInProgress_ = true;
                client_->messages(room_id_, prev_batch_token_);
                paginationTimer_->start(1500);

                return;
        }

        paginationTimer_->stop();
}

void
TimelineView::scrollDown()
{
        int current = scroll_area_->verticalScrollBar()->value();
        int max     = scroll_area_->verticalScrollBar()->maximum();

        // The first time we enter the room move the scroll bar to the bottom.
        if (!isInitialized) {
                scroll_area_->verticalScrollBar()->setValue(max);
                isInitialized = true;
                return;
        }

        // If the gap is small enough move the scroll bar down. e.g when a new
        // message appears.
        if (max - current < SCROLL_BAR_GAP)
                scroll_area_->verticalScrollBar()->setValue(max);
}

void
TimelineView::sliderMoved(int position)
{
        if (!scroll_area_->verticalScrollBar()->isVisible())
                return;

        const int maxScroll     = scroll_area_->verticalScrollBar()->maximum();
        const int currentScroll = scroll_area_->verticalScrollBar()->value();

        if (maxScroll - currentScroll > SCROLL_BAR_GAP) {
                scrollDownBtn_->show();
                scrollDownBtn_->raise();
        } else {
                scrollDownBtn_->hide();
        }

        // The scrollbar is high enough so we can start retrieving old events.
        if (position < SCROLL_BAR_GAP) {
                if (isTimelineFinished)
                        return;

                // Prevent user from moving up when there is pagination in
                // progress.
                // TODO: Keep a map of the event ids to filter out duplicates.
                if (isPaginationInProgress_)
                        return;

                isPaginationInProgress_ = true;

                // FIXME: Maybe move this to TimelineViewManager to remove the
                // extra calls?
                client_->messages(room_id_, prev_batch_token_);
        }
}

void
TimelineView::addBackwardsEvents(const QString &room_id, const mtx::responses::Messages &msgs)
{
        if (room_id_ != room_id)
                return;

        if (msgs.chunk.size() == 0) {
                isTimelineFinished = true;
                return;
        }

        isTimelineFinished = false;
        QList<TimelineItem *> items;

        // Reset the sender of the first message in the timeline
        // cause we're about to insert a new one.
        firstSender_.clear();

        // Parse in reverse order to determine where we should not show sender's
        // name.
        auto ii = msgs.chunk.size();
        while (ii != 0) {
                --ii;

                TimelineItem *item = parseMessageEvent(msgs.chunk[ii], TimelineDirection::Top);

                if (item != nullptr)
                        items.push_back(item);
        }

        // Reverse again to render them.
        std::reverse(items.begin(), items.end());

        oldPosition_ = scroll_area_->verticalScrollBar()->value();
        oldHeight_   = scroll_widget_->size().height();

        for (const auto &item : items)
                addTimelineItem(item, TimelineDirection::Top);

        lastMessageDirection_ = TimelineDirection::Top;

        QApplication::processEvents();

        prev_batch_token_       = QString::fromStdString(msgs.end);
        isPaginationInProgress_ = false;

        // Exclude the top stretch.
        if (msgs.chunk.size() != 0 && scroll_layout_->count() > 1)
                notifyForLastEvent();

        // If this batch is the first being rendered (i.e the first and the last
        // events originate from this batch), set the last sender.
        if (lastSender_.isEmpty() && !items.isEmpty())
                lastSender_ = items.constFirst()->descriptionMessage().userid;
}

TimelineItem *
TimelineView::parseMessageEvent(const mtx::events::collections::TimelineEvents &event,
                                TimelineDirection direction)
{
        namespace msg     = mtx::events::msg;
        using AudioEvent  = mtx::events::RoomEvent<msg::Audio>;
        using EmoteEvent  = mtx::events::RoomEvent<msg::Emote>;
        using FileEvent   = mtx::events::RoomEvent<msg::File>;
        using ImageEvent  = mtx::events::RoomEvent<msg::Image>;
        using NoticeEvent = mtx::events::RoomEvent<msg::Notice>;
        using TextEvent   = mtx::events::RoomEvent<msg::Text>;
        using VideoEvent  = mtx::events::RoomEvent<msg::Video>;

        if (mpark::holds_alternative<mtx::events::RoomEvent<msg::Audio>>(event)) {
                auto audio = mpark::get<mtx::events::RoomEvent<msg::Audio>>(event);
                return processMessageEvent<AudioEvent, AudioItem>(audio, direction);
        } else if (mpark::holds_alternative<mtx::events::RoomEvent<msg::Emote>>(event)) {
                auto emote = mpark::get<mtx::events::RoomEvent<msg::Emote>>(event);
                return processMessageEvent<EmoteEvent>(emote, direction);
        } else if (mpark::holds_alternative<mtx::events::RoomEvent<msg::File>>(event)) {
                auto file = mpark::get<mtx::events::RoomEvent<msg::File>>(event);
                return processMessageEvent<FileEvent, FileItem>(file, direction);
        } else if (mpark::holds_alternative<mtx::events::RoomEvent<msg::Image>>(event)) {
                auto image = mpark::get<mtx::events::RoomEvent<msg::Image>>(event);
                return processMessageEvent<ImageEvent, ImageItem>(image, direction);
        } else if (mpark::holds_alternative<mtx::events::RoomEvent<msg::Notice>>(event)) {
                auto notice = mpark::get<mtx::events::RoomEvent<msg::Notice>>(event);
                return processMessageEvent<NoticeEvent>(notice, direction);
        } else if (mpark::holds_alternative<mtx::events::RoomEvent<msg::Text>>(event)) {
                auto text = mpark::get<mtx::events::RoomEvent<msg::Text>>(event);
                return processMessageEvent<TextEvent>(text, direction);
        } else if (mpark::holds_alternative<mtx::events::RoomEvent<msg::Video>>(event)) {
                auto video = mpark::get<mtx::events::RoomEvent<msg::Video>>(event);
                return processMessageEvent<VideoEvent, VideoItem>(video, direction);
        }

        return nullptr;
}

void
TimelineView::renderBottomEvents(const std::vector<TimelineEvent> &events)
{
        for (const auto &event : events) {
                TimelineItem *item = parseMessageEvent(event, TimelineDirection::Bottom);

                if (item != nullptr)
                        addTimelineItem(item, TimelineDirection::Bottom);
        }

        lastMessageDirection_ = TimelineDirection::Bottom;

        QApplication::processEvents();
}

int
TimelineView::addEvents(const mtx::responses::Timeline &timeline)
{
        int message_count = 0;

        if (isInitialSync) {
                prev_batch_token_ = QString::fromStdString(timeline.prev_batch);
                isInitialSync     = false;
        }

        for (const auto &e : timeline.events) {
                // Save the message if it can be rendered.
                if (isViewable(e))
                        bottomMessages_.push_back(e);

                // Calculate notifications.
                if (isNotifiable(e))
                        message_count += 1;
        }

        if (!bottomMessages_.empty())
                notifyForLastEvent(bottomMessages_[bottomMessages_.size() - 1]);

        // If the current timeline is open and there are messages to be rendered.
        if (isVisible() && !bottomMessages_.empty()) {
                renderBottomEvents(bottomMessages_);

                // Free up space for new messages.
                bottomMessages_.clear();

                // Send a read receipt for the last event.
                if (isActiveWindow())
                        readLastEvent();
        }

        return message_count;
}

inline bool
TimelineView::isViewable(const TimelineEvent &event) const
{
        namespace msg = mtx::events::msg;

        return mpark::holds_alternative<mtx::events::RoomEvent<msg::Audio>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Emote>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::File>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Image>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Notice>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Text>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Video>>(event);
}

inline bool
TimelineView::isNotifiable(const TimelineEvent &event) const
{
        namespace msg = mtx::events::msg;

        if (local_user_ == getEventSender(event))
                return false;

        return mpark::holds_alternative<mtx::events::RoomEvent<msg::Audio>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Emote>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::File>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Image>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Notice>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Text>>(event) ||
               mpark::holds_alternative<mtx::events::RoomEvent<msg::Video>>(event);
}

void
TimelineView::init()
{
        QSettings settings;
        local_user_ = settings.value("auth/user_id").toString();

        QIcon icon;
        icon.addFile(":/icons/icons/ui/angle-arrow-down.png");
        scrollDownBtn_ = new FloatingButton(icon, this);
        scrollDownBtn_->setBackgroundColor(QColor("#F5F5F5"));
        scrollDownBtn_->setForegroundColor(QColor("black"));
        scrollDownBtn_->hide();

        connect(scrollDownBtn_, &QPushButton::clicked, this, [=]() {
                const int max = scroll_area_->verticalScrollBar()->maximum();
                scroll_area_->verticalScrollBar()->setValue(max);
        });
        top_layout_ = new QVBoxLayout(this);
        top_layout_->setSpacing(0);
        top_layout_->setMargin(0);

        scroll_area_ = new QScrollArea(this);
        scroll_area_->setWidgetResizable(true);
        scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        scrollbar_ = new ScrollBar(scroll_area_);
        scroll_area_->setVerticalScrollBar(scrollbar_);

        scroll_widget_ = new QWidget(this);

        scroll_layout_ = new QVBoxLayout(scroll_widget_);
        scroll_layout_->setContentsMargins(15, 0, 15, 15);
        scroll_layout_->addStretch(1);
        scroll_layout_->setSpacing(0);
        scroll_layout_->setObjectName("timelinescrollarea");

        scroll_area_->setWidget(scroll_widget_);

        top_layout_->addWidget(scroll_area_);

        setLayout(top_layout_);

        paginationTimer_ = new QTimer(this);
        connect(paginationTimer_, &QTimer::timeout, this, &TimelineView::fetchHistory);

        connect(client_.data(),
                &MatrixClient::messagesRetrieved,
                this,
                &TimelineView::addBackwardsEvents);

        connect(scroll_area_->verticalScrollBar(),
                SIGNAL(valueChanged(int)),
                this,
                SLOT(sliderMoved(int)));
        connect(scroll_area_->verticalScrollBar(),
                SIGNAL(rangeChanged(int, int)),
                this,
                SLOT(sliderRangeChanged(int, int)));
}

void
TimelineView::updateLastSender(const QString &user_id, TimelineDirection direction)
{
        if (direction == TimelineDirection::Bottom)
                lastSender_ = user_id;
        else
                firstSender_ = user_id;
}

bool
TimelineView::isSenderRendered(const QString &user_id, TimelineDirection direction)
{
        if (direction == TimelineDirection::Bottom)
                return lastSender_ != user_id;
        else
                return firstSender_ != user_id;
}

void
TimelineView::addTimelineItem(TimelineItem *item, TimelineDirection direction)
{
        const auto newDate = item->descriptionMessage().datetime;

        if (direction == TimelineDirection::Bottom) {
                const auto lastItemPosition = scroll_layout_->count() - 1;
                auto lastItem =
                  qobject_cast<TimelineItem *>(scroll_layout_->itemAt(lastItemPosition)->widget());

                if (lastItem) {
                        auto oldDate = lastItem->descriptionMessage().datetime;

                        if (oldDate.daysTo(newDate) != 0)
                                addDateSeparator(newDate, lastItemPosition);
                }

                scroll_layout_->addWidget(item);
        } else {
                // The first item (position 0) is a stretch widget that pushes
                // the widgets to the bottom of the page.
                if (scroll_layout_->count() > 1) {
                        auto firstItem =
                          qobject_cast<TimelineItem *>(scroll_layout_->itemAt(1)->widget());

                        if (firstItem) {
                                auto oldDate = firstItem->descriptionMessage().datetime;

                                if (newDate.daysTo(oldDate) != 0)
                                        addDateSeparator(oldDate, 1);
                        }
                }

                scroll_layout_->insertWidget(1, item);
        }
}

void
TimelineView::updatePendingMessage(int txn_id, QString event_id)
{
        if (!pending_msgs_.isEmpty() &&
            pending_msgs_.head().txn_id == txn_id) { // We haven't received it yet
                auto msg     = pending_msgs_.dequeue();
                msg.event_id = event_id;

                if (msg.widget)
                        msg.widget->setEventId(event_id);

                pending_sent_msgs_.append(msg);
        }

        sendNextPendingMessage();
}

void
TimelineView::addUserMessage(mtx::events::MessageType ty, const QString &body)
{
        auto with_sender = lastSender_ != local_user_;

        TimelineItem *view_item =
          new TimelineItem(ty, local_user_, body, with_sender, scroll_widget_);
        scroll_layout_->addWidget(view_item);

        lastMessageDirection_ = TimelineDirection::Bottom;

        QApplication::processEvents();

        lastSender_ = local_user_;

        int txn_id = client_->incrementTransactionId();
        PendingMessage message(ty, txn_id, body, nullptr, "", view_item);
        handleNewUserMessage(message);
}

void
TimelineView::handleNewUserMessage(PendingMessage msg)
{
        pending_msgs_.enqueue(msg);
        if (pending_msgs_.size() == 1 && pending_sent_msgs_.isEmpty())
                sendNextPendingMessage();
}

void
TimelineView::sendNextPendingMessage()
{
        if (pending_msgs_.size() == 0)
                return;

        PendingMessage &m = pending_msgs_.head();
        switch (m.ty) {
        case mtx::events::MessageType::Audio:
        case mtx::events::MessageType::Image:
        case mtx::events::MessageType::File:
                // FIXME: Improve the API
                client_->sendRoomMessage(m.ty,
                                         m.txn_id,
                                         room_id_,
                                         m.file->fileName(),
                                         QFileInfo(*m.file),
                                         m.body);
                break;
        default:
                client_->sendRoomMessage(m.ty, m.txn_id, room_id_, m.body, QFileInfo());
                break;
        }
}

void
TimelineView::notifyForLastEvent()
{
        auto lastItem          = scroll_layout_->itemAt(scroll_layout_->count() - 1);
        auto *lastTimelineItem = qobject_cast<TimelineItem *>(lastItem->widget());

        if (lastTimelineItem)
                emit updateLastTimelineMessage(room_id_, lastTimelineItem->descriptionMessage());
        else
                qWarning() << "Cast to TimelineView failed" << room_id_;
}

void
TimelineView::notifyForLastEvent(const TimelineEvent &event)
{
        auto descInfo = utils::getMessageDescription(event, local_user_);

        if (!descInfo.timestamp.isEmpty())
                emit updateLastTimelineMessage(room_id_, descInfo);
}

bool
TimelineView::isPendingMessage(const QString &txnid,
                               const QString &sender,
                               const QString &local_userid)
{
        if (sender != local_userid)
                return false;

        for (const auto &msg : pending_msgs_) {
                if (QString::number(msg.txn_id) == txnid)
                        return true;
        }

        for (const auto &msg : pending_sent_msgs_) {
                if (QString::number(msg.txn_id) == txnid)
                        return true;
        }

        return false;
}

void
TimelineView::removePendingMessage(const QString &txnid)
{
        for (auto it = pending_sent_msgs_.begin(); it != pending_sent_msgs_.end(); ++it) {
                if (QString::number(it->txn_id) == txnid) {
                        int index = std::distance(pending_sent_msgs_.begin(), it);
                        pending_sent_msgs_.removeAt(index);

                        if (pending_sent_msgs_.isEmpty())
                                sendNextPendingMessage();

                        return;
                }
        }
        for (auto it = pending_msgs_.begin(); it != pending_msgs_.end(); ++it) {
                if (QString::number(it->txn_id) == txnid) {
                        int index = std::distance(pending_msgs_.begin(), it);
                        pending_msgs_.removeAt(index);
                        return;
                }
        }
}

void
TimelineView::handleFailedMessage(int txnid)
{
        Q_UNUSED(txnid);
        // Note: We do this even if the message has already been echoed.
        QTimer::singleShot(2000, this, SLOT(sendNextPendingMessage()));
}

void
TimelineView::paintEvent(QPaintEvent *)
{
        QStyleOption opt;
        opt.init(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void
TimelineView::readLastEvent() const
{
        const auto eventId = getLastEventId();

        if (!eventId.isEmpty())
                client_->readEvent(room_id_, eventId);
}

QString
TimelineView::getLastEventId() const
{
        auto index = scroll_layout_->count();

        // Search backwards for the first event that has a valid event id.
        while (index > 0) {
                --index;

                auto lastItem          = scroll_layout_->itemAt(index);
                auto *lastTimelineItem = qobject_cast<TimelineItem *>(lastItem->widget());

                if (lastTimelineItem && !lastTimelineItem->eventId().isEmpty())
                        return lastTimelineItem->eventId();
        }

        return QString("");
}

void
TimelineView::showEvent(QShowEvent *event)
{
        if (!bottomMessages_.empty()) {
                renderBottomEvents(bottomMessages_);
                bottomMessages_.clear();
                scrollDown();
        }

        readLastEvent();

        QWidget::showEvent(event);
}

bool
TimelineView::event(QEvent *event)
{
        if (event->type() == QEvent::WindowActivate) {
                QTimer::singleShot(1000, this, [=]() {
                        emit clearUnreadMessageCount(room_id_);
                        readLastEvent();
                });
        }

        return QWidget::event(event);
}

void
TimelineView::addDateSeparator(QDateTime datetime, int position)
{
        auto now  = QDateTime::currentDateTime();
        auto days = now.daysTo(datetime);

        QString fmt;
        QLabel *separator;

        if (now.date().year() != datetime.date().year())
                fmt = QString("ddd d MMMM yy");
        else
                fmt = QString("ddd d MMMM");

        if (days == 0)
                separator = new QLabel(tr("Today"));
        else if (std::abs(days) == 1)
                separator = new QLabel(tr("Yesterday"));
        else
                separator = new QLabel(datetime.toString(fmt));

        if (separator) {
                separator->setStyleSheet(
                  QString("font-size: %1px").arg(conf::timeline::fonts::dateSeparator));
                separator->setAlignment(Qt::AlignCenter);
                separator->setContentsMargins(0, 15, 0, 15);
                scroll_layout_->insertWidget(position, separator);
        }
}

QString
TimelineView::getEventSender(const mtx::events::collections::TimelineEvents &event) const
{
        using Aliases           = mtx::events::StateEvent<mtx::events::state::Aliases>;
        using Avatar            = mtx::events::StateEvent<mtx::events::state::Avatar>;
        using CanonicalAlias    = mtx::events::StateEvent<mtx::events::state::CanonicalAlias>;
        using Create            = mtx::events::StateEvent<mtx::events::state::Create>;
        using HistoryVisibility = mtx::events::StateEvent<mtx::events::state::HistoryVisibility>;
        using JoinRules         = mtx::events::StateEvent<mtx::events::state::JoinRules>;
        using Member            = mtx::events::StateEvent<mtx::events::state::Member>;
        using Name              = mtx::events::StateEvent<mtx::events::state::Name>;
        using PowerLevels       = mtx::events::StateEvent<mtx::events::state::PowerLevels>;
        using Topic             = mtx::events::StateEvent<mtx::events::state::Topic>;

        using Audio  = mtx::events::RoomEvent<mtx::events::msg::Audio>;
        using Emote  = mtx::events::RoomEvent<mtx::events::msg::Emote>;
        using File   = mtx::events::RoomEvent<mtx::events::msg::File>;
        using Image  = mtx::events::RoomEvent<mtx::events::msg::Image>;
        using Notice = mtx::events::RoomEvent<mtx::events::msg::Notice>;
        using Text   = mtx::events::RoomEvent<mtx::events::msg::Text>;
        using Video  = mtx::events::RoomEvent<mtx::events::msg::Video>;

        if (mpark::holds_alternative<Aliases>(event)) {
                auto msg = mpark::get<Aliases>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Avatar>(event)) {
                auto msg = mpark::get<Avatar>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<CanonicalAlias>(event)) {
                auto msg = mpark::get<CanonicalAlias>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Create>(event)) {
                auto msg = mpark::get<Create>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<HistoryVisibility>(event)) {
                auto msg = mpark::get<HistoryVisibility>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<JoinRules>(event)) {
                auto msg = mpark::get<JoinRules>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Name>(event)) {
                auto msg = mpark::get<Name>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Member>(event)) {
                auto msg = mpark::get<Member>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<PowerLevels>(event)) {
                auto msg = mpark::get<PowerLevels>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Topic>(event)) {
                auto msg = mpark::get<Topic>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Audio>(event)) {
                auto msg = mpark::get<Audio>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Emote>(event)) {
                auto msg = mpark::get<Emote>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<File>(event)) {
                auto msg = mpark::get<File>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Image>(event)) {
                auto msg = mpark::get<Image>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Notice>(event)) {
                auto msg = mpark::get<Notice>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Text>(event)) {
                auto msg = mpark::get<Text>(event);
                return QString::fromStdString(msg.sender);
        } else if (mpark::holds_alternative<Video>(event)) {
                auto msg = mpark::get<Video>(event);
                return QString::fromStdString(msg.sender);
        }

        return QString("");
}
