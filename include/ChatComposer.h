#pragma once

#include <QPlainTextEdit>
#include <QString>
#include <QWidget>
#include <cstdint>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

/// The message field. Grows from one line to six as you type, sends on Enter
/// and breaks the line on Shift+Enter.
class ComposerEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit ComposerEdit(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void submitted();

protected:
    void keyPressEvent(QKeyEvent*) override;

private:
    int heightForLines(int lines) const;
};

/// The whole compose area as one widget: reply chip, attachments, text field,
/// voice recording and send.
///
/// This replaces a two-row arrangement — a strip of labelled Image / File /
/// Record voice buttons stacked above a single-line QLineEdit and a wide Send
/// pill — plus the status label and progress bar that were siblings of the
/// message list and therefore shifted it whenever they appeared.
class ChatComposer : public QWidget {
    Q_OBJECT
public:
    explicit ChatComposer(QWidget* parent = nullptr);

    void setPlaceholderText(const QString& text);
    void setVoiceEnabled(bool on, const QString& disabledTip = QString());

    /// Quote the given message in the next send. Replaces the old trick of
    /// pasting "> snippet" into the input, which then went out as literal text.
    void setReplyTarget(int64_t ts, const QString& name, const QString& snippet);
    void clearReply();

    void setRecording(bool on);
    bool isRecording() const { return m_recording; }

    /// Slim strip along the composer's top edge — "Alice is uploading…" and
    /// friends. The band is always reserved, so text appearing never reflows.
    void setStatusText(const QString& text);
    /// Percentage 0-100, or a negative value to hide the bar.
    void setProgress(int pct);

    void focusInput();
    void clearInput();
    /// Drops text into the field, e.g. forwarding a message.
    void setText(const QString& text);

    /// Hides the whole card — former friends' conversations stay readable but
    /// cannot be written to.
    void setReadOnly(bool ro);

signals:
    void sendRequested(const QString& text, int64_t replyToTs,
                       const QString& replyName, const QString& replySnippet);
    void attachRequested(bool imagesOnly);
    void voiceRecordToggled(bool on);
    void voiceRecordCancelled();
    void typing();

private:
    void emitSend();
    void updateSendState();
    void updateReplyChip();

    QWidget*      m_card       = nullptr;
    QWidget*      m_inputRow   = nullptr;
    QWidget*      m_replyChip  = nullptr;
    QLabel*       m_replyName  = nullptr;
    QLabel*       m_replyText  = nullptr;
    ComposerEdit* m_input      = nullptr;
    QPushButton*  m_btnAttach  = nullptr;
    QPushButton*  m_btnVoice   = nullptr;
    QPushButton*  m_btnSend    = nullptr;

    QWidget*     m_recordStrip = nullptr;
    QLabel*      m_recordDot   = nullptr;
    QLabel*      m_recordTime  = nullptr;
    QTimer*      m_recordTick  = nullptr;
    int          m_recordHalfSecs = 0;
    bool         m_recording   = false;

    QLabel*       m_status   = nullptr;
    QProgressBar* m_progress = nullptr;

    int64_t m_replyTs = 0;
    QString m_replyToName;
    QString m_replySnippet;
};
