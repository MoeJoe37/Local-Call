# Security and Protocol Notes

This Python edition is aligned with the C++ Local Call secure RTC protocol.

## Signaling

LAN signaling uses the same TCP frame format as the C++ app:

```text
[4-byte big-endian uint32 length][UTF-8 JSON]
```

The JSON envelope uses:

```text
protocol = localcall.v1
schema   = 1
```

Critical messages are Ed25519 signed after removing `auth_signature` and serializing canonical JSON with sorted keys and compact separators.

## Critical signed messages

The app signs and verifies:

- `friend_req`
- `friend_acc`
- `friend_rej`
- `call_inv`
- `call_acc`
- `call_rej`
- `call_end`
- `rtc_offer`
- `rtc_answer`
- `rtc_ice`

For C++ compatibility, critical messages use only fields known by the C++ `SigMsg` structure. Extra unknown fields are avoided because the C++ verifier canonicalizes through `SigMsg` and would otherwise calculate a different signature payload.

## Device identity

Identity file:

```json
{
  "version": 1,
  "algorithm": "ed25519",
  "public_key_ed25519_raw_b64url": "...",
  "private_key_ed25519_raw_b64url": "...",
  "fingerprint_sha256_b64url": "..."
}
```

Fingerprint:

```text
base64url_no_padding(SHA256(raw_public_key))
```

## Trust model

The app uses trust-on-first-use peer key pinning. If a known peer ID appears with a different public key, the message is blocked.

## RTC transport

The Python client uses WebRTC ICE/DTLS/SCTP DataChannels through `aiortc`, matching the current C++ libdatachannel media path.

Channel labels:

```text
localcall-video
localcall-audio
```

Both are opened unordered with `maxRetransmits = 0` to minimize latency.

## Media payloads

Video:

```text
H.264 Annex-B encoded frames
```

Audio:

```text
raw Opus packets, 48 kHz mono, 10 ms target frame size
```

## LCM1 frame chunking

Every encoded frame is split into one or more DataChannel messages:

```text
0..3   magic:  "LCM1"
4      tag:    "V" or "A"
5      version: 1
6..7   chunk index
8..9   total chunks
10..13 frame id
14..15 payload length
16..   payload
```

All integer fields are big-endian. The chunk payload size is 16 KiB, matching C++ `RtcPeer.cpp`.

## Internet calling

The bundled `signaling_server.py` relays signaling only. It cannot decrypt media. For difficult NAT, configure a TURN server through `LOCALCALL_ICE_SERVERS`.
