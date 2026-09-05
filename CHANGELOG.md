# Changelog

## 2.0.22 — Real call media

Calls previously opened a window and carried nothing. This release replaces the media stack.

### Calls

- **Codecs are actually used now.** `VideoEncoderWorker` and `VideoDecoderWorker` existed but
  were never instantiated, and no Opus encoder was ever created. The wire format was JPEG
  frames plus raw Int16 PCM — several Mbit/s of video and ~768 kbit/s of uncompressed audio.
  Video is now OpenH264 baseline and audio is Opus (48 kHz mono, 20 ms, ~32 kbit/s, inband FEC).
- **Encoding moved off the GUI thread.** Colour conversion, scaling and H.264 encoding used to
  run inline in the camera callback 30 times a second, which stalled the whole UI. The camera
  and screen callbacks now only hand a frame to the encoder thread.
- **New `LCM3` wire format** — one 24-byte header shared by both transports, replacing the
  three incompatible ones (`LCJ1`, `LCA1`, `LCM2`, `LCU1`). Carries a tag, flags (keyframe /
  silence), sequence, capture timestamp, chunk index/count and a sender token for loopback
  rejection.
- **One UDP socket for everything.** Audio, video and control are multiplexed by tag on
  port 50100 instead of one socket per media kind, so a call can no longer come up with audio
  working and video silently blocked by a missing firewall hole.
- **TCP fallback fixed and made usable.** The old TCP path sent only on the locally-initiated
  socket, so if one side's outbound connect was blocked it received but never sent while still
  reporting "connected". It now sends on any connected socket, applies backpressure by
  dropping video (never audio) when the send queue exceeds 256 KB, and resynchronises on the
  next header rather than dropping the call after a framing error.
- **Transport selection is automatic.** UDP is probed with `Hello` packets for 1.5 s, then the
  call falls back to TCP. Previously this was only reachable through undocumented environment
  variables, and TCP was the default.
- **Adaptive jitter buffer.** Remote audio used to be written straight into the audio sink as
  it arrived, so reordering scrambled it, loss clicked, and clock drift eventually broke
  playback. Audio is now reordered by sequence, concealed with Opus PLC on gaps, and pulled by
  the sink on the sound card's clock with an adaptive 60–240 ms target delay.
- **Keyframe requests.** A decoder that has not seen an IDR asks the peer for one instead of
  showing black until the next intra period.
- **Adaptive bitrate.** Sustained inbound loss above 5 % steps video bitrate down; it recovers
  slowly once loss clears.
- **Real call statistics.** The old ping measured a TCP connect to the *signalling* port — the
  wrong path entirely. The stats overlay now shows media-path RTT, up/down bitrate, loss,
  jitter, inbound resolution and frame rate, codecs, transport, and dropped frames.
- Unanswered calls stop ringing after 45 s on both sides.

### Build

- **Media no longer requires libdatachannel.** `HAS_WEBRTC` used to gate every media source
  file and was only set when libdatachannel *and* Opus *and* OpenH264 *and* libyuv *and* Qt
  Multimedia were all found, so missing any one of them left the app with no media path at
  all. Gating is now layered: `HAS_MEDIA_AUDIO` (Qt Multimedia + Opus), `HAS_MEDIA_VIDEO`
  (+ OpenH264 + libyuv), `HAS_WEBRTC` (+ libdatachannel, for the optional `RtcPeer` only).
- The configure summary prints `Calls: voice+video | voice only | disabled`, naming the
  missing package.
- Removed the OpenCV `MediaWorker` path, the raw `UdpMediaPeer` fallback, `MediaTcpPeer`, and
  the `LOCALCALL_WITH_OPENCV` option. `MdnsDiscovery` was compiled but referenced nowhere and
  is no longer built.

### UI

- Rebuilt call window: video painted directly in `paintEvent` with aspect-fit scaling instead
  of a 30 fps `QLabel::setPixmap` treadmill, a self-view picture-in-picture, call timer,
  encryption and transport badges, round control bar, toggleable stats overlay, double-click
  or F11 fullscreen with auto-hiding controls.
