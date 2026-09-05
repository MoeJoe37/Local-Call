#include "ChatView.h"

#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "ChatBubble.h"
#include "UiTheme.h"

namespace {

/// Messages closer together than this from the same sender form one run.
constexpr qint64 kRunGapMs = 5 * 60 * 1000;
/// How far from the bottom counts as "still reading history".
constexpr int kStickyPx = 120;
/// Resize events arrive per pixel while dragging; batch them.
constexpr int kRelayoutDelayMs = 30;

// Item data roles. UserRole stays the message timestamp because ChatStore
// deletes by timestamp and MainWindow's delete path already reads it.
constexpr int kRoleTimestamp = Qt::UserRole;

}  // namespace

ChatView::ChatView(QWidget* parent)
    : QListWidget(parent)
{
    setObjectName(QStringLiteral("msgArea"));
    setFrameShape(QFrame::NoFrame);
    setUniformItemSizes(false);
    setSpacing(0);
    setWordWrap(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setFocusPolicy(Qt::NoFocus);

    m_relayout = new QTimer(this);
    m_relayout->setSingleShot(true);
    m_relayout->setInterval(kRelayoutDelayMs);
    connect(m_relayout, &QTimer::timeout, this, &ChatView::relayoutAll);

    m_emptyState = new QLabel(tr("No messages yet — say hi"), viewport());
    m_emptyState->setObjectName(QStringLiteral("chatEmptyState"));
    m_emptyState->setAlignment(Qt::AlignCenter);

    m_jumpLatest = new QPushButton(viewport());
    m_jumpLatest->setObjectName(QStringLiteral("jumpLatest"));
    m_jumpLatest->setCursor(Qt::PointingHandCursor);
    m_jumpLatest->setVisible(false);
    UiTheme::applyIcon(m_jumpLatest, QStringLiteral("back"), 14);
    connect(m_jumpLatest, &QPushButton::clicked, this, &ChatView::scrollToLatest);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (isNearBottom()) m_missed = 0;
        refreshChrome();
    });

    refreshChrome();
}

// ─────────────────────────────────────────────────────────────────────────────

ChatBubble* ChatView::bubbleFor(QListWidgetItem* item) const
{
    return item ? qobject_cast<ChatBubble*>(itemWidget(item)) : nullptr;
}

bool ChatView::isNearBottom() const
{
    QScrollBar* sb = verticalScrollBar();
    return sb->maximum() - sb->value() <= kStickyPx;
}

void ChatView::clearMessages()
{
    m_typingItem = nullptr;
    m_lastItem   = nullptr;
    m_lastFromId.clear();
    m_lastIsMine = false;
    m_lastTs     = 0;
    m_lastDate   = QDate();
    m_missed     = 0;
    clear();
    refreshChrome();
}

void ChatView::insertDayChip(const QDate& date)
{
    auto* item = new QListWidgetItem(this);
    item->setFlags(Qt::NoItemFlags);          // never selectable, never deletable
    auto* chip = ChatBubble::makeDayChip(date, this);
    item->setSizeHint(QSize(viewport()->width(), chip->sizeHint().height()));
    setItemWidget(item, chip);
}

void ChatView::appendMessage(const ChatMessage& cm)
{
    const bool stick = isNearBottom();

    // The typing ghost always stays last, so lift it out and put it back after.
    const QString typingName = m_typingName;
    const bool wasTyping = (m_typingItem != nullptr);
    removeTypingRow();

    const QDate day = QDateTime::fromMSecsSinceEpoch(cm.timestamp).date();
    bool newDay = false;
    if (!m_lastDate.isValid() || m_lastDate != day) {
        insertDayChip(day);
        m_lastDate = day;
        newDay = true;
    }

    const bool isChip = (cm.kind == MessageKind::System || cm.kind == MessageKind::CallEvent);
    const bool sameRun =
        !newDay && !isChip && m_lastItem != nullptr &&
        m_lastFromId == QString::fromStdString(cm.fromId) &&
        m_lastIsMine == cm.isMine &&
        cm.timestamp - m_lastTs <= kRunGapMs;

    // A run's last bubble carries the clock and the avatar; when a message
    // joins the run, the previous tail is demoted to the middle of it.
    if (sameRun) {
        if (ChatBubble* prev = bubbleFor(m_lastItem)) {
            const BubbleShape demoted = (prev->shape() == BubbleShape::Solo)
                                        ? BubbleShape::First
                                        : BubbleShape::Middle;
            if (prev->setShape(demoted)) remeasure(m_lastItem);
        }
    }

    auto* item = new QListWidgetItem(this);
    item->setData(kRoleTimestamp, qlonglong(cm.timestamp));
    if (isChip) item->setFlags(Qt::NoItemFlags);

    auto* bubble = new ChatBubble(cm, sameRun ? BubbleShape::Last : BubbleShape::Solo,
                                  m_showAvatars, this);
    setItemWidget(item, bubble);
    remeasure(item);

    connect(bubble, &ChatBubble::sizeChanged, this, [this, item] { remeasure(item); });
    connect(bubble, &ChatBubble::deleteRequested, this, [this, item] {
        clearSelection();
        item->setSelected(true);
        emit deleteRequested();
    });
    connect(bubble, &ChatBubble::replyRequested, this, [this, bubble] {
        emit replyRequested(bubble->timestamp(), bubble->senderName(), bubble->plainText());
    });

    if (isChip) {
        // A chip breaks any run — the next message starts fresh.
        m_lastItem = nullptr;
        m_lastFromId.clear();
        m_lastTs = 0;
    } else {
        m_lastItem   = item;
        m_lastFromId = QString::fromStdString(cm.fromId);
        m_lastIsMine = cm.isMine;
        m_lastTs     = cm.timestamp;
    }

    if (wasTyping) setTypingIndicator(true, typingName);

    // Only follow the conversation if the reader was already at the bottom.
    // Scrolling unconditionally used to yank people out of the history they
    // had deliberately scrolled back to.
    if (stick) scrollToLatest();
    else       ++m_missed;

    refreshChrome();
}

