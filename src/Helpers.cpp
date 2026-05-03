#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "Helpers.h"
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
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

static bool isProbablyVirtualInterface(const QNetworkInterface& iface) {
    const QString name = (iface.name() + " " + iface.humanReadableName()).toLower();
    const QStringList deny = {
        "docker", "veth", "br-", "virbr", "vmnet", "vbox", "virtualbox",
        "hyper-v", "zerotier", "tailscale", "tun", "tap", "wg", "hamachi"
    };
    for (const auto& token : deny) {
        if (name.contains(token)) return true;
    }
    return false;
}

static bool isPrivateIPv4(const QHostAddress& a) {
    quint32 ip = a.toIPv4Address();
    return (ip >> 24) == 10 ||
           (ip >> 20) == (172u * 4096 + 16) ||
           (ip >> 16) == (192u * 256 + 168) ||
           (ip >> 16) == (169u * 256 + 254); // link-local fallback
}

std::vector<std::string> localIPv4Addresses(bool includeLoopback) {
    struct Candidate { QHostAddress ip; int score; };
    std::vector<Candidate> candidates;

    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)) continue;
        if ((flags & QNetworkInterface::IsLoopBack) && !includeLoopback) continue;

        int ifaceScore = 0;
        if (flags & QNetworkInterface::CanBroadcast) ifaceScore += 20;
        if (flags & QNetworkInterface::CanMulticast) ifaceScore += 10;
        if (isProbablyVirtualInterface(iface)) ifaceScore -= 50;

        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (entry.ip().isLoopback() && !includeLoopback) continue;
            int score = ifaceScore;
            if (isPrivateIPv4(entry.ip())) score += 40;
            if (!entry.broadcast().isNull()) score += 10;
            candidates.push_back({entry.ip(), score});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    std::vector<std::string> out;
    for (const auto& c : candidates) {
        const auto ip = c.ip.toString().toStdString();
        if (std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
    }
    return out;
}

std::string getLocalIp() {
    // Route-aware first choice. This does not send packets; it asks the OS which
    // local address would be used for a normal outbound route.
    QUdpSocket sock;
    sock.connectToHost("8.8.8.8", 53);
    if (sock.waitForConnected(200)) {
        const std::string ip = sock.localAddress().toString().toStdString();
        sock.close();
        if (!ip.empty() && ip != "0.0.0.0" && ip != "127.0.0.1") return ip;
    }
    sock.close();

    const auto ips = localIPv4Addresses(false);
    return ips.empty() ? std::string("127.0.0.1") : ips.front();
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


QString appDataRoot() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::home().filePath(".local/share/LocalCall");
    }
    QDir().mkpath(base);
    return base;
}

QString legacyAppDataRoot() {
    // Older builds wrote under AppDataLocation/"Local Call". Keep read fallback
    // so existing Windows/Linux profiles migrate without losing history.
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::home().filePath(".local/share/LocalCall");
    }
    return QDir(base).filePath("Local Call");
}

bool writeTextFileAtomically(const QString& path, const QByteArray& data, QString* error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = out.errorString();
        return false;
    }
    if (out.write(data) != data.size()) {
        if (error) *error = out.errorString();
        return false;
    }
    if (!out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
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
