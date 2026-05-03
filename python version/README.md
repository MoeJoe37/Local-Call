# Local Call Pro — Python LAN Calling App

Local Call Pro is a lightweight Python desktop application for local-network voice, video, and screen-sharing communication. It uses PyQt6 for the interface, UDP broadcast discovery for finding peers on the same LAN, OpenCV for camera/video frames, PyAudio for microphone/speaker audio, and MSS for screen capture.

The app is designed for quick local communication between devices connected to the same network, without requiring a central server.

---

## Features

- **Automatic LAN peer discovery** using UDP broadcast.
- **Random local profile name** generated on startup.
- **Editable display name** from the profile button.
- **Peer list** showing online devices on the same network.
- **Voice calling** using microphone and speakers.
- **Camera video sharing** using OpenCV.
- **Screen sharing** using MSS screen capture.
- **Quality controls** for video resolution and FPS.
- **Simple dark UI** built with PyQt6.
- **No account or cloud server required**.

---

## Current Architecture

Local Call Pro currently works as a direct LAN communication tool:

| Component | Technology | Purpose |
|---|---|---|
| GUI | PyQt6 | Desktop application interface |
| Peer discovery | UDP broadcast | Finds other app instances on the same LAN |
| Video capture | OpenCV | Reads webcam frames and encodes JPEG packets |
| Screen capture | MSS + NumPy + OpenCV | Captures and encodes the screen |
| Audio | PyAudio | Captures and plays PCM audio |
| Media transport | UDP sockets | Sends audio/video/screen packets directly |

---

## Important Security Note

This version is intended for **trusted local networks only**.

It does **not** currently provide:

- End-to-end encryption.
- User authentication.
- NAT traversal.
- Internet calling.
- Packet retransmission or congestion control.

Do not expose the media ports directly to the internet. Use it only on a trusted LAN, private Wi-Fi network, or isolated test network.

---

## Requirements

### Python

Recommended:

- Python **3.10 or newer**

Tested dependency target:

- PyQt6
- NumPy
- OpenCV
- PyAudio
- MSS

Install the Python packages with:

```bash
pip install -r requirements.txt
```

---

## System Dependencies

Some packages, especially **PyAudio**, require system audio development libraries before `pip install` can succeed.

### Fedora / Nobara / Bazzite

```bash
sudo dnf install -y python3 python3-pip portaudio portaudio-devel python3-devel gcc gcc-c++ make
```

Then install the Python dependencies:

```bash
pip install -r requirements.txt
```

### Arch / EndeavourOS / Manjaro

```bash
sudo pacman -S --needed python python-pip portaudio base-devel
```

Then install the Python dependencies:

```bash
pip install -r requirements.txt
```

### Debian / Ubuntu / Linux Mint

```bash
sudo apt update
sudo apt install -y python3 python3-pip python3-venv portaudio19-dev python3-dev build-essential
```

Then install the Python dependencies:

```bash
pip install -r requirements.txt
```

### Windows

Install Python from the official Python website or Microsoft Store, then run:

```powershell
pip install -r requirements.txt
```

If PyAudio fails to install on Windows, install a compatible prebuilt wheel or use a Python version that has a matching PyAudio wheel available.

---

## Recommended Virtual Environment Setup

Using a virtual environment keeps the app dependencies separate from the system Python installation.

### Linux / macOS

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
python Localcall.py
```

### Windows PowerShell

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r requirements.txt
python Localcall.py
```

---

## Running the App

After installing the dependencies:

```bash
python Localcall.py
```

Open the app on two or more computers connected to the same local network. Each device should appear in the **ONLINE PEERS** list.

---

## Network Ports

The application uses the following ports by default:

| Port | Protocol | Purpose |
|---:|---|---|
| `50005` | UDP | LAN peer discovery broadcast |
| `50100` | UDP | Video or screen-share stream |
| `50105` | UDP | Audio stream |

Make sure your firewall allows UDP traffic on these ports inside the local network.

