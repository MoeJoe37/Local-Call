#pragma once

#include <QWidget>
#include <QString>
#include "ChatMessage.h"

class QDate;
class QLabel;
class QPixmap;

/// Where a bubble sits inside a run of consecutive messages from one sender.
/// Drives corner rounding, and whether the name/avatar/timestamp are drawn:
/// a run shows the sender once at the top and the clock once at the bottom.
enum class BubbleShape { Solo, First, Middle, Last };

/// One rendered chat message — the widget handed to QListWidget::setItemWidget.
///
/// It owns its own geometry. Callers give it the width available in the
/// viewport and it answers with the height it needs; nothing outside this class
/// needs to know about padding, fonts or wrapping. That replaces the previous
/// arrangement, where MainWindow_logic.cpp reproduced the bubble's internal
/// padding as a pile of literals (padV=16, metaH=14, 270 for any image…) and
/// measured text with a fabricated QFont that ignored the applied stylesheet.
class ChatBubble : public QWidget {
    Q_OBJECT
public:
    ChatBubble(const ChatMessage& cm, BubbleShape shape, bool showAvatar,
               QWidget* parent = nullptr);

    /// Sizes the bubble for the given available outer width and returns the
    /// total height it needs. Call on insert and on every viewport resize.
    int layoutForWidth(int outerW);

    /// Re-classifies an already-built bubble when a later message joins its run.
    /// Returns true if anything changed, i.e. the caller must re-measure.
    bool setShape(BubbleShape shape);
    BubbleShape shape() const { return m_shape; }

    int64_t timestamp()  const { return m_ts; }
    QString senderName() const { return m_senderName; }
    /// The message as typed, with no link markup — what Copy and Forward want.
    QString plainText()  const { return m_plainText; }

    /// Centred "Today" / "Yesterday" / "5 Sep 2026" separator row.
    static QWidget* makeDayChip(const QDate& date, QWidget* parent = nullptr);
    /// Animated "…" placeholder shown while the peer is typing.
    static QWidget* makeTypingRow(const QString& name, QWidget* parent = nullptr);
    /// Circular initials avatar, tinted deterministically from the sender id.
    static QPixmap  avatarPixmap(const QString& name, const QString& id, int px);

signals:
    void copyRequested();
    void replyRequested();
    void deleteRequested();
    /// The bubble's content changed height on its own (the fold toggle); the
    /// view must re-measure and refresh the item's size hint.
    void sizeChanged();

protected:
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void buildTextBody(const ChatMessage& cm);
    void buildImageBody(const ChatMessage& cm);
    void buildAttachmentBody(const ChatMessage& cm, bool voice);
    void buildQuote(const ChatMessage& cm);
    void applyShapeClass();
    int  preferredInnerWidth() const;
    int  measureRichTextHeight(const QString& html, const QFont& font, int width) const;

    MessageKind m_kind  = MessageKind::Text;
    BubbleShape m_shape = BubbleShape::Solo;
    bool        m_isMine     = false;
    bool        m_showAvatar = false;
    int64_t     m_ts = 0;
    QString     m_senderName;
    QString     m_plainText;
    QString     m_richText;      // linkified; empty for non-text kinds
    int         m_naturalBodyW = 0;   // unwrapped width the body would like

    QWidget* m_bubble    = nullptr;
    QWidget* m_actions   = nullptr;
    QLabel*  m_textLabel = nullptr;
    QLabel*  m_nameLabel = nullptr;
    QLabel*  m_metaLabel = nullptr;
    QLabel*  m_avatar    = nullptr;
    QLabel*  m_quoteText = nullptr;
    QWidget* m_quoteBox  = nullptr;
};
