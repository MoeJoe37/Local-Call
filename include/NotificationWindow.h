#pragma once
#include <QDialog>
#include <QString>
#include <QList>
#include <functional>

class QLabel;
class QHBoxLayout;
class QTimer;

class NotificationWindow : public QDialog {
    Q_OBJECT
public:
    /// Accept/Reject buttons are tinted by the theme; Neutral keeps the
    /// default grey. Existing two-element brace initialisers stay valid.
    struct Button {
        enum Kind { Neutral, Accept, Reject };
        QString               label;
        std::function<void()> action;
        Kind                  kind{Neutral};
    };

    // With buttons (persistent until dismissed)
    explicit NotificationWindow(const QString& title, const QString& body,
                                const QList<Button>& buttons,
                                QWidget* parent = nullptr);
    // Auto-close toast
    explicit NotificationWindow(const QString& title, const QString& body,
                                int autoCloseSec = 4,
                                QWidget* parent = nullptr);

private:
    void build(const QString& title, const QString& body,
               const QList<Button>& buttons, int autoCloseSec);
    void positionBottomRight();
};
