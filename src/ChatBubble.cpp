#include "ChatBubble.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

#ifdef HAS_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#endif

#include "UiTheme.h"

namespace {

// The only geometry constants left. Padding, spacing and row heights now come
// from the layouts and from the fonts the stylesheet applies, rather than being
// restated as literals by whoever wants to know how tall a message is.
constexpr int kMinBubbleW = 96;
constexpr int kMaxBubbleW = 560;
constexpr int kBubblePadH = 13;   // keep in sync with #bubbleMine/#bubbleTheirs
constexpr int kAvatarPx   = 28;
constexpr int kActionsW   = 84;   // reserved so the hover row never reflows
constexpr int kImageMaxW  = 320;
constexpr int kImageMaxH  = 280;
constexpr int kFoldChars  = 300;

QString elide(const QString& s, int max)
{
    const QString flat = s.simplified();
    return flat.length() <= max ? flat : flat.left(max).trimmed() + QStringLiteral("...");
}

QString humanSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024) return QString::number(bytes / 1024.0 / 1024.0, 'f', 1) + " MB";
    if (bytes >= 1024)        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}

/// Escapes the message, then turns bare URLs into anchors. Anchors are styled
/// by colour only — no size change — so plain-text metrics still hold.
QString linkify(const QString& plain)
{
    static const QRegularExpression re(
        QStringLiteral("(https?://[^\\s<>\"']+|www\\.[^\\s<>\"']+)"),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    int last = 0;
    auto it = re.globalMatch(plain);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += plain.mid(last, m.capturedStart() - last).toHtmlEscaped();

        QString url = m.captured(0);
        QString tail;   // trailing sentence punctuation is rarely part of a link
        while (!url.isEmpty() && QStringLiteral(".,;:!?)").contains(url.back())) {
            tail.prepend(url.back());
            url.chop(1);
        }
        const QString href = url.startsWith(QLatin1String("www."), Qt::CaseInsensitive)
                             ? QStringLiteral("http://") + url
                             : url;
        out += QStringLiteral("<a href=\"%1\">%2</a>")
                   .arg(href.toHtmlEscaped(), url.toHtmlEscaped());
        out += tail.toHtmlEscaped();
        last = m.capturedEnd();
    }
    out += plain.mid(last).toHtmlEscaped();
    out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return out;
}

void saveBytesAs(const std::vector<uint8_t>& data, const QString& suggested)
{
    const QString path = QFileDialog::getSaveFileName(nullptr, QObject::tr("Save"), suggested);
    if (path.isEmpty()) return;
    QFile out(path);
    if (out.open(QIODevice::WriteOnly))
        out.write(reinterpret_cast<const char*>(data.data()), qint64(data.size()));
}

QPixmap roundedPixmap(const QPixmap& src, int radius)
{
    if (src.isNull()) return src;
    QPixmap out(src.size());
    out.setDevicePixelRatio(src.devicePixelRatio());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0, 0), src.deviceIndependentSize()), radius, radius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, src);
    return out;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

