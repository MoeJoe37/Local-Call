# Local Call Wire Protocol

Local Call keeps the classic protocol intentionally small so Windows, Linux, macOS, and future clients can interoperate without requiring a central server for LAN use.

Signalling uses length-prefixed JSON. Call media uses a separate fixed-size binary header, `LCM3`, documented below.

## Design Principles

Inspired by Matrix-style communication design, Local Call treats each network action as a typed event with an extensible JSON body:

- **Open event envelope:** every TCP message has a `type`, `from_id`, `from_name`, and `ts`.
- **Forward compatibility:** unknown JSON fields must be ignored by receivers.
- **Backward compatibility:** the framing stayed the same as older Local Call builds.
- **Security extension:** critical v2 call events include an Ed25519 public key, fingerprint, and signature.
- **Low-latency media:** call media is carried out-of-band over UDP (or TCP when UDP is blocked) using the `LCM3` header, not over the signalling channel.

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

## Replies

`chat_text` and `grp_text` events may quote the message they answer. All three
fields are optional and are only written when a reply is actually being sent, so
older builds neither receive nor need them:

```json
{
  "type": "chat_text",
  "text": "Sounds good",
  "reply_to_ts": 1777779000000,
  "reply_name": "MoeJoe",
  "reply_snippet": "Are we still on for 6?"
}
```

| Field | Type | Meaning |
|---|---|---|
| `reply_to_ts` | int64 | `ts` of the quoted message — the key chat history is stored under |
| `reply_name` | string | Display name of the quoted message's sender |
| `reply_snippet` | string | Short excerpt, so the quote renders without looking the original up |

A receiver that does not understand these fields ignores them and shows a plain
message, which is why the snippet travels with the reply rather than being
resolved from local history.

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
| Signalling / events | TCP | 50010 |
| Call media (audio + video + control) | UDP | 50100 |
| Call media fallback | TCP | 50120 |
| Group call | UDP | 50200 |
| WebRTC media (optional build) | UDP/TCP selected by ICE | dynamic |

## Compatibility Rule

Any future protocol change must follow this rule:

1. Do not change the 4-byte length prefix unless the protocol name changes.
2. Add fields instead of renaming fields.
3. Ignore unknown fields on read.
4. Keep old event `type` names alive or provide a translation layer.
5. Keep payload size limits on every receiver.
6. Sign new critical trust/call events before sending.

## Media Framing (`LCM3`)

Call media never travels over the signalling socket. Both media transports — UDP on 50100 and
the TCP fallback on 50120 — carry the same 24-byte header, so the two cannot drift apart. It
replaces the four incompatible headers earlier builds used (`LCJ1`, `LCA1`, `LCM2`, `LCU1`).

```text
offset  size  field
  0      4    magic         'L','C','M','3'
  4      1    tag           'A' audio | 'V' video | 'H' hello
                            'K' keyframe request | 'P' ping | 'O' pong
  5      1    flags         bit0 keyframe, bit1 silence/DTX
  6      4    seq           frame sequence, big-endian, per media kind
 10      4    timestampMs   capture clock, big-endian
 14      2    chunkIndex    big-endian
 16      2    chunkCount    big-endian
 18      2    payloadLen    big-endian, bytes in this chunk
 20      4    senderToken   random per session, used to reject our own packets
              ----
               24
```

Frames larger than 1180 bytes are split into chunks that share a `seq` and are reassembled by
`chunkIndex`. A frame whose chunks have not all arrived when a newer sequence completes is
discarded rather than delaying playback. On TCP the same header is read as a byte stream; a
receiver that loses sync scans forward to the next magic instead of dropping the connection.

### Payloads

| Tag | Payload |
|---|---|
| `A` | one Opus packet, 48 kHz mono, 20 ms. Empty with `FlagSilence` set means muted or DTX. |
| `V` | one complete H.264 Annex-B access unit. `FlagKeyframe` marks an IDR. |
| `H` | 8-byte hello: audio codec (1), video codec (1), sample rate (4, BE), channels (1), can-receive-video (1). |
| `K` | empty. Asks the peer to emit an IDR now. |
| `P` / `O` | empty. RTT probe and its reply, answered by the transport itself. |

### Connection setup

Both sides send `H` every 250 ms on UDP until they receive one. If nothing arrives within
1.5 s the call reopens on TCP and repeats the exchange. A peer that reports
`canReceiveVideo = 0` (a voice call) is never sent video.

### Congestion

TCP drops video frames — never audio — when the socket's write queue exceeds 256 KB, and
counts them in the call statistics. UDP does not queue, so nothing is dropped locally. Both
transports report inbound loss once a second, and sustained loss above 5 % steps the video
encoder's bitrate down.
