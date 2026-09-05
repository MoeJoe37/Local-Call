#include "ChatComposer.h"

#include <QAbstractTextDocumentLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "UiTheme.h"

namespace {
constexpr int kMinLines = 1;
constexpr int kMaxLines = 6;
constexpr int kBtnPx    = 34;
/// Reserved for the status text and progress line. Always present, so a peer
/// starting an upload cannot push the message list around.
constexpr int kStatusBandPx = 16;

QString elide(const QString& s, int max)
{
    const QString flat = s.simplified();
    return flat.length() <= max ? flat : flat.left(max).trimmed() + QStringLiteral("...");
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  ComposerEdit
// ─────────────────────────────────────────────────────────────────────────────

ComposerEdit::ComposerEdit(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("composerInput"));
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        updateGeometry();
    });
}

int ComposerEdit::heightForLines(int lines) const
{
    const QMargins m = contentsMargins();
    return int(std::ceil(fontMetrics().lineSpacing() * lines))
           + int(document()->documentMargin() * 2)
           + m.top() + m.bottom() + 2;
}

QSize ComposerEdit::sizeHint() const
{
    // QPlainTextDocumentLayout is the odd one out: its documentSize() reports
    // the height as a line count, not pixels, and it already accounts for
    // wrapped lines. Dividing by lineSpacing() here would always yield 1.
    const qreal docLines = document()->documentLayout()->documentSize().height();
    const int lines = qBound(kMinLines, int(std::ceil(docLines)), kMaxLines);
    return QSize(QPlainTextEdit::sizeHint().width(), heightForLines(lines));
}

