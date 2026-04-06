#pragma once
#include <QDialog>
#include <QList>
#include <QString>
#include <functional>
#include "GroupInfo.h"
#include "FriendInfo.h"

class QVBoxLayout;
class QWidget;
class QComboBox;

class GroupManageDialog : public QDialog {
    Q_OBJECT
public:
    GroupManageDialog(GroupInfo* group, const QString& myId,
                      QList<FriendInfo>& allFriends, QWidget* parent = nullptr);

    // Actions to execute after dialog closes (signaling calls)
    QList<std::function<void()>> pendingActions;
    bool wasDeleted = false;

private:
    void rebuild();
    void queuePermChange(const std::string& memberId);

    GroupInfo*        m_group;
    QString           m_myId;
    QList<FriendInfo>& m_allFriends;

    QWidget*    m_memberRows  = nullptr;
    QVBoxLayout* m_memberLayout = nullptr;
    QComboBox*  m_addCombo    = nullptr;
};
