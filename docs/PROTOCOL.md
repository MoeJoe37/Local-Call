# Local Call Wire Protocol

Local Call keeps the classic protocol intentionally small so Windows, Linux, macOS, and future clients can interoperate without requiring a central server for LAN use.

Version 2 adds a secure RTC path for calls while preserving the original length-prefixed JSON framing.

## Design Principles

Inspired by Matrix-style communication design, Local Call treats each network action as a typed event with an extensible JSON body:

- **Open event envelope:** every TCP message has a `type`, `from_id`, `from_name`, and `ts`.
- **Forward compatibility:** unknown JSON fields must be ignored by receivers.
- **Backward compatibility:** the framing stayed the same as older Local Call builds.
- **Security extension:** critical v2 call events include an Ed25519 public key, fingerprint, and signature.
- **Low-latency RTC:** media uses WebRTC ICE + DTLS/SCTP DataChannels instead of raw app UDP when available.

Local Call is not a Matrix client and does not federate with Matrix homeservers. The inspiration is architectural: versioned JSON events, extensibility, and user-controlled communication.

## Classic Framing

All local signaling messages use TCP port `50010` and are framed as:

```text
[4-byte big-endian unsigned length][UTF-8 JSON body]
```

The frame length is limited by the receiver to avoid accidental memory exhaustion.

## Event Envelope

New builds may send these metadata fields:

```json
{
  "protocol": "localcall.v1",
  "schema": 1,
  "app_version": "2.0.13",
  "platform": "linux",
  "type": "chat_text",
  "from_id": "abcd1234",
  "from_name": "MoeJoe",
  "ts": 1777780000000
}
```

Older builds do not send `protocol`, `schema`, `app_version`, or `platform`; new builds still parse older events where safe.

## Signed Critical Events

Version 2 signs critical call and trust-establishment events with Ed25519:

```json
{
  "type": "call_inv",
  "from_id": "abcd1234",
  "from_name": "MoeJoe",
  "mode": "video",
  "auth_alg": "ed25519",
  "auth_public_key": "base64url-raw-public-key",
  "auth_fingerprint": "base64url-sha256-public-key",
  "auth_signature": "base64url-ed25519-signature",
  "ts": 1777780000000
}
```

The signature is calculated over the canonical JSON event after removing `auth_signature`. Friends store the public key and fingerprint on first accepted contact, then future critical call events must verify against the pinned public key.

## RTC Events

Secure calls use these additional event types:

| Type | Purpose |
|---|---|
| `rtc_offer` | WebRTC SDP offer |
| `rtc_answer` | WebRTC SDP answer |
| `rtc_ice` | ICE candidate |

Example `rtc_offer` body:

```json
{
  "type": "rtc_offer",
  "transport": "webrtc-dtls-srtp",
  "rtc_session_id": "uuid",
  "sdp_type": "offer",
  "sdp": "v=0...",
  "auth_alg": "ed25519",
  "auth_public_key": "...",
  "auth_fingerprint": "...",
  "auth_signature": "..."
}
```

## Discovery

UDP discovery uses port `50005` with:

- broadcast to `255.255.255.255`
- subnet broadcast per active IPv4 interface
- multicast group `239.255.42.99`
- TCP `/24` scan fallback using `disc_probe` / `disc_resp`

Discovery packets are compact JSON and remain compatible with older builds.

## Current Ports

| Purpose | Protocol | Port |
|---|---:|---:|
| LAN discovery | UDP | 50005 |
| Signaling/events | TCP | 50010 |
| Legacy audio stream | UDP | 50100 |
| Legacy video stream | UDP | 50105 |
| Legacy group call | UDP | 50200 |
| WebRTC media | UDP/TCP selected by ICE | dynamic |

## Compatibility Rule

Any future protocol change must follow this rule:

1. Do not change the 4-byte length prefix unless the protocol name changes.
2. Add fields instead of renaming fields.
3. Ignore unknown fields on read.
4. Keep old event `type` names alive or provide a translation layer.
5. Keep payload size limits on every receiver.
6. Sign new critical trust/call events before sending.

## v2.0.13 transport correction

The libdatachannel build distributed by vcpkg exposes the stable WebRTC DataChannel API but does not expose the experimental RTP packetizer helper classes that earlier drafts attempted to use. Local Call v2.0.13 therefore transports encoded Opus and H.264 payloads through two low-latency WebRTC DataChannels:

- `localcall-audio`
- `localcall-video`

Both channels are configured as unordered with `maxRetransmits = 0` to prioritize fresh audio/video over delayed delivery. Payloads are chunked with a small Local Call frame header, reassembled by frame id, and stale incomplete frames are discarded. This keeps the secure ICE/DTLS WebRTC path while avoiding non-portable libdatachannel RTP helper APIs.