ChatBubble::ChatBubble(const ChatMessage& cm, BubbleShape shape, bool showAvatar,
                       QWidget* parent)
    : QWidget(parent)
    , m_kind(cm.kind)
    , m_shape(shape)
    , m_isMine(cm.isMine)
    , m_showAvatar(showAvatar && !cm.isMine)
    , m_ts(cm.timestamp)
    , m_senderName(QString::fromStdString(cm.fromName))
    , m_plainText(QString::fromStdString(cm.text))
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(10, 1, 10, 1);
    row->setSpacing(8);

    // System and call events are a centred chip, not a bubble. These used to
    // fall through buildBubble's `default: break;` and render as an empty
    // bubble containing nothing but a timestamp.
    if (cm.kind == MessageKind::System || cm.kind == MessageKind::CallEvent) {
        auto* chip = new QLabel(m_plainText, this);
        chip->setObjectName(QStringLiteral("systemChip"));
        chip->setAlignment(Qt::AlignCenter);
        chip->setWordWrap(true);
        row->addStretch();
        row->addWidget(chip);
        row->addStretch();
        m_bubble = chip;
        return;
    }

    if (m_showAvatar) {
        m_avatar = new QLabel(this);
        m_avatar->setObjectName(QStringLiteral("msgAvatar"));
        m_avatar->setFixedSize(kAvatarPx, kAvatarPx);
        m_avatar->setPixmap(avatarPixmap(m_senderName,
                                         QString::fromStdString(cm.fromId), kAvatarPx));
        // Hidden inside a run, but the gutter has to stay or the run would
        // step left under the avatar of its final message.
        QSizePolicy sp = m_avatar->sizePolicy();
        sp.setRetainSizeWhenHidden(true);
        m_avatar->setSizePolicy(sp);
    }

    // Hover actions. Always laid out, only shown on hover, so revealing them
    // cannot reflow the message.
    m_actions = new QWidget(this);
    m_actions->setObjectName(QStringLiteral("msgActions"));
    m_actions->setFixedWidth(kActionsW);
    auto* actLayout = new QHBoxLayout(m_actions);
    actLayout->setContentsMargins(0, 0, 0, 0);
    actLayout->setSpacing(2);
    struct ActionSpec { const char* icon; const char* tip; void (ChatBubble::*sig)(); };
    static const ActionSpec kActions[] = {
        {"back",   QT_TR_NOOP("Reply"),  &ChatBubble::replyRequested},
        {"copy",   QT_TR_NOOP("Copy"),   &ChatBubble::copyRequested},
        {"delete", QT_TR_NOOP("Delete"), &ChatBubble::deleteRequested},
    };
    for (const ActionSpec& a : kActions) {
        auto* b = new QPushButton(m_actions);
        b->setObjectName(QStringLiteral("msgActionBtn"));
        b->setFixedSize(24, 24);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tr(a.tip));
        UiTheme::applyIcon(b, QString::fromLatin1(a.icon), 13);
        connect(b, &QPushButton::clicked, this, a.sig);
        actLayout->addWidget(b);
    }
    // Reserved permanently: hiding a laid-out widget removes it from the row,
    // so without this the bubble would jump sideways on every hover.
    QSizePolicy actSp = m_actions->sizePolicy();
    actSp.setRetainSizeWhenHidden(true);
    m_actions->setSizePolicy(actSp);
    m_actions->setVisible(false);

    m_bubble = new QWidget(this);
    m_bubble->setAttribute(Qt::WA_StyledBackground, true);
    m_bubble->setObjectName(m_isMine ? QStringLiteral("bubbleMine")
                                     : QStringLiteral("bubbleTheirs"));
    auto* col = new QVBoxLayout(m_bubble);
    col->setContentsMargins(kBubblePadH, 7, kBubblePadH, 6);
    col->setSpacing(3);

    // Sender name — only at the top of a run, and only for other people.
    if (!cm.nameDisplay().empty()) {
        m_nameLabel = new QLabel(QString::fromStdString(cm.nameDisplay()), m_bubble);
        m_nameLabel->setObjectName(QStringLiteral("msgName"));
        col->addWidget(m_nameLabel);
    }

    buildQuote(cm);

    switch (cm.kind) {
    case MessageKind::Image:     buildImageBody(cm); break;
    case MessageKind::File:      buildAttachmentBody(cm, false); break;
    case MessageKind::VoiceNote: buildAttachmentBody(cm, true);  break;
    default:                     buildTextBody(cm);  break;
    }

    // Timestamp — once per run, on the last bubble. The rest carry it in a
    // tooltip, so the information stays reachable without the vertical cost.
    m_metaLabel = new QLabel(QString::fromStdString(cm.timeStr()), m_bubble);
    m_metaLabel->setObjectName(QStringLiteral("msgMeta"));
    m_metaLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    col->addWidget(m_metaLabel);

    setToolTip(QDateTime::fromMSecsSinceEpoch(cm.timestamp)
                   .toString(QStringLiteral("d MMM yyyy, HH:mm")));

    if (m_isMine) {
        row->addStretch();
        row->addWidget(m_actions, 0, Qt::AlignVCenter);
        row->addWidget(m_bubble, 0, Qt::AlignTop);
    } else {
        if (m_avatar) row->addWidget(m_avatar, 0, Qt::AlignBottom);
        row->addWidget(m_bubble, 0, Qt::AlignTop);
        row->addWidget(m_actions, 0, Qt::AlignVCenter);
        row->addStretch();
    }

    applyShapeClass();
}

