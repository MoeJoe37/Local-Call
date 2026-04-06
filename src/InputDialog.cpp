#include "InputDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QKeyEvent>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

InputDialog::InputDialog(const QString& title, const QString& label,
                         const QString& defaultText, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(320);

    setStyleSheet(R"(
        QDialog { background: #1E1E2E; }
        QLabel  { color: #CDD6F4; font-size:13px; }
        QLineEdit {
            background: #313244; color: #CDD6F4; border: 1px solid #45475A;
            border-radius: 4px; padding: 6px 10px; font-size: 13px;
        }
        QLineEdit:focus { border-color: #CBA6F7; }
        QPushButton {
            background: #CBA6F7; color: #11111B; border: none;
            border-radius: 4px; padding: 7px 20px; font-size: 13px;
        }
        QPushButton:hover { background: #B4BEFE; }
        QPushButton#cancel {
            background: #313244; color: #CDD6F4;
        }
        QPushButton#cancel:hover { background: #45475A; }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(10);

    root->addWidget(new QLabel(label, this));

    m_edit = new QLineEdit(defaultText, this);
    m_edit->selectAll();
    root->addWidget(m_edit);

    auto* row = new QHBoxLayout();
    row->addStretch();
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setObjectName("cancel");
    auto* ok = new QPushButton("OK", this);
    row->addWidget(cancel);
    row->addWidget(ok);
    root->addLayout(row);

    connect(ok, &QPushButton::clicked, this, [this]() {
        m_result = m_edit->text().trimmed();
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_edit, &QLineEdit::returnPressed, ok, &QPushButton::click);

    m_edit->setFocus();

#ifdef Q_OS_WIN
    // Apply dark title bar once the window handle exists (after first show event)
    QTimer::singleShot(0, this, [this]() {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (!hwnd) return;
        BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    });
#endif
}

QString InputDialog::result() const { return m_result; }
