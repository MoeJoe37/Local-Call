#pragma once
#include <string>
#include <chrono>

struct PeerInfo {
    std::string id;
    std::string name;
    std::string ip;
    std::chrono::steady_clock::time_point lastSeen;
};
