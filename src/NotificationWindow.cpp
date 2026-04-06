#include "NotificationWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QGraphicsDropShadowEffect>

NotificationWindow::NotificationWindow(const QString& title, const QString& body,
                                       const QList<Button>& buttons, QWidget* parent)
    : QDialog(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    build(title, body, buttons, 0);
}

NotificationWindow::NotificationWindow(const QString& title, const QString& body,
                                       int autoCloseSec, QWidget* parent)
    : QDialog(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    build(title, body, {}, autoCloseSec);
}

void NotificationWindow::build(const QString& title, const QString& body,
                                const QList<Button>& buttons, int autoCloseSec)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);

    // Styling
    setStyleSheet(R"(
        QDialog {
            background: #1E1E2E;
            border: 1px solid #44475A;
            border-radius: 8px;
        }
        QLabel#title {
            color: #CDD6F4;
            font-size: 13px;
            font-weight: bold;
        }
        QLabel#body {
            color: #A6ADC8;
            font-size: 12px;
        }
        QPushButton {
            background: #313244;
            color: #CDD6F4;
            border: none;
            border-radius: 4px;
            padding: 6px 14px;
            font-size: 12px;
            min-width: 64px;
        }
        QPushButton:hover { background: #45475A; }
    )");

    auto* root   = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(6);

    auto* lTitle = new QLabel(title, this);
    lTitle->setObjectName("title");
    root->addWidget(lTitle);

    auto* lBody = new QLabel(body, this);
    lBody->setObjectName("body");
    lBody->setWordWrap(true);
    root->addWidget(lBody);

    if (!buttons.isEmpty()) {
        auto* row = new QHBoxLayout();
        row->addStretch();
        for (const auto& btn : buttons) {
            auto* pb = new QPushButton(btn.label, this);
            auto  fn = btn.action;
            connect(pb, &QPushButton::clicked, this, [this, fn]() {
                if (fn) fn();
                close();
            });
            row->addWidget(pb);
        }
        root->addLayout(row);
    }

    setFixedWidth(300);
    adjustSize();

    if (autoCloseSec > 0) {
        auto* t = new QTimer(this);
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, &QDialog::close);
        t->start(autoCloseSec * 1000);
    }

    // Drop shadow
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(16);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 120));
    setGraphicsEffect(shadow);

    // Position after show
    QTimer::singleShot(0, this, &NotificationWindow::positionBottomRight);
}

void NotificationWindow::positionBottomRight()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect wa = screen->availableGeometry();
    move(wa.right() - width() - 16, wa.bottom() - height() - 16);
}
