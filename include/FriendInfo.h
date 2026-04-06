#pragma once
#include <string>
#include "nlohmann/json.hpp"

struct FriendInfo {
    std::string id;
    std::string name;
    std::string ip;

    // Runtime-only (not persisted)
    bool isOnline    = false;
    int  unreadCount = 0;

    std::string statusColor() const { return isOnline ? "#03DAC6" : "#555555"; }
};

// Serialization — use nlohmann::json explicitly, NOT the 'json' alias
inline void to_json(nlohmann::json& j, const FriendInfo& f) {
    j = nlohmann::json{{"id", f.id}, {"name", f.name}, {"ip", f.ip}};
}

inline void from_json(const nlohmann::json& j, FriendInfo& f) {
    if (j.contains("id"))   j.at("id").get_to(f.id);
    if (j.contains("name")) j.at("name").get_to(f.name);
    if (j.contains("ip"))   j.at("ip").get_to(f.ip);
}

struct PendingRequest {
    std::string fromId;
    std::string fromName;
    std::string fromIp;
};

inline void to_json(nlohmann::json& j, const PendingRequest& r) {
    j = nlohmann::json{{"fromId", r.fromId}, {"fromName", r.fromName}, {"fromIp", r.fromIp}};
}

inline void from_json(const nlohmann::json& j, PendingRequest& r) {
    if (j.contains("fromId"))   j.at("fromId").get_to(r.fromId);
    if (j.contains("fromName")) j.at("fromName").get_to(r.fromName);
    if (j.contains("fromIp"))   j.at("fromIp").get_to(r.fromIp);
}