void ChatBubble::buildQuote(const ChatMessage& cm)
{
    if (!cm.isReply()) return;

    m_quoteBox = new QWidget(m_bubble);
    m_quoteBox->setAttribute(Qt::WA_StyledBackground, true);
    m_quoteBox->setObjectName(QStringLiteral("msgQuote"));
    UiTheme::setClass(m_quoteBox, m_isMine ? QStringLiteral("mine") : QStringLiteral("theirs"));

    auto* qcol = new QVBoxLayout(m_quoteBox);
    qcol->setContentsMargins(8, 5, 8, 5);
    qcol->setSpacing(1);

    auto* who = new QLabel(QString::fromStdString(cm.replyName), m_quoteBox);
    who->setObjectName(QStringLiteral("msgQuoteName"));
    m_quoteText = new QLabel(elide(QString::fromStdString(cm.replySnippet), 120), m_quoteBox);
    m_quoteText->setObjectName(QStringLiteral("msgQuoteText"));
    m_quoteText->setWordWrap(true);
    qcol->addWidget(who);
    qcol->addWidget(m_quoteText);

    qobject_cast<QVBoxLayout*>(m_bubble->layout())->addWidget(m_quoteBox);
}

void ChatBubble::buildTextBody(const ChatMessage& cm)
{
    Q_UNUSED(cm);
    auto* col = qobject_cast<QVBoxLayout*>(m_bubble->layout());

    const bool isLong = m_plainText.length() > kFoldChars;
    const QString shortPlain = isLong
        ? m_plainText.left(kFoldChars).trimmed() + QStringLiteral("...")
        : m_plainText;

    m_richText = linkify(shortPlain);

    m_textLabel = new QLabel(m_richText, m_bubble);
    m_textLabel->setObjectName(QStringLiteral("msgText"));
    m_textLabel->setTextFormat(Qt::RichText);
    m_textLabel->setWordWrap(true);
    m_textLabel->setOpenExternalLinks(true);
    m_textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    // The context menu reads this back, because text() is now HTML.
    m_textLabel->setProperty("plainText", m_plainText);
    col->addWidget(m_textLabel);

    QTextDocument doc;
    doc.setDefaultFont(m_textLabel->font());
    doc.setDocumentMargin(0);
    doc.setHtml(m_richText);
    m_naturalBodyW = int(std::ceil(doc.idealWidth()));

    if (!isLong) return;

    auto* toggle = new QPushButton(tr("Show more"), m_bubble);
    toggle->setObjectName(QStringLiteral("msgToggle"));
    toggle->setFlat(true);
    toggle->setCursor(Qt::PointingHandCursor);
    toggle->setProperty("expanded", false);
    col->addWidget(toggle);

    const QString fullRich  = linkify(m_plainText);
    const QString shortRich = m_richText;
    connect(toggle, &QPushButton::clicked, this, [this, toggle, fullRich, shortRich]() {
        const bool expanded = !toggle->property("expanded").toBool();
        toggle->setProperty("expanded", expanded);
        toggle->setText(expanded ? tr("Show less") : tr("Show more"));
        m_richText = expanded ? fullRich : shortRich;
        m_textLabel->setText(m_richText);
        emit sizeChanged();   // ChatView re-measures and updates the item hint
    });
}

