#include "FriendManager.h"
#include "Helpers.h"
#include "nlohmann/json.hpp"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

// Convenience alias for this file
using json = nlohmann::json;

FriendManager::FriendManager(QObject* parent) : QObject(parent)
{
    QDir().mkpath(dataDir());
    // Defer disk I/O until after the event loop starts so the window can
    // render its first frame before we block on file reads.
    QTimer::singleShot(0, this, [this]{ load(); });
}

QString FriendManager::dataDir() const
{
    return Helpers::appDataRoot();
}

// ── Friends ───────────────────────────────────────────────────────────────────

bool FriendManager::hasFriend(const QString& id) const
{
    return std::any_of(m_friends.begin(), m_friends.end(),
        [&](const FriendInfo& f){ return f.id == id.toStdString(); });
}

FriendInfo* FriendManager::getFriend(const QString& id)
{
    for (auto& f : m_friends)
        if (f.id == id.toStdString()) return &f;
    return nullptr;
}

void FriendManager::addFriend(const FriendInfo& f)
{
    if (hasFriend(QString::fromStdString(f.id))) return;
    removePending(QString::fromStdString(f.id));
    m_formerFriends.removeIf([&](const FriendInfo& x){ return x.id == f.id; });
    saveFormer();
    m_blocked.remove(QString::fromStdString(f.id));
    saveBlocked();
    m_friends.append(f);
    saveFriends();
    emit friendsChanged();
}

void FriendManager::removeFormerFriend(const QString& id)
{
    int before = m_formerFriends.size();
    m_formerFriends.removeIf([&](const FriendInfo& f){ return f.id == id.toStdString(); });
    m_blocked.remove(id);
    if (m_formerFriends.size() != before) { saveFormer(); saveBlocked(); emit friendsChanged(); }
}

void FriendManager::removeFriend(const QString& id)
{
    FriendInfo* fp = getFriend(id);
    if (!fp) return;
    FriendInfo copy = *fp;
    m_friends.removeIf([&](const FriendInfo& f){ return f.id == id.toStdString(); });
    saveFriends();

    bool alreadyFormer = std::any_of(m_formerFriends.begin(), m_formerFriends.end(),
        [&](const FriendInfo& f){ return f.id == id.toStdString(); });
    if (!alreadyFormer) {
        FriendInfo former; former.id = copy.id; former.name = copy.name; former.ip = copy.ip; former.authPublicKey = copy.authPublicKey; former.authFingerprint = copy.authFingerprint;
        m_formerFriends.append(former);
        saveFormer();
    }
    m_blocked.insert(id);
    saveBlocked();

    for (auto& g : m_groups) {
        g.memberIds.erase(std::remove(g.memberIds.begin(), g.memberIds.end(), id.toStdString()),
                          g.memberIds.end());
        g.helperIds.erase(std::remove(g.helperIds.begin(), g.helperIds.end(), id.toStdString()),
                          g.helperIds.end());
    }
    saveGroups();
    emit friendsChanged();
}

void FriendManager::updateFriendIp(const QString& id, const QString& ip)
{
    if (auto* f = getFriend(id)) { f->ip = ip.toStdString(); saveFriends(); }
}

// ── Pending ───────────────────────────────────────────────────────────────────

bool FriendManager::hasPending(const QString& fromId) const
{
    return std::any_of(m_pending.begin(), m_pending.end(),
        [&](const PendingRequest& r){ return r.fromId == fromId.toStdString(); });
}

void FriendManager::addPending(const PendingRequest& req)
{
    if (hasFriend(QString::fromStdString(req.fromId))) return;
    if (hasPending(QString::fromStdString(req.fromId))) return;
    m_pending.append(req);
    savePending();
    emit pendingChanged();
}

void FriendManager::removePending(const QString& fromId)
{
    int before = m_pending.size();
    m_pending.removeIf([&](const PendingRequest& r){ return r.fromId == fromId.toStdString(); });
    if (m_pending.size() != before) { savePending(); emit pendingChanged(); }
}

// ── Blocked ───────────────────────────────────────────────────────────────────