- The camera button no longer starts in the "on" state during a voice call.
- **One stylesheet for the whole app.** The theme moved out of a raw string literal in
  `MainWindow.cpp` into `resources/theme/localcall.qss`, applied once at startup. All ~75
  inline `setStyleSheet` calls — sidebar rows, discovered peers, friend requests, chat
  bubbles, file and voice-note cards, toasts, and the input/group dialogs — are now
  `objectName` and `class` selectors in that file, so colours live in exactly one place.
  Widget state (online, locked, mine/theirs) is a `class` property rather than a rebuilt
  stylesheet string.
- Incoming-call and group-invite toasts colour their Answer/Decline buttons instead of
  showing two identical grey ones.

### Docs

- README rewritten to describe the stack that actually ships. The previous "secure RTC-first"
  capability matrix described a WebRTC/H.264/Opus path that was not the code being run.
- `docs/PROTOCOL.md` documents the `LCM3` media header.

## 2.0.15

- Fixed the Windows `FirewallHelper.cpp` build break caused by missing Windows API includes
  and a missing namespace wrapper.
- Linked the Qt application target against `shell32` and `advapi32` for elevated firewall setup.
- Sent voice notes through the chunked-transfer path instead of one large signalling message.
- Added in-app voice-note playback through Qt Multimedia.
- Tested Opus-safe Windows audio formats instead of falling back to unsupported preferred ones.
- Hardened video/screen encoding against odd frame dimensions.

## 2.0.13

### Windows launcher runtime fix

Windows builds produce two executables:

- `LocalCall.exe` — a native launcher with no Qt imports. Run this one.
- `LocalCallApp.exe` — the real Qt application. Do not run it directly.

The launcher repairs stale Qt DLLs from `localcall-qt-prefix.txt`, prepares the DLL/plugin
search path, smoke-loads Qt from the deployed folder, then starts `LocalCallApp.exe`. This
prevents repeated Windows loader entry-point popups caused by an old MinGW or old-version
`Qt6Widgets.dll`.

### Runtime

- Windows post-build Qt deployment enabled by default.
- `scripts/deploy-windows.ps1` deletes stale Qt DLLs/plugins, runs the exact `windeployqt.exe`
  from the Qt kit passed to CMake, writes `qt.conf`, and verifies every deployed `Qt6*.dll`
  by SHA-256 against that kit.
- Added `scripts/check-windows-runtime.ps1` and `scripts/run-windows.ps1`.
- Removed `QSlider` from the call UI in favour of a quality dropdown, avoiding
  `QSlider`-specific entry-point errors when a stale Qt DLL is loaded.

### Security and signalling

- Persistent Ed25519 device identity using OpenSSL.
- Signed friend requests, call invites, call accept/end, SDP offers/answers and ICE candidates.
- Fingerprint pinning fields on saved friends and pending requests.
- RTC signalling events `rtc_offer`, `rtc_answer`, `rtc_ice`.
- Configurable ICE servers through `LOCALCALL_ICE_SERVERS`.
- Kept the original length-prefixed JSON framing so older builds still understand non-RTC events.

### Build fixes

- Replaced the invalid `rtc/opusrtpdepacketizer.hpp` include with `rtc/rtpdepacketizer.hpp`.
- Replaced unsupported OpenH264 symbols with `PRO_BASELINE` and `VIDEO_BITSTREAM_DEFAULT`.
- Replaced non-portable libdatachannel RTP helper classes with DataChannels and Local Call
  frame chunking, fixing MSVC/vcpkg errors such as `RtpPacketizationConfig is not a member of rtc`.
- Fixed a configure error caused by a backslash in the `LOCALCALL_POST_BUILD_DEPLOY_QT`
  option description.
- Normalised quoted paths in `cmake/deploy_windows_qt.cmake`, fixing
  `Target executable does not exist` from the MSBuild deploy step.
