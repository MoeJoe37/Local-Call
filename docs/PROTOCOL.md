# Local Call Wire Protocol

Local Call keeps its protocol intentionally small so Windows, Linux, macOS, and future clients can interoperate without a central server.

## Design Principles

Inspired by Matrix-style communication design, Local Call now treats each network action as a typed event with an extensible JSON body:

- **Open event envelope:** every TCP message has a `type`, `from_id`, `from_name`, and `ts`.
- **Forward compatibility:** unknown JSON fields must be ignored by receivers.
- **Backward compatibility:** the framing stayed the same as older Local Call builds.
- **Room-like groups:** group messages carry `group_id`, `group_name`, and membership/permission data.
- **No central dependency:** LAN discovery and delivery remain peer-to-peer.

Local Call is not a Matrix client and does not federate with Matrix homeservers. The inspiration is architectural: versioned JSON events, extensibility, and user-controlled communication.

## Framing

All signaling messages use TCP port `50010` and are framed as:

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
  "app_version": "1.2.0",
  "platform": "linux",
  "type": "chat_text",
  "from_id": "abcd1234",
  "from_name": "MoeJoe",
  "ts": 1777780000000
}
```

Older builds do not send `protocol`, `schema`, `app_version`, or `platform`; new builds accept that. Older builds ignore these fields because their parser only reads known fields.

## Discovery

UDP discovery uses port `50005` with:

- broadcast to `255.255.255.255`
- subnet broadcast per active IPv4 interface
- multicast group `239.255.42.99`
- TCP `/24` scan fallback using `disc_probe` / `disc_resp`

Discovery packets are compact JSON and remain compatible with older builds:

```json
{
  "protocol": "localcall.v1",
  "schema": 1,
  "version": "1.2.0",
  "platform": "linux",
  "id": "abcd1234",
  "name": "MoeJoe"
}
```

## Current Ports

| Purpose | Protocol | Port |
|---|---:|---:|
| LAN discovery | UDP | 50005 |
| Signaling/events | TCP | 50010 |
| Audio stream | UDP | 50100 |
| Video stream | UDP | 50105 |
| Group call | UDP | 50200 |

## Compatibility Rule

Any future protocol change must follow this rule:

1. Do not change the 4-byte length prefix unless the protocol name changes.
2. Add fields instead of renaming fields.
3. Ignore unknown fields on read.
4. Keep old event `type` names alive or provide a translation layer.
5. Keep payload size limits on every receiver.
