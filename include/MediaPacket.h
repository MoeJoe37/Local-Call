#pragma once

#include <QByteArray>
#include <QtEndian>
#include <cstdint>
#include <cstring>

// ── Local Call Media v3 (LCM3) ───────────────────────────────────────────────
//
// One framing format shared by every media transport.  Earlier releases used
// three incompatible headers — LCJ1/LCA1 inside the payload, LCM2 for TCP and
// LCU1 for UDP — which meant the payload could not carry a sequence number, a
// capture timestamp, or a keyframe flag.  Without those a jitter buffer and
// keyframe recovery are impossible, so audio was played straight out and video
// stayed black until the next periodic intra frame.
//
//   offset  size  field
//   0       4     magic 'L','C','M','3'
//   4       1     tag        (MediaTag)
//   5       1     flags      (MediaFlag bitmask)
//   6       4     seq        frame sequence, per tag
//   10      4     timestampMs capture clock
//   14      2     chunkIndex
//   16      2     chunkCount
//   18      2     payloadLen  bytes of this chunk
//   20      4     senderToken loopback rejection
//   ------------------------------------------------
//   24            header, then payloadLen bytes
namespace MediaPacket {

inline constexpr char   Magic[4]    = {'L', 'C', 'M', '3'};
inline constexpr int    HeaderSize  = 24;
inline constexpr int    MaxPayload  = 1180;             // stays under a 1500 byte MTU
inline constexpr int    MaxFrameBytes = 4 * 1024 * 1024;
inline constexpr quint16 MaxChunks   = 0xFFFF;

enum class Tag : char {
    Audio          = 'A',
    Video          = 'V',
    Hello          = 'H',   // codec/format announcement, also used as a reachability probe
    KeyframeRequest= 'K',
    Ping           = 'P',
    Pong           = 'O',
};

enum Flag : quint8 {
    FlagNone     = 0,
    FlagKeyframe = 1 << 0,  // video: this frame is an IDR
    FlagSilence  = 1 << 1,  // audio: encoder emitted DTX / the mic is muted
};

struct Header {
    Tag      tag{Tag::Audio};
    quint8   flags{FlagNone};
    quint32  seq{0};
    quint32  timestampMs{0};
    quint16  chunkIndex{0};
    quint16  chunkCount{1};
    quint16  payloadLen{0};
    quint32  senderToken{0};
};

inline bool isMediaTag(Tag t) noexcept
{
    return t == Tag::Audio || t == Tag::Video;
}

inline void write(char* out, const Header& h)
{
    std::memcpy(out, Magic, 4);
    out[4] = static_cast<char>(h.tag);
    out[5] = static_cast<char>(h.flags);
    qToBigEndian<quint32>(h.seq,         out + 6);
    qToBigEndian<quint32>(h.timestampMs, out + 10);
    qToBigEndian<quint16>(h.chunkIndex,  out + 14);
    qToBigEndian<quint16>(h.chunkCount,  out + 16);
    qToBigEndian<quint16>(h.payloadLen,  out + 18);
    qToBigEndian<quint32>(h.senderToken, out + 20);
}

/// Returns false when the buffer is too short or the magic does not match.
/// Does not validate payloadLen against the buffer — callers differ (a UDP
/// datagram must contain the whole chunk, a TCP stream may still be filling).
inline bool read(const char* in, int size, Header& out)
{
    if (size < HeaderSize) return false;
    if (std::memcmp(in, Magic, 4) != 0) return false;
    out.tag         = static_cast<Tag>(in[4]);
    out.flags       = static_cast<quint8>(in[5]);
    out.seq         = qFromBigEndian<quint32>(in + 6);
    out.timestampMs = qFromBigEndian<quint32>(in + 10);
    out.chunkIndex  = qFromBigEndian<quint16>(in + 14);
    out.chunkCount  = qFromBigEndian<quint16>(in + 16);
    out.payloadLen  = qFromBigEndian<quint16>(in + 18);
    out.senderToken = qFromBigEndian<quint32>(in + 20);
    return true;
}

inline QByteArray build(const Header& h, const char* payload, int payloadLen)
{
    QByteArray packet(HeaderSize + payloadLen, Qt::Uninitialized);
    Header copy = h;
    copy.payloadLen = static_cast<quint16>(payloadLen);
    write(packet.data(), copy);
    if (payloadLen > 0) std::memcpy(packet.data() + HeaderSize, payload, static_cast<size_t>(payloadLen));
    return packet;
}

// ── Hello payload ────────────────────────────────────────────────────────────
// Sent by both sides before media flows so a voice-only peer and a
// voice+video peer agree on what they can exchange, and so each side learns
// the other's audio format instead of guessing it from the first packet.
enum class AudioCodec : quint8 { None = 0, Opus = 1 };
enum class VideoCodec : quint8 { None = 0, H264 = 1 };

struct Hello {
    AudioCodec audioCodec{AudioCodec::Opus};
    VideoCodec videoCodec{VideoCodec::None};
    quint32    sampleRate{48000};
    quint8     channels{1};
    quint8     canReceiveVideo{0};
};

inline QByteArray encodeHello(const Hello& h)
{
    QByteArray body(8, Qt::Uninitialized);
    char* p = body.data();
    p[0] = static_cast<char>(h.audioCodec);
    p[1] = static_cast<char>(h.videoCodec);
    qToBigEndian<quint32>(h.sampleRate, p + 2);
    p[6] = static_cast<char>(h.channels);
    p[7] = static_cast<char>(h.canReceiveVideo);
    return body;
}

inline bool decodeHello(const QByteArray& body, Hello& out)
{
    if (body.size() < 8) return false;
    const char* p = body.constData();
    out.audioCodec      = static_cast<AudioCodec>(static_cast<quint8>(p[0]));
    out.videoCodec      = static_cast<VideoCodec>(static_cast<quint8>(p[1]));
    out.sampleRate      = qFromBigEndian<quint32>(p + 2);
    out.channels        = static_cast<quint8>(p[6]);
    out.canReceiveVideo = static_cast<quint8>(p[7]);
    return out.sampleRate > 0 && out.channels > 0;
}

}  // namespace MediaPacket