void ChatBubble::buildImageBody(const ChatMessage& cm)
{
    QPixmap px;
    if (!cm.data.empty())
        px.loadFromData(cm.data.data(), int(cm.data.size()));

    if (px.isNull()) {
        // Not decodable after all — fall back to the file card.
        buildAttachmentBody(cm, false);
        return;
    }

    auto* col = qobject_cast<QVBoxLayout*>(m_bubble->layout());

    const QPixmap scaled = px.scaled(kImageMaxW, kImageMaxH,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);

    auto* holder = new QWidget(m_bubble);
    holder->setFixedSize(scaled.size());

    auto* img = new QLabel(holder);
    img->setObjectName(QStringLiteral("msgImage"));
    img->setPixmap(roundedPixmap(scaled, 10));
    img->setFixedSize(scaled.size());

    // Save moved onto a hover overlay. It used to be a permanent button plus a
    // filename label, stacking two rows of chrome under every picture.
    auto* save = new QPushButton(holder);
    save->setObjectName(QStringLiteral("imgSaveBtn"));
    save->setFixedSize(26, 26);
    save->setCursor(Qt::PointingHandCursor);
    save->setToolTip(tr("Save image"));
    UiTheme::applyIcon(save, QStringLiteral("save"), 14);
    save->move(scaled.width() - 34, 8);
    save->setVisible(false);

    const std::vector<uint8_t> bytes = cm.data;
    const QString fname = QString::fromStdString(cm.fileName);
    connect(save, &QPushButton::clicked, this, [bytes, fname]() { saveBytesAs(bytes, fname); });

    holder->setToolTip(fname.isEmpty()
        ? humanSize(qint64(cm.data.size()))
        : fname + QStringLiteral("  -  ") + humanSize(qint64(cm.data.size())));

    col->addWidget(holder, 0, Qt::AlignLeft);
    m_naturalBodyW = scaled.width();
}

void ChatBubble::buildAttachmentBody(const ChatMessage& cm, bool voice)
{
    auto* col = qobject_cast<QVBoxLayout*>(m_bubble->layout());

    auto* card = new QWidget(m_bubble);
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setObjectName(QStringLiteral("msgAttachBox"));
    UiTheme::setClass(card, m_isMine ? QStringLiteral("mine") : QStringLiteral("theirs"));

    auto* line = new QHBoxLayout(card);
    line->setContentsMargins(8, 8, 8, 8);
    line->setSpacing(10);

    auto* tile = new QLabel(card);
    tile->setObjectName(QStringLiteral("attachTile"));
    tile->setFixedSize(36, 36);
    tile->setAlignment(Qt::AlignCenter);
    tile->setPixmap(UiTheme::icon(voice ? QStringLiteral("voice") : QStringLiteral("file"))
                        .pixmap(18, 18));

    auto* info = new QVBoxLayout();
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(1);

    const QString fname = QString::fromStdString(cm.fileName);
    auto* name = new QLabel(voice ? tr("Voice note")
                                  : (fname.isEmpty() ? tr("Attachment") : fname), card);
    name->setObjectName(QStringLiteral("attachName"));
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* sub = new QLabel(humanSize(qint64(cm.data.size())), card);
    sub->setObjectName(QStringLiteral("attachSize"));
    info->addWidget(name);
    info->addWidget(sub);

    auto* action = new QPushButton(card);
    action->setObjectName(QStringLiteral("attachAction"));
    action->setFixedSize(30, 30);
    action->setCursor(Qt::PointingHandCursor);
    action->setToolTip(voice ? tr("Play") : tr("Save file"));
    UiTheme::applyIcon(action, voice ? QStringLiteral("play") : QStringLiteral("save"), 14);

    const std::vector<uint8_t> bytes = cm.data;
    if (voice) {
        connect(action, &QPushButton::clicked, this, [bytes]() {
            if (bytes.empty()) return;
            const QString tmp = QDir::temp().filePath(
                QStringLiteral("localcall_voice_%1.wav")
                    .arg(QDateTime::currentMSecsSinceEpoch()));
            QFile out(tmp);
            if (!out.open(QIODevice::WriteOnly)) return;
            out.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()));
            out.close();
#ifdef HAS_MULTIMEDIA
            auto* player = new QMediaPlayer;
            auto* audio  = new QAudioOutput(player);
            player->setAudioOutput(audio);
            player->setSource(QUrl::fromLocalFile(tmp));
            QObject::connect(player, &QMediaPlayer::mediaStatusChanged, player,
                    [player](QMediaPlayer::MediaStatus st) {
                        if (st == QMediaPlayer::EndOfMedia || st == QMediaPlayer::InvalidMedia)
                            player->deleteLater();
                    });
            QObject::connect(player, &QMediaPlayer::errorOccurred, player,
                    [player](QMediaPlayer::Error, const QString&) { player->deleteLater(); });
            player->play();
#else
            QDesktopServices::openUrl(QUrl::fromLocalFile(tmp));
#endif
        });
    } else {
        connect(action, &QPushButton::clicked, this,
                [bytes, fname]() { saveBytesAs(bytes, fname); });
    }

    line->addWidget(tile);
    line->addLayout(info, 1);
    line->addWidget(action);
    col->addWidget(card);

    m_naturalBodyW = qMax(card->sizeHint().width(), 220);
}

