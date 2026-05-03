#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "nlohmann/json.hpp"

// Convenience alias — only for use in .cpp files that include this header directly
using json = nlohmann::json;

namespace LocalCallProtocol {
    inline const std::string Name = "localcall.v1";
    inline constexpr int Schema = 1;
}

// ── Signal type constants ─────────────────────────────────────────────────────
namespace SigType {
    inline const std::string FriendReq   = "friend_req";
    inline const std::string FriendAcc   = "friend_acc";
    inline const std::string FriendRej   = "friend_rej";
    inline const std::string FriendDel   = "friend_del";
    inline const std::string ChatText    = "chat_text";
    inline const std::string ChatFile    = "chat_file";
    inline const std::string ChatVoice   = "chat_voice";
    inline const std::string CallInv     = "call_inv";
    inline const std::string CallAcc     = "call_acc";
    inline const std::string CallRej     = "call_rej";
    inline const std::string CallEnd     = "call_end";
    inline const std::string GrpInv      = "grp_inv";
    inline const std::string GrpAcc      = "grp_acc";
    inline const std::string GrpRej      = "grp_rej";
    inline const std::string GrpLeave    = "grp_leave";
    inline const std::string GrpText     = "grp_text";
    inline const std::string GrpFile     = "grp_file";
    inline const std::string GrpVoice    = "grp_voice";
    inline const std::string GrpKick     = "grp_kick";
    inline const std::string GrpAddMember= "grp_add";
    inline const std::string GrpDelete   = "grp_delete";
    inline const std::string GrpPromote  = "grp_promote";
    inline const std::string GrpDemote   = "grp_demote";
    inline const std::string GrpPerm     = "grp_perm";
    inline const std::string GrpCallInv  = "grp_call_inv";
    inline const std::string GrpCallAcc  = "grp_call_acc";
    inline const std::string GrpCallRej  = "grp_call_rej";
    inline const std::string GrpCallEnd  = "grp_call_end";
    inline const std::string ScreenInv   = "screen_inv";
    inline const std::string ScreenEnd   = "screen_end";
    inline const std::string DiscProbe   = "disc_probe";
    inline const std::string DiscResp    = "disc_resp";
    inline const std::string Typing      = "typing";        // user is typing
    inline const std::string UploadStart = "upload_start";  // user started uploading a file
    inline const std::string UploadEnd   = "upload_end";    // user finished uploading
}

// ── Member DTO ────────────────────────────────────────────────────────────────
struct MemberDto {
    std::string id;
    std::string name;
    std::string ip;
};

inline void to_json(nlohmann::json& j, const MemberDto& m) {
    j = nlohmann::json{{"id", m.id}, {"name", m.name}, {"ip", m.ip}};
}
inline void from_json(const nlohmann::json& j, MemberDto& m) {
    if (j.contains("id"))   j.at("id").get_to(m.id);
    if (j.contains("name")) j.at("name").get_to(m.name);
    if (j.contains("ip"))   j.at("ip").get_to(m.ip);
}

// ── Main signaling message ────────────────────────────────────────────────────
struct SigMsg {
    std::optional<std::string> protocol;
    std::optional<int> schema;
    std::optional<std::string> app_version;
    std::optional<std::string> platform;
    std::string type;
    std::string from_id;
    std::string from_name;
    std::optional<std::string> text;
    std::optional<std::string> file_name;
    std::optional<std::string> mime;
    std::optional<std::string> data;
    std::optional<std::string> group_id;
    std::optional<std::string> group_name;
    std::optional<std::vector<MemberDto>> members;
    std::optional<std::string> mode;
    std::optional<std::string> target_id;
    std::optional<std::string> owner_id;
    std::optional<bool> perm_msg;
    std::optional<bool> perm_file;
    std::optional<bool> perm_call;
    // Chunked file transfer
    std::optional<std::string> transfer_id;
    std::optional<int>         chunk_index;
    std::optional<int>         total_chunks;
    std::optional<int64_t>     file_size;
    int64_t ts = 0;
};