bool FriendManager::isBlocked(const QString& id) const { return m_blocked.contains(id); }
void FriendManager::block(const QString& id)   { m_blocked.insert(id);  saveBlocked(); }
void FriendManager::unblock(const QString& id) { m_blocked.remove(id);  saveBlocked(); }

// ── Groups ────────────────────────────────────────────────────────────────────

void FriendManager::addGroup(const GroupInfo& g)
{
    m_groups.append(g);
    saveGroups();
    emit groupsChanged();
}

void FriendManager::removeGroup(const QString& groupId)
{
    int before = m_groups.size();
    m_groups.removeIf([&](const GroupInfo& g){ return g.groupId == groupId.toStdString(); });
    if (m_groups.size() != before) { saveGroups(); emit groupsChanged(); }
}

GroupInfo* FriendManager::getGroup(const QString& groupId)
{
    for (auto& g : m_groups)
        if (g.groupId == groupId.toStdString()) return &g;
    return nullptr;
}

void FriendManager::saveGroups() { trySave(dataDir() + "/groups.json", m_groups); }

// ── Persistence ───────────────────────────────────────────────────────────────

void FriendManager::load()
{
    auto loadFile = [this](const QString& leaf) -> json {
        const QString primary = QDir(dataDir()).filePath(leaf);
        const QString legacy  = QDir(Helpers::legacyAppDataRoot()).filePath(leaf);
        for (const auto& path : {primary, legacy}) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            try { return json::parse(f.readAll().toStdString()); }
            catch (...) { return json::array(); }
        }
        return json::array();
    };

    m_friends.clear();
    m_groups.clear();
    m_pending.clear();
    m_blocked.clear();
    m_formerFriends.clear();

    try { for (const auto& v : loadFile("friends.json"))
              m_friends.append(v.get<FriendInfo>()); } catch (...) {}

    try { for (const auto& v : loadFile("groups.json"))
              m_groups.append(v.get<GroupInfo>()); } catch (...) {}

    try { for (const auto& v : loadFile("pending.json"))
              m_pending.append(v.get<PendingRequest>()); } catch (...) {}

    try { for (const auto& v : loadFile("blocked.json"))
              m_blocked.insert(QString::fromStdString(v.get<std::string>())); } catch (...) {}

    try { for (const auto& v : loadFile("former_friends.json"))
              m_formerFriends.append(v.get<FriendInfo>()); } catch (...) {}

    emit friendsChanged();
    emit groupsChanged();
    emit pendingChanged();
}

void FriendManager::saveFriends()
{
    try {
        json arr = json::array();
        for (const auto& f : m_friends) arr.push_back(f);
        Helpers::writeTextFileAtomically(dataDir()+"/friends.json", QByteArray::fromStdString(arr.dump(2)));
    } catch (...) {}
}

void FriendManager::savePending()
{
    try {
        json arr = json::array();
        for (const auto& p : m_pending) arr.push_back(p);
        Helpers::writeTextFileAtomically(dataDir()+"/pending.json", QByteArray::fromStdString(arr.dump(2)));
    } catch (...) {}
}

void FriendManager::saveBlocked()
{
    try {
        json arr = json::array();
        for (const auto& id : m_blocked) arr.push_back(id.toStdString());
        Helpers::writeTextFileAtomically(dataDir()+"/blocked.json", QByteArray::fromStdString(arr.dump(2)));
    } catch (...) {}
}

void FriendManager::saveFormer()
{
    try {
        json arr = json::array();
        for (const auto& f : m_formerFriends) arr.push_back(f);
        Helpers::writeTextFileAtomically(dataDir()+"/former_friends.json", QByteArray::fromStdString(arr.dump(2)));
    } catch (...) {}
}

template<typename T>
void FriendManager::trySave(const QString& path, const QList<T>& list)
{
    try {
        json arr = json::array();
        for (const auto& v : list) arr.push_back(v);
        Helpers::writeTextFileAtomically(path, QByteArray::fromStdString(arr.dump(2)));
    } catch (...) {}
}

// Explicit instantiations to avoid linker errors
template void FriendManager::trySave(const QString&, const QList<GroupInfo>&);
template void FriendManager::trySave(const QString&, const QList<FriendInfo>&);