void ComposerEdit::keyPressEvent(QKeyEvent* e)
{
    const bool isReturn = (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter);
    if (isReturn && !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
        emit submitted();
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ChatComposer
// ─────────────────────────────────────────────────────────────────────────────

ChatComposer::ChatComposer(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("composerBar"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 0, 14, 12);
    outer->setSpacing(0);

    // ── Status band ─────────────────────────────────────────────────────────
    auto* band = new QWidget(this);
    band->setFixedHeight(kStatusBandPx);
    auto* bandLayout = new QVBoxLayout(band);
    bandLayout->setContentsMargins(6, 0, 6, 2);
    bandLayout->setSpacing(1);

    m_progress = new QProgressBar(band);
    m_progress->setObjectName(QStringLiteral("uploadBar"));
    m_progress->setFixedHeight(3);
    m_progress->setRange(0, 100);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);

    m_status = new QLabel(QString(), band);
    m_status->setObjectName(QStringLiteral("composerStatus"));

    bandLayout->addWidget(m_progress);
    bandLayout->addWidget(m_status);
    outer->addWidget(band);

    // ── Card ────────────────────────────────────────────────────────────────
    m_card = new QWidget(this);
    m_card->setAttribute(Qt::WA_StyledBackground, true);
    m_card->setObjectName(QStringLiteral("composerCard"));
    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(6, 6, 6, 6);
    cardLayout->setSpacing(6);

    // Reply chip
    m_replyChip = new QWidget(m_card);
    m_replyChip->setAttribute(Qt::WA_StyledBackground, true);
    m_replyChip->setObjectName(QStringLiteral("replyChip"));
    auto* chipRow = new QHBoxLayout(m_replyChip);
    chipRow->setContentsMargins(10, 5, 5, 5);
    chipRow->setSpacing(8);
    auto* chipCol = new QVBoxLayout();
    chipCol->setContentsMargins(0, 0, 0, 0);
    chipCol->setSpacing(0);
    m_replyName = new QLabel(m_replyChip);
    m_replyName->setObjectName(QStringLiteral("replyChipName"));
    m_replyText = new QLabel(m_replyChip);
    m_replyText->setObjectName(QStringLiteral("replyChipText"));
    chipCol->addWidget(m_replyName);
    chipCol->addWidget(m_replyText);
    auto* chipClose = new QPushButton(m_replyChip);
    chipClose->setObjectName(QStringLiteral("replyChipClose"));
    chipClose->setFixedSize(22, 22);
    chipClose->setCursor(Qt::PointingHandCursor);
    chipClose->setToolTip(tr("Cancel reply"));
    UiTheme::applyIcon(chipClose, QStringLiteral("close"), 12);
    connect(chipClose, &QPushButton::clicked, this, &ChatComposer::clearReply);
    chipRow->addLayout(chipCol, 1);
    chipRow->addWidget(chipClose, 0, Qt::AlignTop);
    m_replyChip->setVisible(false);
    cardLayout->addWidget(m_replyChip);

    // Input row
    m_inputRow = new QWidget(m_card);
    auto* row = new QHBoxLayout(m_inputRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    m_btnAttach = new QPushButton(m_inputRow);
    m_btnAttach->setObjectName(QStringLiteral("composerBtn"));
    m_btnAttach->setFixedSize(kBtnPx, kBtnPx);
    m_btnAttach->setCursor(Qt::PointingHandCursor);
    m_btnAttach->setToolTip(tr("Attach"));
    UiTheme::applyIcon(m_btnAttach, QStringLiteral("attach"), 17);
    connect(m_btnAttach, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        menu.addAction(UiTheme::icon(QStringLiteral("image")), tr("Photo"),
                       this, [this] { emit attachRequested(true); });
        menu.addAction(UiTheme::icon(QStringLiteral("file")), tr("File"),
                       this, [this] { emit attachRequested(false); });
        menu.exec(m_btnAttach->mapToGlobal(QPoint(0, -menu.sizeHint().height() - 4)));
    });

    m_input = new ComposerEdit(m_inputRow);
    connect(m_input, &ComposerEdit::submitted, this, &ChatComposer::emitSend);
    connect(m_input, &QPlainTextEdit::textChanged, this, [this] {
        updateSendState();
        if (!m_input->toPlainText().isEmpty()) emit typing();
    });

    m_btnVoice = new QPushButton(m_inputRow);
    m_btnVoice->setObjectName(QStringLiteral("composerBtn"));
    m_btnVoice->setFixedSize(kBtnPx, kBtnPx);
    m_btnVoice->setCheckable(true);
    m_btnVoice->setCursor(Qt::PointingHandCursor);
    m_btnVoice->setToolTip(tr("Record a voice note"));
    UiTheme::applyIcon(m_btnVoice, QStringLiteral("voice"), 17);
    connect(m_btnVoice, &QPushButton::clicked, this,
            [this](bool on) { emit voiceRecordToggled(on); });

    m_btnSend = new QPushButton(m_inputRow);
    m_btnSend->setObjectName(QStringLiteral("composerSend"));
    m_btnSend->setFixedSize(kBtnPx, kBtnPx);
    m_btnSend->setCursor(Qt::PointingHandCursor);
    m_btnSend->setToolTip(tr("Send"));
    UiTheme::applyIcon(m_btnSend, QStringLiteral("send"), 16);
    connect(m_btnSend, &QPushButton::clicked, this, &ChatComposer::emitSend);

    row->addWidget(m_btnAttach, 0, Qt::AlignBottom);
    row->addWidget(m_input, 1);
    row->addWidget(m_btnVoice, 0, Qt::AlignBottom);
    row->addWidget(m_btnSend, 0, Qt::AlignBottom);
    cardLayout->addWidget(m_inputRow);

    // ── Recording strip ─────────────────────────────────────────────────────
    m_recordStrip = new QWidget(m_card);
    auto* recRow = new QHBoxLayout(m_recordStrip);
    recRow->setContentsMargins(6, 0, 0, 0);
    recRow->setSpacing(10);

    m_recordDot = new QLabel(m_recordStrip);
    m_recordDot->setObjectName(QStringLiteral("recordDot"));
    m_recordDot->setFixedSize(10, 10);

    m_recordTime = new QLabel(QStringLiteral("0:00"), m_recordStrip);
    m_recordTime->setObjectName(QStringLiteral("recordTime"));

    auto* recHint = new QLabel(tr("Recording a voice note"), m_recordStrip);
    recHint->setObjectName(QStringLiteral("recordHint"));

    auto* recCancel = new QPushButton(tr("Cancel"), m_recordStrip);
    recCancel->setObjectName(QStringLiteral("recordCancel"));
    recCancel->setCursor(Qt::PointingHandCursor);
    connect(recCancel, &QPushButton::clicked, this, [this] {
        setRecording(false);
        emit voiceRecordCancelled();
    });

    auto* recSend = new QPushButton(m_recordStrip);
    recSend->setObjectName(QStringLiteral("composerSend"));
    recSend->setFixedSize(kBtnPx, kBtnPx);
    recSend->setCursor(Qt::PointingHandCursor);
    recSend->setToolTip(tr("Send voice note"));
    UiTheme::applyIcon(recSend, QStringLiteral("send"), 16);
    connect(recSend, &QPushButton::clicked, this, [this] {
        setRecording(false);
        emit voiceRecordToggled(false);
    });

    recRow->addWidget(m_recordDot);
    recRow->addWidget(m_recordTime);
    recRow->addWidget(recHint, 1);
    recRow->addWidget(recCancel);
    recRow->addWidget(recSend);
    m_recordStrip->setVisible(false);
    cardLayout->addWidget(m_recordStrip);

    m_recordTick = new QTimer(this);
    m_recordTick->setInterval(500);
    connect(m_recordTick, &QTimer::timeout, this, [this] {
        // m_recordHalfSecs counts half-second ticks: the parity pulses the dot,
        // the whole seconds drive the clock.
        ++m_recordHalfSecs;
        UiTheme::setClass(m_recordDot, (m_recordHalfSecs % 2) ? QStringLiteral("dim")
                                                              : QStringLiteral("lit"));
        if (m_recordHalfSecs % 2 == 0) {
            const int s = m_recordHalfSecs / 2;
            m_recordTime->setText(QStringLiteral("%1:%2")
                                      .arg(s / 60)
                                      .arg(s % 60, 2, 10, QLatin1Char('0')));
        }
    });

    outer->addWidget(m_card);

    setStatusText(QString());
    updateSendState();
}

// ─────────────────────────────────────────────────────────────────────────────

void ChatComposer::setPlaceholderText(const QString& text)
{
    if (m_input) m_input->setPlaceholderText(text);
}

void ChatComposer::setVoiceEnabled(bool on, const QString& disabledTip)
{
    if (!m_btnVoice) return;
    m_btnVoice->setEnabled(on);
    if (!on && !disabledTip.isEmpty()) m_btnVoice->setToolTip(disabledTip);
}

void ChatComposer::setReplyTarget(int64_t ts, const QString& name, const QString& snippet)
{
    m_replyTs      = ts;
    m_replyToName  = name;
    m_replySnippet = snippet;
    updateReplyChip();
    focusInput();
}

void ChatComposer::clearReply()
{
    m_replyTs = 0;
    m_replyToName.clear();
    m_replySnippet.clear();
    updateReplyChip();
}

void ChatComposer::updateReplyChip()
{
    const bool on = (m_replyTs != 0);
    m_replyChip->setVisible(on);
    if (!on) return;
    m_replyName->setText(m_replyToName.isEmpty() ? tr("Reply") : m_replyToName);
    m_replyText->setText(elide(m_replySnippet, 90));
}

void ChatComposer::setRecording(bool on)
{
    m_recording = on;
    m_recordStrip->setVisible(on);
    m_inputRow->setVisible(!on);
    if (m_btnVoice->isChecked() != on) m_btnVoice->setChecked(on);

    if (on) {
        m_recordHalfSecs = 0;
        m_recordTime->setText(QStringLiteral("0:00"));
        UiTheme::setClass(m_recordDot, QStringLiteral("lit"));
        m_recordTick->start();
    } else {
        m_recordTick->stop();
    }
}

void ChatComposer::setStatusText(const QString& text)
{
    if (!m_status) return;
    m_status->setText(text);
    // The band keeps its height either way; only the text comes and goes.
    m_status->setVisible(!text.isEmpty());
}

void ChatComposer::setProgress(int pct)
{
    if (!m_progress) return;
    if (pct < 0) {
        m_progress->setVisible(false);
        m_progress->setValue(0);
        return;
    }
    m_progress->setValue(qBound(0, pct, 100));
    m_progress->setVisible(true);
}

void ChatComposer::focusInput()
{
    if (m_input) m_input->setFocus();
}

void ChatComposer::clearInput()
{
    if (m_input) m_input->clear();
    clearReply();
}

void ChatComposer::setText(const QString& text)
{
    if (!m_input) return;
    m_input->setPlainText(text);
    m_input->moveCursor(QTextCursor::End);
    focusInput();
}

void ChatComposer::setReadOnly(bool ro)
{
    if (ro) {
        setRecording(false);
        clearReply();
    }
    m_card->setVisible(!ro);
}

void ChatComposer::updateSendState()
{
    const bool has = m_input && !m_input->toPlainText().trimmed().isEmpty();
    m_btnSend->setEnabled(has);
    UiTheme::setClass(m_btnSend, has ? QStringLiteral("active") : QStringLiteral("idle"));
}

void ChatComposer::emitSend()
{
    if (!m_input) return;
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) return;

    const int64_t ts = m_replyTs;
    const QString name = m_replyToName;
    const QString snippet = elide(m_replySnippet, 160);

    m_input->clear();
    clearReply();
    updateSendState();

    emit sendRequested(text, ts, name, snippet);
}
