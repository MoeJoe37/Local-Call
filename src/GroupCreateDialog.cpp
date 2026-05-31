#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "GroupCreateDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QMessageBox>
#include <QIcon>

GroupCreateDialog::GroupCreateDialog(const QList<FriendInfo*>& onlineFriends, QWidget* parent)
    : QDialog(parent), m_friends(onlineFriends)
{
    setWindowTitle("Create Group");
    setModal(true);
    setMinimumWidth(340);

    setStyleSheet(R"(
        QDialog    { background: #1E1E2E; }
        QLabel     { color: #CDD6F4; font-size: 13px; }
        QLineEdit  {
            background: #313244; color: #CDD6F4; border: 1px solid #45475A;
            border-radius: 4px; padding: 6px 10px; font-size: 13px;
        }
        QLineEdit:focus { border-color: #CBA6F7; }
        QListWidget {
            background: #181825; color: #CDD6F4; border: 1px solid #313244;
            border-radius: 4px; font-size: 13px;
        }
        QListWidget::item { padding: 6px 8px; }
        QListWidget::item:hover { background: #313244; }
        QListWidget::item:selected { background: #45475A; }
        QPushButton {
            background: #CBA6F7; color: #11111B; border: none;
            border-radius: 4px; padding: 7px 20px; font-size: 13px;
        }
        QPushButton:hover { background: #B4BEFE; }
        QPushButton#cancel { background: #313244; color: #CDD6F4; }
        QPushButton#cancel:hover { background: #45475A; }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(10);

    root->addWidget(new QLabel("Group name:", this));
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Enter group name…");
    root->addWidget(m_nameEdit);

    root->addWidget(new QLabel("Select members:", this));
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::MultiSelection);
    for (auto* f : onlineFriends) {
        auto* item = new QListWidgetItem(QIcon(":/icons/check.png"), QString::fromStdString(f->name), m_list);
        item->setData(Qt::UserRole, QVariant::fromValue<void*>(f));
    }
    root->addWidget(m_list);

    auto* row = new QHBoxLayout();
    row->addStretch();
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setObjectName("cancel");
    auto* create = new QPushButton("Create", this);
    row->addWidget(cancel);
    row->addWidget(create);
    root->addLayout(row);

    connect(create, &QPushButton::clicked, this, &GroupCreateDialog::tryCreate);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &GroupCreateDialog::tryCreate);

    m_nameEdit->setFocus();
}

void GroupCreateDialog::tryCreate()
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Create Group", "Please enter a group name.");
        return;
    }

    m_selected.clear();
    for (auto* item : m_list->selectedItems()) {
        auto* f = static_cast<FriendInfo*>(item->data(Qt::UserRole).value<void*>());
        if (f) m_selected.append(f);
    }

    if (m_selected.isEmpty()) {
        QMessageBox::warning(this, "Create Group", "Please select at least one member.");
        return;
    }

    m_groupName = name;
    accept();
}

QString GroupCreateDialog::groupName() const { return m_groupName; }
QList<FriendInfo*> GroupCreateDialog::selected() const { return m_selected; }
