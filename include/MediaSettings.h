#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>   // std::pair
#include <vector>

namespace MediaSettings {
    // Network ports
    constexpr int BroadcastPort      = 50005;
    constexpr int SignalingPort      = 50010;
    constexpr int MediaUdpPort       = 50100;   // default media path: audio+video+control
    constexpr int MediaTcpPort       = 50120;   // fallback when UDP is blocked
    constexpr int GroupCallPort      = 50200;

    // Timing
    constexpr int BroadcastIntervalMs = 1000;
    constexpr int PeerTimeoutSeconds  = 8;

    // Media tuning
    constexpr int  UdpProbeTimeoutMs  = 1500;   // no Hello back within this -> fall back to TCP
    constexpr int  HelloIntervalMs    = 250;    // Hello repeat rate while probing
    constexpr int  JitterTargetMs     = 60;     // nominal playout delay
    constexpr int  JitterMaxMs        = 240;    // hard cap before frames are discarded
    constexpr int  MaxTcpQueueBytes   = 256 * 1024;  // video backpressure threshold
    constexpr int  OpusFrameMs        = 20;
    constexpr int  OpusSampleRate     = 48000;
    constexpr int  OpusBitrate        = 32000;
    constexpr int  KeyframeRequestCooldownMs = 1000;

    // Limits
    constexpr int  BufferSize      = 65536;
    constexpr int  FrameHeaderSize = 8;
    constexpr int  ChunkSize       = 256 * 1024;              // 256 KB per TCP chunk
    constexpr long FileMaxBytes    = 2000L * 1024 * 1024;     // 2 GB practical cap

    // Video presets
    struct Resolution { int w, h; };
    inline const std::map<std::string, std::optional<Resolution>> Resolutions = {
        {"144p",  Resolution{256,  144}},
        {"240p",  Resolution{426,  240}},
        {"360p",  Resolution{640,  360}},
        {"480p",  Resolution{854,  480}},
        {"720p",  Resolution{1280, 720}},
        {"1080p", Resolution{1920, 1080}},
        {"Source", std::nullopt}
    };
    inline const std::vector<std::string> FpsOptions = {"30","60","90","120","Source"};
}