// ─────────────────────────────────────────────────────────────────────────────

void ChatBubble::applyShapeClass()
{
    if (!m_bubble) return;

    const char* cls = "solo";
    switch (m_shape) {
    case BubbleShape::Solo:   cls = "solo";  break;
    case BubbleShape::First:  cls = "first"; break;
    case BubbleShape::Middle: cls = "mid";   break;
    case BubbleShape::Last:   cls = "last";  break;
    }
    // The objectName already says which side it is; the class only carries the
    // position in the run, which is what the corner radii key off.
    UiTheme::setClass(m_bubble, QString::fromLatin1(cls));

    const bool startOfRun = (m_shape == BubbleShape::Solo || m_shape == BubbleShape::First);
    const bool endOfRun   = (m_shape == BubbleShape::Solo || m_shape == BubbleShape::Last);
    if (m_metaLabel) m_metaLabel->setVisible(endOfRun);
    if (m_avatar)    m_avatar->setVisible(endOfRun);
    if (m_nameLabel) m_nameLabel->setVisible(startOfRun);
}

bool ChatBubble::setShape(BubbleShape shape)
{
    if (m_shape == shape) return false;
    m_shape = shape;
    applyShapeClass();
    return true;
}

int ChatBubble::preferredInnerWidth() const
{
    int w = m_naturalBodyW;
    if (m_nameLabel && m_nameLabel->isVisible())
        w = qMax(w, m_nameLabel->sizeHint().width());
    if (m_metaLabel && m_metaLabel->isVisible())
        w = qMax(w, m_metaLabel->sizeHint().width());
    if (m_quoteText)
        w = qMax(w, qMin(m_quoteText->sizeHint().width() + 16, 320));
    return w + 2 * kBubblePadH;
}

int ChatBubble::measureRichTextHeight(const QString& html, const QFont& font, int width) const
{
    QTextDocument doc;
    doc.setDefaultFont(font);
    doc.setDocumentMargin(0);
    doc.setHtml(html);
    doc.setTextWidth(width);
    return int(std::ceil(doc.size().height()));
}

int ChatBubble::layoutForWidth(int outerW)
{
    if (!m_bubble || !layout()) return 0;

    // System chips span the row and simply wrap.
    if (m_kind == MessageKind::System || m_kind == MessageKind::CallEvent) {
        m_bubble->setMaximumWidth(qMax(kMinBubbleW, outerW - 80));
        layout()->activate();
        return layout()->totalSizeHint().height();
    }

    // Row margins, the avatar column and the hover-action column come off the
    // available width before the bubble gets a say.
    int reserved = 20 + kActionsW + 8;
    if (m_showAvatar) reserved += kAvatarPx + 8;
    const int avail = qMax(kMinBubbleW, qMin(outerW - reserved, kMaxBubbleW));

    const int bubbleW = qBound(kMinBubbleW, preferredInnerWidth(), avail);
    m_bubble->setFixedWidth(bubbleW);

    const int innerW = bubbleW - 2 * kBubblePadH;

    // Fixing each wrapping label's height hands the remaining arithmetic —
    // padding, spacing, the meta row — back to the layout.
    if (m_textLabel) {
        m_textLabel->setFixedWidth(innerW);
        m_textLabel->setFixedHeight(
            measureRichTextHeight(m_richText, m_textLabel->font(), innerW));
    }
    if (m_quoteText) {
        const int quoteW = qMax(32, innerW - 16);   // quote box padding
        m_quoteText->setFixedWidth(quoteW);
        m_quoteText->setFixedHeight(
            measureRichTextHeight(m_quoteText->text().toHtmlEscaped(),
                                  m_quoteText->font(), quoteW));
    }

    m_bubble->layout()->activate();
    m_bubble->setFixedHeight(m_bubble->layout()->totalSizeHint().height());

    layout()->activate();
    return layout()->totalSizeHint().height();
}