inline void to_json(nlohmann::json& j, const SigMsg& m) {
    j = nlohmann::json{{"type", m.type}, {"from_id", m.from_id},
                       {"from_name", m.from_name}, {"ts", m.ts}};
    if (m.protocol)    j["protocol"]    = *m.protocol;
    if (m.schema)      j["schema"]      = *m.schema;
    if (m.app_version) j["app_version"] = *m.app_version;
    if (m.platform)    j["platform"]    = *m.platform;
    if (m.text)       j["text"]       = *m.text;
    if (m.file_name)  j["file_name"]  = *m.file_name;
    if (m.mime)       j["mime"]       = *m.mime;
    if (m.data)       j["data"]       = *m.data;
    if (m.group_id)   j["group_id"]   = *m.group_id;
    if (m.group_name) j["group_name"] = *m.group_name;
    if (m.members)    j["members"]    = *m.members;
    if (m.mode)       j["mode"]       = *m.mode;
    if (m.target_id)  j["target_id"]  = *m.target_id;
    if (m.owner_id)   j["owner_id"]   = *m.owner_id;
    if (m.perm_msg)      j["perm_msg"]      = *m.perm_msg;
    if (m.perm_file)     j["perm_file"]     = *m.perm_file;
    if (m.perm_call)     j["perm_call"]     = *m.perm_call;
    if (m.transfer_id)   j["transfer_id"]   = *m.transfer_id;
    if (m.chunk_index)   j["chunk_index"]   = *m.chunk_index;
    if (m.total_chunks)  j["total_chunks"]  = *m.total_chunks;
    if (m.file_size)     j["file_size"]     = *m.file_size;
}

inline void from_json(const nlohmann::json& j, SigMsg& m) {
    if (j.contains("type"))      j.at("type").get_to(m.type);
    if (j.contains("from_id"))   j.at("from_id").get_to(m.from_id);
    if (j.contains("from_name")) j.at("from_name").get_to(m.from_name);
    if (j.contains("ts"))        j.at("ts").get_to(m.ts);

    auto os = [&](const char* k, std::optional<std::string>& v) {
        if (j.contains(k) && !j[k].is_null()) v = j[k].get<std::string>();
    };
    os("protocol",    m.protocol);
    os("app_version", m.app_version);
    os("platform",    m.platform);
    if (j.contains("schema") && j["schema"].is_number_integer()) m.schema = j["schema"].get<int>();

    os("text",       m.text);      os("file_name",  m.file_name);
    os("mime",       m.mime);      os("data",       m.data);
    os("group_id",   m.group_id);  os("group_name", m.group_name);
    os("mode",       m.mode);      os("target_id",  m.target_id);
    os("owner_id",   m.owner_id);

    if (j.contains("members") && !j["members"].is_null())
        m.members = j["members"].get<std::vector<MemberDto>>();

    auto ob = [&](const char* k, std::optional<bool>& v) {
        if (j.contains(k) && !j[k].is_null()) v = j[k].get<bool>();
    };
    ob("perm_msg", m.perm_msg); ob("perm_file", m.perm_file); ob("perm_call", m.perm_call);
    os("transfer_id", m.transfer_id);
    if (j.contains("chunk_index")  && j["chunk_index"].is_number())  m.chunk_index  = j["chunk_index"].get<int>();
    if (j.contains("total_chunks") && j["total_chunks"].is_number()) m.total_chunks = j["total_chunks"].get<int>();
    if (j.contains("file_size")    && j["file_size"].is_number())    m.file_size    = j["file_size"].get<int64_t>();
}

// Wire encoding
#include <vector>
inline std::vector<uint8_t> SigMsgEncode(const SigMsg& msg) {
    std::string body = nlohmann::json(msg).dump();
    uint32_t len = static_cast<uint32_t>(body.size());
    std::vector<uint8_t> out(4 + body.size());
    out[0] = (len >> 24) & 0xFF;
    out[1] = (len >> 16) & 0xFF;
    out[2] = (len >>  8) & 0xFF;
    out[3] = (len      ) & 0xFF;
    std::copy(body.begin(), body.end(), out.begin() + 4);
    return out;
}

#include <QMetaType>
Q_DECLARE_METATYPE(SigMsg)

