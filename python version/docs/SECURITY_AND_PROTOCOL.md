# Security and Protocol Notes

## Goals

The secure Python edition is designed to turn the original LAN-only prototype into a low-latency RTC app with:

- End-to-end encrypted media through WebRTC DTLS-SRTP/SRTP.
- Signed critical signaling through Ed25519.
- NAT traversal through ICE with STUN/TURN.
- Internet calling through a WebSocket signaling relay.
- WebRTC-managed packet loss and congestion behavior without adding high-latency app-level retransmission.

## What the signaling server can and cannot see

The WebSocket signaling server can see JSON metadata such as peer IDs, room names, and SDP/candidate text. It does not receive plaintext audio or video. Media encryption is negotiated by WebRTC endpoints and transported through SRTP.

## Device identity

Each client creates a persistent Ed25519 keypair.

Stored fields:

```json
{
  "version": 1,
  "algorithm": "ed25519",
  "public_key_ed25519_raw_b64url": "...",
  "private_key_ed25519_raw_b64url": "...",
  "fingerprint_sha256_b64url": "..."
}
```

The fingerprint is:

```text
base64url_no_padding(SHA256(raw_public_key))
```

## Signed event format

Critical events include:

```json
{
  "protocol": "localcall.v1",
  "schema": 1,
  "type": "rtc_offer",
  "from_id": "abcd1234",
  "from_name": "MoeJoe",
  "target_id": "peer1234",
  "transport": "webrtc-dtls-srtp",
  "auth_alg": "ed25519",
  "auth_public_key": "...",
  "auth_fingerprint": "...",
  "auth_signature": "...",
  "ts": 1777780000000
}
```

The signature is calculated over canonical JSON after removing `auth_signature`:

```text
json.dumps(message_without_auth_signature, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
```

This matches the sorted-key canonical representation expected by the updated C++ protocol design.

## Trust-on-first-use

The app pins the first public key seen for each peer ID. Later messages from the same peer ID must use the same public key. If the key changes, messages are blocked.

A key change may mean:

- impersonation attempt,
- peer reinstalled the app,
- peer deleted their identity file,
- peer moved to a new device.

## RTC event types

| Type | Purpose |
|---|---|
| `hello` | Presence announcement over WebSocket room |
| `call_inv` | Request to start a call |
| `call_acc` | Accept a call |
| `call_rej` | Reject a call |
| `call_end` | End a call |
| `rtc_offer` | WebRTC SDP offer |
| `rtc_answer` | WebRTC SDP answer |
| `rtc_ice` | Reserved for trickle ICE candidates |

The current implementation exchanges complete SDP offers/answers after ICE gathering. Trickle ICE is reserved for future setup-speed improvements.

## LAN wire framing

LAN direct signaling uses:

```text
[4-byte big-endian uint32 length][UTF-8 JSON]
```

The receiver rejects frames larger than 8 MiB.

## Low-latency choices

- WebRTC media path instead of custom TCP streams.
- 10 ms audio frame target.
- 360p/30 FPS default video.
- No app-level retransmission for live media.
- TURN is supported but should be treated as fallback.

## What remains out of scope

- Full user accounts.
- Federation.
- Matrix homeserver compatibility.
- Group calling.
- Server-side room authorization.
- Perfect forward secrecy for signaling metadata. Media key negotiation is handled by WebRTC.
