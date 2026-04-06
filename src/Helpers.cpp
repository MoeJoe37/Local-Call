#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "Helpers.h"
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <string>

namespace Helpers {

static const char* adjectives[] = {
    "Silly","Brave","Goofy","Turbo","Fancy","Sleepy","Hyper","Invisible",
    "Dapper","Fuzzy","Wobbly","Sneaky","Spooky","Clumsy","Radical","Chunky"
};
static const char* nouns[] = {
    "Hamster","Potato","Ninja","Wizard","Toaster","Unicorn","Panda","Cactus",
    "Penguin","Bagel","Waffle","Dolphin","Dragon","Muffin","Robot","Pickle"
};

std::string getFunnyName() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> adjDist(0, 15);
    std::uniform_int_distribution<int> nounDist(0, 15);
    return std::string(adjectives[adjDist(rng)]) + " " + nouns[nounDist(rng)];
}

std::string getLocalIp() {
    // Route-aware method: ask the OS which local address it would use to reach
    // a public IP (8.8.8.8). This correctly selects the real LAN adapter and
    // ignores VPN/VirtualBox/Docker/Hamachi virtual interfaces.
    // QUdpSocket::connectToHost in Unconnected→Bound state doesn't send any
    // packets — it just resolves the routing table.
    QUdpSocket sock;
    sock.connectToHost("8.8.8.8", 53);   // no actual data sent, just routes
    if (sock.waitForConnected(200)) {
        std::string ip = sock.localAddress().toString().toStdString();
        sock.close();
        if (!ip.empty() && ip != "0.0.0.0" && ip != "127.0.0.1")
            return ip;
    }
    sock.close();

    // Fallback: iterate interfaces, preferring private RFC-1918 ranges on a
    // running non-loopback adapter (still beats picking a VM adapter at random).
    const auto ifaces = QNetworkInterface::allInterfaces();
    auto isPrivate = [](const QHostAddress& a) {
        quint32 ip = a.toIPv4Address();
        return (ip >> 24) == 10 ||
               (ip >> 20) == (172u * 4096 + 16) ||
               (ip >> 16) == (192u * 256  + 168);
    };
    for (const auto& iface : ifaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp))       continue;
        if (iface.flags()   & QNetworkInterface::IsLoopBack)  continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (isPrivate(entry.ip()))
                return entry.ip().toString().toStdString();
        }
    }
    return "127.0.0.1";
}

std::string generateId() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    const char hex[] = "0123456789abcdef";
    std::string id(8, '0');
    for (auto& c : id) c = hex[dist(rng)];
    return id;
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ── Base64 ────────────────────────────────────────────────────────────────────
static const std::string B64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string ret;
    int i = 0;
    uint8_t buf3[3], buf4[4];
    size_t len = data.size();
    size_t idx = 0;
    while (len--) {
        buf3[i++] = data[idx++];
        if (i == 3) {
            buf4[0] = (buf3[0] & 0xfc) >> 2;
            buf4[1] = ((buf3[0] & 0x03) << 4) + ((buf3[1] & 0xf0) >> 4);
            buf4[2] = ((buf3[1] & 0x0f) << 2) + ((buf3[2] & 0xc0) >> 6);
            buf4[3] =  buf3[2] & 0x3f;
            for (int k=0; k<4; k++) ret += B64_CHARS[buf4[k]];
            i = 0;
        }
    }
    if (i) {
        for (int k=i; k<3; k++) buf3[k] = 0;
        buf4[0] = (buf3[0] & 0xfc) >> 2;
        buf4[1] = ((buf3[0] & 0x03) << 4) + ((buf3[1] & 0xf0) >> 4);
        buf4[2] = ((buf3[1] & 0x0f) << 2) + ((buf3[2] & 0xc0) >> 6);
        for (int k=0; k<i+1; k++) ret += B64_CHARS[buf4[k]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

std::vector<uint8_t> base64Decode(const std::string& encoded) {
    std::vector<uint8_t> ret;
    int i = 0;
    uint8_t buf4[4], buf3[3];
    for (char c : encoded) {
        if (c == '=') break;
        size_t pos = B64_CHARS.find(c);
        if (pos == std::string::npos) continue;
        buf4[i++] = static_cast<uint8_t>(pos);
        if (i == 4) {
            buf3[0] = ( buf4[0]        << 2) + ((buf4[1] & 0x30) >> 4);
            buf3[1] = ((buf4[1] & 0xf) << 4) + ((buf4[2] & 0x3c) >> 2);
            buf3[2] = ((buf4[2] & 0x3) << 6) +   buf4[3];
            for (int k=0; k<3; k++) ret.push_back(buf3[k]);
            i = 0;
        }
    }
    if (i) {
        for (int k=i; k<4; k++) buf4[k] = 0;
        buf3[0] = ( buf4[0]        << 2) + ((buf4[1] & 0x30) >> 4);
        buf3[1] = ((buf4[1] & 0xf) << 4) + ((buf4[2] & 0x3c) >> 2);
        for (int k=0; k<i-1; k++) ret.push_back(buf3[k]);
    }
    return ret;
}

// ── MIME ──────────────────────────────────────────────────────────────────────
std::string guessMime(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    if (e == ".png")  return "image/png";
    if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
    if (e == ".gif")  return "image/gif";
    if (e == ".bmp")  return "image/bmp";
    if (e == ".mp4")  return "video/mp4";
    if (e == ".mkv")  return "video/x-matroska";
    if (e == ".mov")  return "video/quicktime";
    if (e == ".avi")  return "video/x-msvideo";
    if (e == ".pdf")  return "application/pdf";
    if (e == ".wav")  return "audio/wav";
    if (e == ".mp3")  return "audio/mpeg";
    return "application/octet-stream";
}

std::string mimeToExt(const std::string& mime) {
    if (mime == "image/png")  return ".png";
    if (mime == "image/jpeg") return ".jpg";
    if (mime == "image/gif")  return ".gif";
    if (mime == "video/mp4")  return ".mp4";
    if (mime == "audio/wav")  return ".wav";
    return ".bin";
}

bool isImage(const std::string& mime) {
    return mime.rfind("image/", 0) == 0;
}

} // namespace Helpers
