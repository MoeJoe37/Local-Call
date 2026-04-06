#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Helpers {
    std::string getFunnyName();
    std::string getLocalIp();
    std::string generateId();
    int64_t nowMs();

    // Base64
    std::string base64Encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> base64Decode(const std::string& encoded);

    // MIME
    std::string guessMime(const std::string& ext);
    std::string mimeToExt(const std::string& mime);
    bool isImage(const std::string& mime);
}
