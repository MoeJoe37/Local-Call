#pragma once
#include <QDialog>
#include <QList>
#include <QString>
#include "FriendInfo.h"

class QLineEdit;
class QListWidget;

class GroupCreateDialog : public QDialog {
    Q_OBJECT
public:
    GroupCreateDialog(const QList<FriendInfo*>& onlineFriends, QWidget* parent = nullptr);

    QString            groupName() const;
    QList<FriendInfo*> selected()  const;

private:
    void tryCreate();

    QLineEdit*                m_nameEdit = nullptr;
    QListWidget*              m_list     = nullptr;
    const QList<FriendInfo*>& m_friends;
    QString                   m_groupName;
    QList<FriendInfo*>        m_selected;
};