void ChatView::removeTypingRow()
{
    if (!m_typingItem) return;
    delete takeItem(row(m_typingItem));
    m_typingItem = nullptr;
}

void ChatView::setTypingIndicator(bool on, const QString& name)
{
    if (!on) {
        removeTypingRow();
        m_typingName.clear();
        return;
    }
    if (m_typingItem) {          // already showing; just keep it at the end
        m_typingName = name;
        return;
    }

    const bool stick = isNearBottom();
    m_typingName = name;

    m_typingItem = new QListWidgetItem(this);
    m_typingItem->setFlags(Qt::NoItemFlags);
    auto* rowWidget = ChatBubble::makeTypingRow(name, this);
    m_typingItem->setSizeHint(QSize(viewport()->width(), rowWidget->sizeHint().height()));
    setItemWidget(m_typingItem, rowWidget);

    if (stick) scrollToLatest();
}

// ─────────────────────────────────────────────────────────────────────────────

void ChatView::remeasure(QListWidgetItem* item)
{
    if (!item) return;
    const int w = viewport()->width();
    if (ChatBubble* b = bubbleFor(item)) {
        item->setSizeHint(QSize(w, b->layoutForWidth(w)));
    } else if (QWidget* plain = itemWidget(item)) {
        item->setSizeHint(QSize(w, plain->sizeHint().height()));
    }
}

void ChatView::relayoutAll()
{
    const bool stick = isNearBottom();
    setUpdatesEnabled(false);
    for (int i = 0; i < count(); ++i) remeasure(item(i));
    setUpdatesEnabled(true);
    if (stick) scrollToLatest();
    refreshChrome();
}

void ChatView::resizeEvent(QResizeEvent* e)
{
    QListWidget::resizeEvent(e);
    if (e->oldSize().width() != e->size().width())
        m_relayout->start();      // restarts, so a drag relayouts once at the end
    refreshChrome();
}

void ChatView::refreshChrome()
{
    if (!m_emptyState || !m_jumpLatest) return;

    const QRect vp = viewport()->rect();

    const bool empty = (count() == 0);
    m_emptyState->setVisible(empty);
    if (empty) m_emptyState->setGeometry(vp);

    const bool show = !empty && !isNearBottom();
    m_jumpLatest->setText(m_missed > 0 ? tr("%n new message(s)", "", m_missed)
                                       : tr("Jump to latest"));
    m_jumpLatest->adjustSize();
    m_jumpLatest->setVisible(show);
    if (show) {
        m_jumpLatest->move((vp.width() - m_jumpLatest->width()) / 2,
                           vp.height() - m_jumpLatest->height() - 12);
        m_jumpLatest->raise();
    }
}

void ChatView::scrollToLatest()
{
    m_missed = 0;
    QTimer::singleShot(0, this, [this] {
        scrollToBottom();
        refreshChrome();
    });
}

// ─────────────────────────────────────────────────────────────────────────────

QList<int64_t> ChatView::selectedTimestamps() const
{
    QList<int64_t> out;
    const auto sel = selectedItems();
    for (QListWidgetItem* it : sel) {
        const qlonglong ts = it->data(kRoleTimestamp).toLongLong();
        if (ts != 0) out.append(int64_t(ts));
    }
    return out;
}

ChatBubble* ChatView::firstSelectedBubble() const
{
    const auto sel = selectedItems();
    return sel.isEmpty() ? nullptr : bubbleFor(sel.first());
}
