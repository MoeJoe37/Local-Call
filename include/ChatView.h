#pragma once

#include <QDate>
#include <QListWidget>
#include <QString>
#include "ChatMessage.h"

class ChatBubble;
class QLabel;
class QPushButton;
class QTimer;

/// The scrolling message history, shared by the 1:1 and the group panel.
///
/// Owns everything that used to be spread between MainWindow's two identical
/// appendChatMsg/appendGroupMsg pairs and a viewport event filter: run
/// grouping, day separators, the typing placeholder, sizing on resize, and
/// whether an incoming message should steal the scroll position.
class ChatView : public QListWidget {
    Q_OBJECT
public:
    explicit ChatView(QWidget* parent = nullptr);

    /// Group chats identify speakers with avatars; a 1:1 chat does not need to.
    void setShowAvatars(bool on) { m_showAvatars = on; }

    void clearMessages();
    void appendMessage(const ChatMessage& cm);

    /// Ghost bubble pinned to the end of the list. Unlike the old status label
    /// this lives inside the scroll area, so showing it cannot nudge the
    /// message list or the composer.
    void setTypingIndicator(bool on, const QString& name = QString());

    /// Timestamps of the selected messages — the key ChatStore deletes by.
    QList<int64_t> selectedTimestamps() const;
    /// First selected message, or nullptr. Used by the context menu.
    ChatBubble* firstSelectedBubble() const;

    void scrollToLatest();

signals:
    void replyRequested(int64_t ts, const QString& name, const QString& snippet);
    void deleteRequested();

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    ChatBubble* bubbleFor(QListWidgetItem* item) const;
    void relayoutAll();
    void remeasure(QListWidgetItem* item);
    void insertDayChip(const QDate& date);
    void refreshChrome();          // empty state + jump pill placement/visibility
    bool isNearBottom() const;
    void removeTypingRow();

    bool     m_showAvatars = false;
    QTimer*  m_relayout    = nullptr;   // coalesces resize storms

    // Tail of the current run, for grouping decisions.
    QListWidgetItem* m_lastItem   = nullptr;
    QString          m_lastFromId;
    bool             m_lastIsMine = false;
    int64_t          m_lastTs     = 0;
    QDate            m_lastDate;

    QListWidgetItem* m_typingItem = nullptr;
    QString          m_typingName;

    QLabel*      m_emptyState = nullptr;
    QPushButton* m_jumpLatest = nullptr;
    int          m_missed     = 0;      // messages arrived while scrolled away
};
