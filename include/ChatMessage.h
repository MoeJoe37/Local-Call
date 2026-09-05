#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include "nlohmann/json.hpp"

enum class MessageKind { Text, Image, File, VoiceNote, System, CallEvent };

struct ChatMessage {
    MessageKind          kind      = MessageKind::Text;
    std::string          fromId;
    std::string          fromName;
    std::string          text;
    std::string          fileName;
    std::string          mime;
    std::vector<uint8_t> data;
    bool                 isMine    = false;
    int64_t              timestamp = 0;  // unix ms

    // Reply metadata. replyToTs == 0 means "not a reply". The name and snippet
    // are carried alongside the timestamp so a quoted message still renders
    // after the original has been deleted, and without a second store lookup.
    int64_t     replyToTs = 0;
    std::string replyName;
    std::string replySnippet;

    std::string timeStr() const {
        time_t t = static_cast<time_t>(timestamp / 1000);
        struct tm* tm_info = localtime(&t);
        char buf[6];
        if (tm_info) snprintf(buf, sizeof(buf), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
        else         snprintf(buf, sizeof(buf), "--:--");
        return buf;
    }
    std::string nameDisplay() const { return isMine ? "" : fromName; }
    bool isReply() const { return replyToTs != 0; }
};

// Disk-serialised form (binary data stored as base64 string)
struct StoredMessage {
    std::string kind;
    std::string fromId;
    std::string fromName;
    std::string text;
    std::string fileName;
    std::string mime;
    std::string data;    // base64
    bool        isMine = false;
    int64_t     ts     = 0;
    int64_t     replyToTs = 0;
    std::string replyName;
    std::string replySnippet;
};

inline void to_json(nlohmann::json& j, const StoredMessage& s) {
    j = nlohmann::json{
        {"kind",     s.kind},    {"fromId",   s.fromId},
        {"fromName", s.fromName},{"text",     s.text},
        {"fileName", s.fileName},{"mime",     s.mime},
        {"data",     s.data},    {"isMine",   s.isMine},
        {"ts",       s.ts}
    };
    // Only written when present, so untouched conversations keep their old shape.
    if (s.replyToTs != 0) {
        j["replyToTs"]    = s.replyToTs;
        j["replyName"]    = s.replyName;
        j["replySnippet"] = s.replySnippet;
    }
}

inline void from_json(const nlohmann::json& j, StoredMessage& s) {
    auto g = [&](const char* k, auto& v) { if (j.contains(k)) j.at(k).get_to(v); };
    g("kind",     s.kind);    g("fromId",   s.fromId);
    g("fromName", s.fromName);g("text",     s.text);
    g("fileName", s.fileName);g("mime",     s.mime);
    g("data",     s.data);    g("isMine",   s.isMine);
    g("ts",       s.ts);
    g("replyToTs",    s.replyToTs);
    g("replyName",    s.replyName);
    g("replySnippet", s.replySnippet);
}
