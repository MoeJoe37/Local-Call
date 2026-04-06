# Local Call Pro — C++ Port

A complete C++ rewrite of the C# / WPF **Local Call Pro** LAN communication app.
The UI, protocol, and all features are preserved 1:1. Built with **Qt 6** (replaces WPF)
and **OpenCV** (replaces OpenCvSharp).

---

## Features

- 🔍 **LAN peer discovery** — UDP broadcast + multicast + TCP subnet scan
- 👥 **Friend management** — requests, accept/decline, remove, block
- 💬 **1-to-1 chat** — text, images, files (≤50 MB), voice notes
- 🏘 **Group chat** — create groups, send messages/files/voice, manage members
- 📞 **Voice & video calls** — UDP audio/video streaming via OpenCV
- 🖥 **Screen sharing** — live desktop capture during a video call
- 🔔 **Toast notifications** — incoming calls, friend requests, messages
- 🌑 **Dark theme** — matching the original Catppuccin-dark colour scheme
- 💾 **Persistent history** — chat logs stored to `%AppData%/Local Call/`
- 🛡 **Group permissions** — owner/helper roles, per-member send/file/call flags
- 🔒 **Firewall helper** — auto-adds Windows Firewall rules (UAC once)

---

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| Qt 6    | ≥ 6.5   | UI, networking, audio |
| OpenCV  | ≥ 4.8   | Camera capture, video encode/decode |
| [nlohmann/json](https://github.com/nlohmann/json) | ≥ 3.11 | JSON serialisation |

---

## Build

### Prerequisites (Windows)

```powershell
# Install Qt 6 (e.g. via Qt Online Installer → Qt 6.6 MSVC 2022)
# Install OpenCV (e.g. via vcpkg: vcpkg install opencv4)
# Install CMake ≥ 3.20
```

### CMake build

```powershell
git clone <this-repo>
cd LocalCallPro_CPP

# nlohmann/json is auto-downloaded by CMake on first configure,
# OR manually place json.hpp in:  third_party/nlohmann/json.hpp

cmake -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_PREFIX_PATH="C:/Qt/6.6.0/msvc2022_64;C:/vcpkg/installed/x64-windows"
cmake --build build --config Release
```

The binary lands at `build/Release/LocalCallPro.exe`.

### Linux / macOS

```bash
sudo apt install qt6-base-dev qt6-multimedia-dev libopencv-dev   # Ubuntu 24
cmake -B build && cmake --build build -j$(nproc)
```

> **Note:** Firewall helper is Windows-only; on Linux the ports just need to be open.

---

## Project Structure

```
LocalCallPro_CPP/
├── CMakeLists.txt
├── include/
│   ├── MediaSettings.h       # Port constants, video presets
│   ├── SigMsg.h              # Signaling protocol + JSON helpers
│   ├── PeerInfo.h            # Peer discovery record
│   ├── FriendInfo.h          # Friend + PendingRequest data
│   ├── GroupInfo.h           # Group + permissions data
│   ├── ChatMessage.h         # Chat message model + StoredMessage
│   ├── Helpers.h             # Base64, MIME, IP, random names
│   ├── PeerDiscovery.h       # LAN discovery (UDP + TCP scan)
│   ├── SignalingServer.h     # TCP server
│   ├── SignalingClient.h     # TCP client (fire-and-forget + reliable)
│   ├── FriendManager.h       # Friends/groups/pending persistence
│   ├── ChatStore.h           # Per-conversation history
│   ├── VoiceNoteRecorder.h   # Hold-to-record audio → WAV
│   ├── MediaWorker.h         # UDP audio/video streaming
│   ├── FirewallHelper.h      # Windows Firewall rules
│   ├── NotificationWindow.h  # Toast / action popup
│   ├── InputDialog.h         # Single-field input dialog
│   ├── CallWindow.h          # Voice/video call window
│   ├── GroupCreateDialog.h   # New group dialog
│   ├── GroupManageDialog.h   # Group admin panel
│   └── MainWindow.h          # Main app window
├── src/
│   ├── main.cpp
│   ├── Helpers.cpp
│   ├── PeerDiscovery.cpp
│   ├── SignalingServer.cpp
│   ├── SignalingClient.cpp
│   ├── FriendManager.cpp
│   ├── ChatStore.cpp
│   ├── VoiceNoteRecorder.cpp
│   ├── MediaWorker.cpp
│   ├── FirewallHelper.cpp
│   ├── NotificationWindow.cpp
│   ├── InputDialog.cpp
│   ├── CallWindow.cpp
│   ├── GroupCreateDialog.cpp
│   ├── GroupManageDialog.cpp
│   ├── MainWindow.cpp          # UI construction
│   └── MainWindow_logic.cpp    # All signal handlers + chat logic
└── third_party/
    └── nlohmann/
        └── json.hpp            # (auto-downloaded by CMake)
```

---

## C# → C++ Mapping

| C# (WPF)              | C++ (Qt)                          |
|-----------------------|-----------------------------------|
| `ObservableCollection`| `QList` + manual `rebuild*()`     |
| `INotifyPropertyChanged` | `QObject` + signals          |
| `Dispatcher.InvokeAsync` | `QMetaObject::invokeMethod` / direct (already on main thread via signal) |
| `Task.Run`            | `QtConcurrent::run` / `QThread`   |
| `BitmapSource`        | `QImage` / `QPixmap`              |
| `WaveInEvent`         | `QAudioSource`                    |
| `WaveOutEvent`        | `QAudioSink`                      |
| `JsonSerializer`      | `nlohmann::json`                  |
| `DataTemplate`        | `QListWidgetItem` + `setItemWidget` |
| `DataBinding`         | Manual `rebuild*()` helpers       |
| `OpenCvSharp`         | `opencv2/opencv.hpp`              |
| `NAudio`              | Qt Multimedia                     |

---

## Network Protocol

All messages are **length-prefixed JSON** over TCP (port 50010):

```
[4 bytes big-endian length][JSON body]
```

Peer discovery uses **UDP broadcast** (port 50005) + **multicast 239.255.42.99**
+ **TCP /24 subnet scan** as fallback.

Audio/video streams are **raw UDP** (ports 50100 / 50105). Video frames are
JPEG-encoded and chunked at 60 KB with an 8-byte header.
