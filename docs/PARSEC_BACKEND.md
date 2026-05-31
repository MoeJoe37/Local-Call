# Parsec SDK integration note

LocalCall does **not** bundle Parsec SDK binaries. Parsec SDK is a separate third-party SDK that requires Parsec API/session credentials (`sessionID` and `peerID`) before an application can host or join a Parsec stream.

This release keeps LocalCall self-contained and uses its own LAN media transport by default:

- Qt Multimedia captures microphone/camera.
- Qt screen capture sends screen-share frames.
- A direct UDP transport sends low-latency audio/video packets between the two LocalCall peers.
- The Windows launcher and application request Administrator rights so the app can add its own firewall rules.

If you obtain licensed Parsec SDK files and API credentials, the clean integration point is `CallWindow::startMedia()` / `MediaPipeline`:

1. Replace the `UdpMediaPeer` send/receive connection with a `ParsecMediaPeer` class.
2. Feed captured frames/audio from `MediaPipeline::encodedVideoFrame` and `MediaPipeline::encodedAudioFrame` into the Parsec host API.
3. Feed received Parsec video/audio back into `MediaPipeline::onRemoteVideoFrame` and `MediaPipeline::onRemoteAudioFrame`, or render directly to the call window.
4. Keep LocalCall signaling for friend discovery and call invitations, then exchange Parsec session data in `SigMsg`.

The current default transport was kept because it does not need external accounts, external services, or proprietary runtime files.
