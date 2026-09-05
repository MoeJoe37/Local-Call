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

    setObjectName("groupCreateDialog");   // styled in localcall.qss

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
