# Local Call Pro  —  C# / WPF Port

A LAN peer-to-peer calling application that mirrors the original Python / PyQt6 app.

## Features

| Feature | Status |
|---|---|
| LAN peer discovery (UDP broadcast) | ✅ |
| Voice call (microphone → speaker) | ✅ |
| Video call (webcam) | ✅ |
| Screen sharing | ✅ |
| Adjustable resolution & FPS | ✅ |
| Mute / unmute | ✅ |
| Funny display names | ✅ |
| Edit profile name | ✅ |
| Chat / file transfer | 🚧 Coming soon |

## Prerequisites

| Requirement | Notes |
|---|---|
| **.NET 8 SDK** | https://dotnet.microsoft.com/download |
| **Windows 10 / 11** | WPF is Windows-only |
| **Webcam** | Optional – needed for video calls |
| **Microphone + speakers** | Optional – needed for voice |

## Quick Start

```bash
# 1. Restore NuGet packages & build
cd LocalCallPro
dotnet restore
dotnet build -c Release

# 2. Run
dotnet run
```

The app auto-discovers other instances on the same LAN within ~2 seconds.

## How to make a call

1. Launch the app on two machines on the same Wi-Fi / LAN.
2. Each machine will appear in the other's **Online Peers** list.
3. Click a peer → choose **Voice / Video Call** or **Share Screen**.
4. Adjust **Quality** and **FPS** sliders during the call.
5. Click **🛑 End Call** to hang up.

## Network ports used

| Port | Purpose |
|---|---|
| **50005** UDP | Peer discovery broadcast |
| **50100** UDP | Video stream (send & receive) |
| **50105** UDP | Audio stream (send & receive) |

> Make sure your firewall allows these UDP ports (both inbound and outbound).

## Compatibility with Python version

The C# app is wire-compatible with the original Python app.
Both sides use the same frame-header format:

```
Python:  struct.pack("?I", is_last_chunk, chunk_size)
         = 8 bytes  (1 bool + 3-byte pad + 4-byte uint, native alignment on x86/x64)

C#:      byte[0]   – end-of-frame flag (0 or 1)
         byte[1-3] – alignment padding (always 0)
         byte[4-7] – chunk payload length as uint32 little-endian
         byte[8..] – JPEG chunk payload
```

## NuGet packages

| Package | Version | Purpose |
|---|---|---|
| NAudio | 2.2.1 | Audio capture / playback |
| OpenCvSharp4 | 4.9.0.20240103 | Camera capture, JPEG encode/decode |
| OpenCvSharp4.runtime.win | 4.9.0.20240103 | Windows native OpenCV binaries |

## Project structure

```
LocalCallPro/
├── LocalCallPro.csproj   – project + NuGet references
├── App.xaml / .cs        – WPF application entry point
├── MainWindow.xaml / .cs – main UI (lobby + call room)
├── InputDialog.xaml / .cs– name-edit dialog (replaces QInputDialog)
├── Helpers.cs            – GetFunnyName, GetLocalIp
├── MediaSettings.cs      – port constants, resolution / FPS tables
├── PeerInfo.cs           – peer data model
├── PeerDiscovery.cs      – UDP broadcast peer discovery
└── MediaWorker.cs        – audio + video send / receive workers
```
