#pragma once
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include "FriendInfo.h"
#include "nlohmann/json.hpp"

struct GroupPermissions {
    bool canSendMessages = true;
    bool canSendFiles    = true;
    bool canStartCalls   = true;
};

// Use nlohmann::json explicitly (not the 'json' alias from SigMsg.h)
inline void to_json(nlohmann::json& j, const GroupPermissions& p) {
    j = nlohmann::json{
        {"canSendMessages", p.canSendMessages},
        {"canSendFiles",    p.canSendFiles},
        {"canStartCalls",   p.canStartCalls}
    };
}

inline void from_json(const nlohmann::json& j, GroupPermissions& p) {
    if (j.contains("canSendMessages")) j.at("canSendMessages").get_to(p.canSendMessages);
    if (j.contains("canSendFiles"))    j.at("canSendFiles").get_to(p.canSendFiles);
    if (j.contains("canStartCalls"))   j.at("canStartCalls").get_to(p.canStartCalls);
}

struct GroupInfo {
    std::string groupId;
    std::string name;
    std::string ownerId;
    std::vector<std::string> memberIds;
    std::vector<std::string> helperIds;
    std::map<std::string, GroupPermissions> permissions;

    // Runtime-only — not serialized
    std::vector<FriendInfo*> members;

    GroupInfo() {
        const char hex[] = "0123456789abcdef";
        groupId.resize(8);
        for (auto& c : groupId) c = hex[std::rand() % 16];
    }

    bool isOwner(const std::string& id) const { return id == ownerId; }
    bool isHelper(const std::string& id) const {
        return std::find(helperIds.begin(), helperIds.end(), id) != helperIds.end();
    }
    bool canManage(const std::string& actor, const std::string& target) const {
        return isOwner(actor) || (isHelper(actor) && !isOwner(target) && !isHelper(target));
    }
    GroupPermissions getPermissions(const std::string& memberId) const {
        auto it = permissions.find(memberId);
        return it != permissions.end() ? it->second : GroupPermissions{};
    }
    std::string toString() const { return name; }
};

inline void to_json(nlohmann::json& j, const GroupInfo& g) {
    j = nlohmann::json{
        {"groupId",     g.groupId},
        {"name",        g.name},
        {"ownerId",     g.ownerId},
        {"memberIds",   g.memberIds},
        {"helperIds",   g.helperIds},
        {"permissions", g.permissions}
    };
}

inline void from_json(const nlohmann::json& j, GroupInfo& g) {
    if (j.contains("groupId"))   j.at("groupId").get_to(g.groupId);
    if (j.contains("name"))      j.at("name").get_to(g.name);
    if (j.contains("ownerId"))   j.at("ownerId").get_to(g.ownerId);
    if (j.contains("memberIds")) j.at("memberIds").get_to(g.memberIds);
    if (j.contains("helperIds")) j.at("helperIds").get_to(g.helperIds);
    if (j.contains("permissions")) {
        for (auto& [k, v] : j.at("permissions").items())
            g.permissions[k] = v.get<GroupPermissions>();
    }
}
