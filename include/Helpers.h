#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <QString>
#include <QByteArray>

namespace Helpers {
    std::string getFunnyName();
    std::string getLocalIp();
    std::vector<std::string> localIPv4Addresses(bool includeLoopback = false);
    std::string generateId();
    int64_t nowMs();

    // Cross-platform storage and safe writes.
    QString appDataRoot();
    QString legacyAppDataRoot();
    bool writeTextFileAtomically(const QString& path, const QByteArray& data, QString* error = nullptr);

    // Base64
    std::string base64Encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> base64Decode(const std::string& encoded);

    // MIME
    std::string guessMime(const std::string& ext);
    std::string mimeToExt(const std::string& mime);
    bool isImage(const std::string& mime);
}
