using System.Collections.Generic;

namespace LocalCallPro;

public static class MediaSettings
{
    // ── Network ports ─────────────────────────────────────────────────────────
    public const int  BroadcastPort      = 50005;   // UDP: peer discovery
    public const int  SignalingPort       = 50010;   // TCP: friend requests, chat, calls
    public const int  MediaAudioPort      = 50100;   // UDP: call audio
    public const int  MediaVideoPort      = 50105;   // UDP: call video / screen
    public const int  GroupCallPortBase   = 50200;   // UDP: group voice call

    // ── Timing ────────────────────────────────────────────────────────────────
    public const int  BroadcastIntervalMs = 1_000;
    public const int  PeerTimeoutSeconds  = 8;

    // ── Buffers / limits ──────────────────────────────────────────────────────
    public const int  BufferSize          = 65_536;
    public const int  FrameHeaderSize     = 8;
    public const long FileMaxBytes        = 50L * 1024 * 1024; // 50 MB

    // ── Video quality presets ─────────────────────────────────────────────────
    public static readonly Dictionary<string, (int W, int H)?> Resolutions = new()
    {
        { "144p",  (256,  144)  },
        { "240p",  (426,  240)  },
        { "360p",  (640,  360)  },
        { "480p",  (854,  480)  },
        { "720p",  (1280, 720)  },
        { "1080p", (1920, 1080) },
        { "Source", null        }
    };

    public static readonly string[] FpsOptions = ["30", "60", "90", "120", "Source"];
}
