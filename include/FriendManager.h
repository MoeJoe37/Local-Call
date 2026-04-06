#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QSet>
#include <QStandardPaths>
#include "FriendInfo.h"
#include "GroupInfo.h"

class FriendManager : public QObject {
    Q_OBJECT
public:
    explicit FriendManager(QObject* parent = nullptr);

    // Friends
    bool        hasFriend(const QString& id) const;
    FriendInfo* getFriend(const QString& id);
    void        addFriend(const FriendInfo& f);
    void        removeFriend(const QString& id);
    void        removeFormerFriend(const QString& id);
    void        updateFriendIp(const QString& id, const QString& ip);
    void        saveFriendsDirect() { saveFriends(); }  // expose for DHCP update

    // Former friends
    const QList<FriendInfo>& formerFriends() const { return m_formerFriends; }

    // Pending requests
    bool hasPending(const QString& fromId) const;
    void addPending(const PendingRequest& req);
    void removePending(const QString& fromId);

    // Blocked
    bool isBlocked(const QString& id) const;
    void block(const QString& id);
    void unblock(const QString& id);

    // Groups
    void      addGroup(const GroupInfo& g);
    void      removeGroup(const QString& groupId);
    GroupInfo* getGroup(const QString& groupId);
    void      saveGroups();

    // Accessors (for UI binding)
    QList<FriendInfo>&    friends()     { return m_friends; }
    QList<GroupInfo>&     groups()      { return m_groups; }
    QList<PendingRequest>& pending()    { return m_pending; }

signals:
    void friendsChanged();
    void groupsChanged();
    void pendingChanged();

private:
    void load();
    void saveFriends();
    void savePending();
    void saveBlocked();
    void saveFormer();

    template<typename T>
    void tryLoad(const QString& path, QList<T>& list);
    template<typename T>
    void trySave(const QString& path, const QList<T>& list);

    QString dataDir() const;

    QList<FriendInfo>    m_friends;
    QList<GroupInfo>     m_groups;
    QList<PendingRequest> m_pending;
    QList<FriendInfo>    m_formerFriends;
    QSet<QString>        m_blocked;
};
