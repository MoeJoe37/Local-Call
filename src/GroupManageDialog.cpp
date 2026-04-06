#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "GroupManageDialog.h"
#include "SignalingClient.h"
#include "Helpers.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollArea>
#include <QMessageBox>
#include <QFrame>
#include <algorithm>

static const QString BASE_SS = R"(
    QDialog    { background: #1E1E2E; }
    QLabel     { color: #CDD6F4; font-size: 13px; }
    QScrollArea { background: #181825; border: none; }
    QWidget#memberContainer { background: #181825; }
    QFrame#memberRow { background: #1E1E2E; border-bottom: 1px solid #313244; }
    QPushButton.action {
        background: #313244; color: #A6ADC8; border: none;
        border-radius: 3px; padding: 3px 10px; font-size: 11px;
    }
    QPushButton.action:hover { background: #45475A; }
    QPushButton.danger {
        background: #2A1010; color: #CF4444; border: none;
        border-radius: 3px; padding: 3px 10px; font-size: 11px;
    }
    QPushButton.danger:hover { background: #3D1515; }
    QPushButton#addBtn {
        background: #CBA6F7; color: #11111B; border: none;
        border-radius: 4px; padding: 6px 14px; font-size: 12px;
    }
    QPushButton#addBtn:hover { background: #B4BEFE; }
    QPushButton#deleteBtn {
        background: #2A1010; color: #CF4444; border: none;
        border-radius: 4px; padding: 6px 14px; font-size: 12px;
    }
    QPushButton#deleteBtn:hover { background: #3D1515; }
    QCheckBox  { color: #A6ADC8; font-size: 11px; }
    QComboBox  {
        background: #313244; color: #CDD6F4; border: 1px solid #45475A;
        border-radius: 4px; padding: 4px 8px; font-size: 12px;
    }
)";

GroupManageDialog::GroupManageDialog(GroupInfo* group, const QString& myId,
                                     QList<FriendInfo>& allFriends, QWidget* parent)
    : QDialog(parent), m_group(group), m_myId(myId), m_allFriends(allFriends)
{
    setWindowTitle(QString("Manage: %1").arg(QString::fromStdString(group->name)));
    setModal(true);
    setMinimumWidth(520);
    setMinimumHeight(420);
    setStyleSheet(BASE_SS);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    // Title row
    auto* titleRow = new QHBoxLayout();
    auto* titleLbl = new QLabel(QString::fromStdString(group->name), this);
    titleLbl->setStyleSheet("font-size:16px; font-weight:bold; color:#CDD6F4;");
    QString roleStr = group->isOwner(myId.toStdString()) ? "You are the owner"
                    : group->isHelper(myId.toStdString()) ? "You are a helper" : "Member";
    auto* roleLbl = new QLabel(roleStr, this);
    roleLbl->setStyleSheet("color:#A6ADC8; font-size:12px;");
    titleRow->addWidget(titleLbl);
    titleRow->addStretch();
    titleRow->addWidget(roleLbl);
    root->addLayout(titleRow);

    // Scrollable member list
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_memberRows = new QWidget();
    m_memberRows->setObjectName("memberContainer");
    m_memberLayout = new QVBoxLayout(m_memberRows);
    m_memberLayout->setContentsMargins(0,0,0,0);
    m_memberLayout->setSpacing(0);
    m_memberLayout->addStretch();
    scroll->setWidget(m_memberRows);
    root->addWidget(scroll, 1);

    // Add member row (owner only)
    if (group->isOwner(myId.toStdString())) {
        auto* addRow = new QHBoxLayout();
        m_addCombo = new QComboBox(this);
        for (auto& f : allFriends) {
            bool inGroup = std::find(group->memberIds.begin(), group->memberIds.end(), f.id)
                           != group->memberIds.end();
            if (!inGroup && f.isOnline) {
                m_addCombo->addItem(QString::fromStdString(f.name),
                                    QString::fromStdString(f.id));
            }
        }
        auto* addBtn = new QPushButton("Add Member", this);
        addBtn->setObjectName("addBtn");
        addRow->addWidget(m_addCombo, 1);
        addRow->addWidget(addBtn);
        root->addLayout(addRow);

        connect(addBtn, &QPushButton::clicked, this, [this]() {
            int idx = m_addCombo->currentIndex();
            if (idx < 0) return;
            QString friendId = m_addCombo->currentData().toString();
            FriendInfo* friend_ = nullptr;
            for (auto& f : m_allFriends) if (f.id == friendId.toStdString()) { friend_ = &f; break; }
            if (!friend_) return;

            m_group->memberIds.push_back(friend_->id);
            m_group->members.push_back(friend_);

            std::vector<MemberDto> dtos;
            for (auto* m : m_group->members)
                dtos.push_back({m->id, m->name, m->ip});

            FriendInfo* fCopy = friend_;
            GroupInfo*  gCopy = m_group;
            QString     myId  = m_myId;
            pendingActions.append([fCopy, gCopy, myId, dtos]() {
                SigMsg sig;
                sig.type       = SigType::GrpInv;
                sig.from_id    = myId.toStdString();
                sig.from_name  = "";
                sig.group_id   = gCopy->groupId;
                sig.group_name = gCopy->name;
                sig.members    = dtos;
                sig.ts         = Helpers::nowMs();
                SignalingClient::send(QString::fromStdString(fCopy->ip), sig);
            });
            rebuild();
            // Refresh combo
            m_addCombo->clear();
            for (auto& f : m_allFriends) {
                bool in = std::find(m_group->memberIds.begin(), m_group->memberIds.end(), f.id)
                          != m_group->memberIds.end();
                if (!in && f.isOnline)
                    m_addCombo->addItem(QString::fromStdString(f.name),
                                        QString::fromStdString(f.id));
            }
        });

        // Delete group button
        auto* bottomRow = new QHBoxLayout();
        bottomRow->addStretch();
        auto* delBtn = new QPushButton("🗑  Delete Group", this);
        delBtn->setObjectName("deleteBtn");
        bottomRow->addWidget(delBtn);
        root->addLayout(bottomRow);

        connect(delBtn, &QPushButton::clicked, this, [this]() {
            if (QMessageBox::question(this, "Delete Group",
                    QString("Delete group \"%1\"?\nThis will remove it for all members.")
                    .arg(QString::fromStdString(m_group->name)),
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

            for (const auto& memberId : m_group->memberIds) {
                FriendInfo* f = nullptr;
                for (auto& fi : m_allFriends) if (fi.id == memberId) { f = &fi; break; }
                if (!f) continue;
                GroupInfo* g  = m_group;
                QString   mid = m_myId;
                FriendInfo* fCopy = f;
                pendingActions.append([fCopy, g, mid]() {
                    SigMsg sig;
                    sig.type     = SigType::GrpDelete;
                    sig.from_id  = mid.toStdString();
                    sig.group_id = g->groupId;
                    sig.ts       = Helpers::nowMs();
                    SignalingClient::send(QString::fromStdString(fCopy->ip), sig);
                });
            }
            wasDeleted = true;
            accept();
        });
    }

    rebuild();
}

void GroupManageDialog::rebuild()
{
    // Clear existing rows (leave the stretch at the end)
    while (m_memberLayout->count() > 1) {
        auto* item = m_memberLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    bool actorIsOwner  = m_group->isOwner(m_myId.toStdString());
    bool actorIsHelper = m_group->isHelper(m_myId.toStdString());

    for (const auto& memberId : m_group->memberIds) {
        FriendInfo* friend_ = nullptr;
        for (auto& f : m_allFriends) if (f.id == memberId) { friend_ = &f; break; }
        if (!friend_) continue;

        GroupPermissions perms = m_group->getPermissions(memberId);
        bool isOwner  = m_group->isOwner(memberId);
        bool isHelper = m_group->isHelper(memberId);
        bool canAct   = m_group->canManage(m_myId.toStdString(), memberId)
                        && memberId != m_myId.toStdString();
        bool isMe     = (memberId == m_myId.toStdString());

        auto* row = new QFrame(m_memberRows);
        row->setObjectName("memberRow");
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 8);

        QString badge = isOwner ? " 👑" : isHelper ? " 🛡" : isMe ? " (you)" : "";
        auto* nameLbl = new QLabel(QString::fromStdString(friend_->name) + badge, row);
        nameLbl->setStyleSheet(QString("color:%1; font-size:13px;").arg(isMe ? "#CBA6F7" : "#CDD6F4"));
        rowLayout->addWidget(nameLbl, 1);

        if (canAct) {
            // Permission checkboxes
            auto makeChk = [&](const QString& icon, bool init,
                                std::function<void(bool)> onChange) -> QCheckBox* {
                auto* cb = new QCheckBox(icon, row);
                cb->setChecked(init);
                connect(cb, &QCheckBox::toggled, row, [onChange](bool v){ onChange(v); });
                return cb;
            };

            std::string mid = memberId;
            rowLayout->addWidget(makeChk("💬", perms.canSendMessages, [this, mid](bool v){
                m_group->permissions[mid].canSendMessages = v;
                queuePermChange(mid);
            }));
            rowLayout->addWidget(makeChk("📎", perms.canSendFiles, [this, mid](bool v){
                m_group->permissions[mid].canSendFiles = v;
                queuePermChange(mid);
            }));
            rowLayout->addWidget(makeChk("📞", perms.canStartCalls, [this, mid](bool v){
                m_group->permissions[mid].canStartCalls = v;
                queuePermChange(mid);
            }));

            if (actorIsOwner && !isOwner) {
                if (!isHelper) {
                    auto* promote = new QPushButton("🛡 Promote", row);
                    promote->setProperty("class", "action");
                    std::string fip = friend_->ip;
                    connect(promote, &QPushButton::clicked, this, [this, mid, fip](){
                        m_group->helperIds.push_back(mid);
                        GroupInfo* g  = m_group;
                        QString    myId = m_myId;
                        pendingActions.append([fip, g, mid, myId](){
                            SigMsg sig;
                            sig.type      = SigType::GrpPromote;
                            sig.from_id   = myId.toStdString();
                            sig.group_id  = g->groupId;
                            sig.target_id = mid;
                            sig.ts        = Helpers::nowMs();
                            SignalingClient::send(QString::fromStdString(fip), sig);
                        });
                        rebuild();
                    });
                    rowLayout->addWidget(promote);
                } else {
                    auto* demote = new QPushButton("⬇ Demote", row);
                    demote->setProperty("class", "action");
                    std::string fip = friend_->ip;
                    connect(demote, &QPushButton::clicked, this, [this, mid, fip](){
                        m_group->helperIds.erase(
                            std::remove(m_group->helperIds.begin(), m_group->helperIds.end(), mid),
                            m_group->helperIds.end());
                        GroupInfo* g  = m_group;
                        QString   myId = m_myId;
                        pendingActions.append([fip, g, mid, myId](){
                            SigMsg sig;
                            sig.type      = SigType::GrpDemote;
                            sig.from_id   = myId.toStdString();
                            sig.group_id  = g->groupId;
                            sig.target_id = mid;
                            sig.ts        = Helpers::nowMs();
                            SignalingClient::send(QString::fromStdString(fip), sig);
                        });
                        rebuild();
                    });
                    rowLayout->addWidget(demote);
                }

                auto* kick = new QPushButton("✕ Kick", row);
                kick->setProperty("class", "danger");
                std::string fname = friend_->name;
                std::string fip   = friend_->ip;
                connect(kick, &QPushButton::clicked, this, [this, mid, fname, fip](){
                    if (QMessageBox::question(this, "Kick",
                        QString("Remove %1 from the group?").arg(QString::fromStdString(fname)),
                        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
                    m_group->memberIds.erase(
                        std::remove(m_group->memberIds.begin(), m_group->memberIds.end(), mid),
                        m_group->memberIds.end());
                    m_group->members.erase(
                        std::remove_if(m_group->members.begin(), m_group->members.end(),
                            [&mid](FriendInfo* f){ return f->id == mid; }),
                        m_group->members.end());
                    GroupInfo* g  = m_group;
                    QString    myId = m_myId;
                    pendingActions.append([fip, g, mid, myId](){
                        SigMsg sig;
                        sig.type      = SigType::GrpKick;
                        sig.from_id   = myId.toStdString();
                        sig.group_id  = g->groupId;
                        sig.target_id = mid;
                        sig.ts        = Helpers::nowMs();
                        SignalingClient::send(QString::fromStdString(fip), sig);
                    });
                    rebuild();
                });
                rowLayout->addWidget(kick);
            }
        }

        // Insert before the stretch
        m_memberLayout->insertWidget(m_memberLayout->count() - 1, row);
    }
}

void GroupManageDialog::queuePermChange(const std::string& memberId)
{
    FriendInfo* friend_ = nullptr;
    for (auto& f : m_allFriends) if (f.id == memberId) { friend_ = &f; break; }
    if (!friend_) return;

    GroupPermissions perms = m_group->getPermissions(memberId);
    if (m_group->permissions.find(memberId) == m_group->permissions.end())
        m_group->permissions[memberId] = perms;

    std::string fip = friend_->ip;
    GroupInfo*  g   = m_group;
    QString     myId = m_myId;
    pendingActions.append([fip, g, memberId, perms, myId](){
        SigMsg sig;
        sig.type      = SigType::GrpPerm;
        sig.from_id   = myId.toStdString();
        sig.group_id  = g->groupId;
        sig.target_id = memberId;
        sig.perm_msg  = perms.canSendMessages;
        sig.perm_file = perms.canSendFiles;
        sig.perm_call = perms.canStartCalls;
        sig.ts        = Helpers::nowMs();
        SignalingClient::send(QString::fromStdString(fip), sig);
    });
}