### Fedora / Nobara / Bazzite firewall example

```bash
sudo firewall-cmd --add-port=50005/udp --permanent
sudo firewall-cmd --add-port=50100/udp --permanent
sudo firewall-cmd --add-port=50105/udp --permanent
sudo firewall-cmd --reload
```

### Arch Linux with UFW example

```bash
sudo ufw allow 50005/udp
sudo ufw allow 50100/udp
sudo ufw allow 50105/udp
```

### Windows Defender Firewall

Allow Python through Windows Defender Firewall when prompted, or manually allow inbound UDP traffic for:

- `50005`
- `50100`
- `50105`

---

## How to Use

1. Start the app on the first computer.
2. Start the app on another computer on the same LAN.
3. Wait for the second device to appear in **ONLINE PEERS**.
4. Select a peer.
5. Choose one of the available interaction options:
   - **Voice/Video Call**
   - **Share Screen**
6. Use the call controls to:
   - Change resolution.
   - Change FPS.
   - Mute/unmute audio.
   - End the session.

---

## Troubleshooting

### No peers appear

Check the following:

- Both devices are on the same Wi-Fi or Ethernet network.
- The network is not set to client isolation mode.
- UDP broadcast is allowed by the router.
- Firewall allows UDP port `50005`.
- VPN software is not forcing traffic through a virtual adapter.

### Camera does not work

Check:

- OpenCV is installed.
- The camera is not being used by another app.
- The operating system gave Python camera permission.
- On Linux, the user has access to `/dev/video0`.

Linux check:

```bash
ls /dev/video*
```

### Audio does not work

Check:

- PyAudio installed correctly.
- PortAudio system libraries are installed.
- The selected microphone/speaker works in the operating system.
- Firewall allows UDP port `50105`.

### Screen sharing does not work

Check:

- MSS is installed.
- Screen capture permissions are granted.
- On Wayland sessions, screen capture behavior can vary by desktop environment and security policy. X11 sessions usually work more predictably with direct screen capture libraries.

### PyAudio installation fails

Install PortAudio development headers first, then retry:

Fedora/Nobara/Bazzite:

```bash
sudo dnf install -y portaudio-devel python3-devel gcc gcc-c++ make
pip install PyAudio
```

Arch:

```bash
sudo pacman -S --needed portaudio base-devel
pip install PyAudio
```

Debian/Ubuntu:

```bash
sudo apt install -y portaudio19-dev python3-dev build-essential
pip install PyAudio
```

---

## Development Notes

The current app is implemented in a single Python file:

```text
Localcall.py
```

Main classes:

| Class | Purpose |
|---|---|
| `MediaSettings` | Holds resolution and FPS options |
| `MediaWorker` | Sends or receives audio/video/screen UDP streams |
| `PeerDiscovery` | Broadcasts and receives peer presence messages |
| `App` | Main PyQt6 application window and UI flow |

---

## Known Limitations

- Chat and file/media buttons are present in the UI but are not fully implemented in this Python version.
- Calls are started directly after selecting a peer; there is no accept/reject invitation workflow yet.
- UDP media packets can be lost on unstable networks.
- Audio/video synchronization is basic.
- No encryption or authentication is implemented.
- Internet calls are not supported without additional relay/NAT traversal architecture.

---

## Suggested Future Improvements

- Add an invitation/accept/reject call flow.
- Add encrypted signaling and media transport.
- Add TCP or QUIC-based reliable file transfer.
- Add chat messaging.
- Add configurable ports from the UI.
- Add device selection for microphone, speaker, and camera.
- Add better packet framing and frame IDs for video chunks.
- Add protocol version metadata for compatibility between future builds.
- Add a settings page and persistent user preferences.
- Package the app with PyInstaller for Windows and AppImage/Flatpak for Linux.

---

## License

No license file is included in this package. Before publishing the project publicly, add a clear license file such as MIT, GPL, Apache-2.0, or another license that matches your intended usage rights.