void ChatBubble::enterEvent(QEnterEvent* e)
{
    if (m_actions) m_actions->setVisible(true);
    const auto overlays = findChildren<QPushButton*>(QStringLiteral("imgSaveBtn"));
    for (QPushButton* b : overlays) b->setVisible(true);
    QWidget::enterEvent(e);
}

void ChatBubble::leaveEvent(QEvent* e)
{
    if (m_actions) m_actions->setVisible(false);
    const auto overlays = findChildren<QPushButton*>(QStringLiteral("imgSaveBtn"));
    for (QPushButton* b : overlays) b->setVisible(false);
    QWidget::leaveEvent(e);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Static helpers
// ─────────────────────────────────────────────────────────────────────────────

QPixmap ChatBubble::avatarPixmap(const QString& name, const QString& id, int px)
{
    static const QRgb kTints[] = {
        UiTheme::Color::Mauve, UiTheme::Color::Blue,  UiTheme::Color::Green,
        UiTheme::Color::Red,   UiTheme::Color::Peach,
    };
    const QByteArray h = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Md5);
    const QColor tint(kTints[quint8(h.at(0)) % (sizeof(kTints) / sizeof(kTints[0]))]);

    QString initials;
    const QStringList parts = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!parts.isEmpty()) initials += parts.first().at(0).toUpper();
    if (parts.size() > 1) initials += parts.at(1).at(0).toUpper();
    if (initials.isEmpty()) initials = QStringLiteral("?");

    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(tint);
    p.drawEllipse(QRectF(0, 0, px, px));

    QFont f = qApp ? qApp->font() : QFont();
    f.setPixelSize(int(px * 0.42));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(UiTheme::Color::Crust));
    p.drawText(QRectF(0, 0, px, px), Qt::AlignCenter, initials);
    return pm;
}

QWidget* ChatBubble::makeDayChip(const QDate& date, QWidget* parent)
{
    const QDate today = QDate::currentDate();
    QString text;
    if (date == today)                    text = tr("Today");
    else if (date == today.addDays(-1))   text = tr("Yesterday");
    else if (date.daysTo(today) < 7)      text = date.toString(QStringLiteral("dddd"));
    else if (date.year() == today.year()) text = date.toString(QStringLiteral("d MMMM"));
    else                                  text = date.toString(QStringLiteral("d MMMM yyyy"));

    auto* row = new QWidget(parent);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(10, 10, 10, 6);
    auto* chip = new QLabel(text, row);
    chip->setObjectName(QStringLiteral("dayChip"));
    chip->setAlignment(Qt::AlignCenter);
    l->addStretch();
    l->addWidget(chip);
    l->addStretch();
    return row;
}

QWidget* ChatBubble::makeTypingRow(const QString& name, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(10, 2, 10, 2);
    l->setSpacing(8);

    auto* bubble = new QLabel(row);
    bubble->setAttribute(Qt::WA_StyledBackground, true);
    bubble->setObjectName(QStringLiteral("typingBubble"));
    bubble->setAlignment(Qt::AlignCenter);
    bubble->setToolTip(name.isEmpty() ? QString() : tr("%1 is typing").arg(name));
    bubble->setText(QStringLiteral("•  "));

    // Dots cycle in place, so the row never changes width.
    auto* timer = new QTimer(bubble);
    timer->setInterval(380);
    QObject::connect(timer, &QTimer::timeout, bubble, [bubble]() {
        static const char* kFrames[] = {"•  ", "•• ", "•••", " ••"};
        const int step = (bubble->property("frame").toInt() + 1) % 4;
        bubble->setProperty("frame", step);
        bubble->setText(QString::fromUtf8(kFrames[step]));
    });
    timer->start();

    l->addWidget(bubble);
    l->addStretch();
    return row;
}
