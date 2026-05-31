#!/usr/bin/env python3
"""
Local Call Pro — Secure RTC Python Edition

WebRTC-first, low-latency local/internet calling app.
- WebRTC/aiortc DataChannel media path compatible with Local Call C++ v2.0.15+.
- Ed25519 signed signaling using PyNaCl.
- LAN peer discovery and TCP signaling compatible with the Local Call v1 JSON envelope.
- Optional WebSocket signaling server for internet calling.

Run:
    python Localcall.py

Optional internet signaling server:
    python signaling_server.py --host 0.0.0.0 --port 8765
"""

from __future__ import annotations

import asyncio
import base64
import hashlib
import json
import os
import platform
import queue
import random
import socket
import struct
import sys
import threading
import time
import uuid
from dataclasses import dataclass, field
from fractions import Fraction
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import urlencode, urlparse, urlunparse, parse_qsl

import numpy as np
from PyQt6.QtCore import Qt, QThread, pyqtSignal, QTimer
from PyQt6.QtGui import QImage, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSplitter,
    QStackedWidget,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

# ─────────────────────────────────────────────────────────────────────────────
# Optional media / RTC dependencies
# ─────────────────────────────────────────────────────────────────────────────

try:
    import cv2

    OPENCV_AVAILABLE = True
except Exception:
    cv2 = None
    OPENCV_AVAILABLE = False

try:
    import pyaudio

    AUDIO_AVAILABLE = True
except Exception:
    pyaudio = None
    AUDIO_AVAILABLE = False

try:
    import mss

    SCREEN_SHARE_AVAILABLE = True
except Exception:
    mss = None
    SCREEN_SHARE_AVAILABLE = False

try:
    import av
    from aiortc import (
        RTCConfiguration,
        RTCIceServer,
        RTCPeerConnection,
        RTCSessionDescription,
    )
    from aiortc.sdp import candidate_from_sdp, candidate_to_sdp

    AIORTC_AVAILABLE = True
except Exception:
    av = None
    RTCConfiguration = None  # type: ignore
    RTCIceServer = None  # type: ignore
    RTCPeerConnection = None  # type: ignore
    RTCSessionDescription = None  # type: ignore
    candidate_from_sdp = None  # type: ignore
    candidate_to_sdp = None  # type: ignore
    AIORTC_AVAILABLE = False

try:
    from nacl.exceptions import BadSignatureError
    from nacl.signing import SigningKey, VerifyKey

    NACL_AVAILABLE = True
except Exception:
    SigningKey = None  # type: ignore
    VerifyKey = None  # type: ignore
    BadSignatureError = Exception  # type: ignore
    NACL_AVAILABLE = False

try:
    import aiohttp

    AIOHTTP_AVAILABLE = True
except Exception:
    aiohttp = None  # type: ignore
    AIOHTTP_AVAILABLE = False


# ─────────────────────────────────────────────────────────────────────────────
# Constants
# ─────────────────────────────────────────────────────────────────────────────

APP_NAME = "Local Call Pro"
APP_VERSION = "2.2.0-python-cpp-compatible"
PROTOCOL = "localcall.v1"
SCHEMA = 1

BROADCAST_PORT = 50005
TCP_SIGNAL_PORT = 50010
BROADCAST_INTERVAL = 2.0
PEER_TIMEOUT = 10.0
MAX_SIGNAL_FRAME = 8 * 1024 * 1024

CRITICAL_SIGNED_TYPES = {
    "hello",
    "disc_probe",
    "disc_resp",
    "friend_req",
    "friend_acc",
    "friend_rej",
    "call_inv",
    "call_acc",
    "call_rej",
    "call_end",
    "rtc_offer",
    "rtc_answer",
    "rtc_ice",
}

DEFAULT_ICE_SERVERS = "stun:stun.l.google.com:19302"
DEFAULT_SIGNALING_URL = "ws://127.0.0.1:8765/ws"


# ─────────────────────────────────────────────────────────────────────────────
# Utility functions
# ─────────────────────────────────────────────────────────────────────────────


def now_ms() -> int:
    return int(time.time() * 1000)


def b64u(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def unb64u(text: str) -> bytes:
    padding = "=" * (-len(text) % 4)
    return base64.urlsafe_b64decode((text + padding).encode("ascii"))


def canonical_json(obj: Dict[str, Any]) -> bytes:
    """Canonical JSON compatible with nlohmann::json default sorted-key output."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def app_data_dir() -> Path:
    if sys.platform.startswith("win"):
        root = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
        path = Path(root) / "LocalCallPro"
    elif sys.platform == "darwin":
        path = Path.home() / "Library" / "Application Support" / "LocalCallPro"
    else:
        root = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
        path = Path(root) / "local-call-pro"
    path.mkdir(parents=True, exist_ok=True)
    return path


def atomic_write_json(path: Path, data: Dict[str, Any]) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")
    tmp.replace(path)


def get_funny_name() -> str:
    adjs = ["Silly", "Brave", "Goofy", "Turbo", "Fancy", "Sleepy", "Hyper", "Invisible"]
    nouns = ["Hamster", "Potato", "Ninja", "Wizard", "Toaster", "Unicorn", "Panda", "Cactus"]
    return f"{random.choice(adjs)} {random.choice(nouns)}"


def local_ip_guess() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # No packet is sent; this only asks the OS which interface it would use.
        s.connect(("192.0.2.1", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def platform_name() -> str:
    return f"{platform.system().lower()}-{platform.machine().lower()}"


def compact_fingerprint(fp: str) -> str:
    return ":".join(fp[i : i + 4] for i in range(0, min(len(fp), 24), 4))


def read_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("socket closed")
        data.extend(chunk)
    return bytes(data)


def send_framed_json(ip: str, msg: Dict[str, Any], port: int = TCP_SIGNAL_PORT, timeout: float = 4.0) -> None:
    body = json.dumps(msg, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    frame = struct.pack("!I", len(body)) + body
    with socket.create_connection((ip, port), timeout=timeout) as sock:
        sock.sendall(frame)


def parse_ice_servers(text: str):
    """
    Parse LOCALCALL_ICE_SERVERS style config.

    Supported formats:
      1. Semicolon list:
         stun:stun.l.google.com:19302;turn:turn.example.com:3478,username,password
      2. JSON list:
         [{"urls":"stun:..."},{"urls":"turn:...","username":"u","credential":"p"}]
    """
    if not AIORTC_AVAILABLE:
        return []
    value = (text or "").strip() or DEFAULT_ICE_SERVERS
    servers = []
    try:
        if value.startswith("["):
            for item in json.loads(value):
                servers.append(
                    RTCIceServer(
                        urls=item.get("urls"),
                        username=item.get("username"),
                        credential=item.get("credential"),
                    )
                )
            return servers
    except Exception:
        pass

    for part in [p.strip() for p in value.split(";") if p.strip()]:
        bits = [b.strip() for b in part.split(",")]
        if len(bits) == 1:
            servers.append(RTCIceServer(urls=bits[0]))
        elif len(bits) >= 3:
            servers.append(RTCIceServer(urls=bits[0], username=bits[1], credential=bits[2]))
    return servers


# ─────────────────────────────────────────────────────────────────────────────
# Security: persistent Ed25519 device identity and TOFU peer pinning
# ─────────────────────────────────────────────────────────────────────────────


class DeviceIdentity:
    def __init__(self) -> None:
        if not NACL_AVAILABLE:
            raise RuntimeError("PyNaCl is required for signed signaling")
        self.path = app_data_dir() / "identity-ed25519.json"
        self.signing_key: SigningKey
        self.public_key_b64: str
        self.fingerprint: str
        self.load_or_create()

    def load_or_create(self) -> None:
        if self.path.exists():
            try:
                obj = json.loads(self.path.read_text(encoding="utf-8"))
                private_raw = unb64u(obj["private_key_ed25519_raw_b64url"])
                self.signing_key = SigningKey(private_raw)
                self.public_key_b64 = b64u(bytes(self.signing_key.verify_key))
                self.fingerprint = b64u(hashlib.sha256(bytes(self.signing_key.verify_key)).digest())
                return
            except Exception:
                # Preserve broken identity for investigation and create a new one.
                self.path.rename(self.path.with_suffix(".broken.json"))

        self.signing_key = SigningKey.generate()
        self.public_key_b64 = b64u(bytes(self.signing_key.verify_key))
        self.fingerprint = b64u(hashlib.sha256(bytes(self.signing_key.verify_key)).digest())
        atomic_write_json(
            self.path,
            {
                "version": 1,
                "algorithm": "ed25519",
                "public_key_ed25519_raw_b64url": self.public_key_b64,
                "private_key_ed25519_raw_b64url": b64u(bytes(self.signing_key)),
                "fingerprint_sha256_b64url": self.fingerprint,
            },
        )

    def base_envelope(self, msg_type: str, my_id: str, my_name: str) -> Dict[str, Any]:
        return {
            "protocol": PROTOCOL,
            "schema": SCHEMA,
            "app_version": APP_VERSION,
            "platform": platform_name(),
            "type": msg_type,
            "from_id": my_id,
            "from_name": my_name,
            "ts": now_ms(),
        }

    def sign(self, msg: Dict[str, Any]) -> Dict[str, Any]:
        signed = dict(msg)
        signed["auth_alg"] = "ed25519"
        signed["auth_public_key"] = self.public_key_b64
        signed["auth_fingerprint"] = self.fingerprint
        signed.pop("auth_signature", None)
        signed["auth_signature"] = b64u(self.signing_key.sign(canonical_json(signed)).signature)
        return signed

    @staticmethod
    def verify(msg: Dict[str, Any], expected_public_key_b64: Optional[str] = None) -> Tuple[bool, str]:
        try:
            pub = msg.get("auth_public_key")
            sig = msg.get("auth_signature")
            if not pub or not sig:
                return False, "missing signature"
            if expected_public_key_b64 and pub != expected_public_key_b64:
                return False, "public key changed"
            expected_fp = b64u(hashlib.sha256(unb64u(pub)).digest())
            if msg.get("auth_fingerprint") and msg.get("auth_fingerprint") != expected_fp:
                return False, "fingerprint mismatch"
            payload = dict(msg)
            payload.pop("auth_signature", None)
            VerifyKey(unb64u(pub)).verify(canonical_json(payload), unb64u(sig))
            return True, "ok"
        except BadSignatureError:
            return False, "bad signature"
        except Exception as exc:
            return False, str(exc)


class PeerTrustStore:
    def __init__(self) -> None:
        self.path = app_data_dir() / "trusted-peers.json"
        self.peers: Dict[str, Dict[str, Any]] = {}
        self.load()

    def load(self) -> None:
        if self.path.exists():
            try:
                self.peers = json.loads(self.path.read_text(encoding="utf-8"))
            except Exception:
                self.peers = {}

    def save(self) -> None:
        atomic_write_json(self.path, self.peers)

    def learn_or_verify(self, peer_id: str, name: str, public_key_b64: str, fingerprint: str) -> Tuple[bool, str]:
        if not peer_id or not public_key_b64:
            return False, "missing peer identity"
        known = self.peers.get(peer_id)
        if known:
            if known.get("public_key") != public_key_b64:
                return False, "peer key changed; possible impersonation or device reset"
            known["name"] = name or known.get("name", peer_id)
            known["last_seen"] = now_ms()
            self.save()
            return True, "verified"
        self.peers[peer_id] = {
            "name": name or peer_id,
            "public_key": public_key_b64,
            "fingerprint": fingerprint,
            "first_seen": now_ms(),
            "last_seen": now_ms(),
            "trust_model": "TOFU",
        }
        self.save()
        return True, "trusted first use"

    def expected_key(self, peer_id: str) -> Optional[str]:
        item = self.peers.get(peer_id)
        return item.get("public_key") if item else None


# ─────────────────────────────────────────────────────────────────────────────
# LAN discovery and Local Call TCP-framed signaling
# ─────────────────────────────────────────────────────────────────────────────


class PeerDiscovery(QThread):
    peers_updated = pyqtSignal(dict)

    def __init__(self, my_id: str, my_name: str, identity: DeviceIdentity):
        super().__init__()
        self.my_id = my_id
        self.my_name = my_name
        self.identity = identity
        self.peers: Dict[str, Dict[str, Any]] = {}
        self.running = True

    def run(self) -> None:
        tx = threading.Thread(target=self._broadcast_loop, daemon=True)
        tx.start()
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        try:
            sock.bind(("", BROADCAST_PORT))
        except OSError:
            self.peers_updated.emit(self.peers)
            return
        sock.settimeout(1.0)
        while self.running:
            try:
                data, addr = sock.recvfrom(4096)
                msg = json.loads(data.decode("utf-8"))
                peer_id = msg.get("id") or msg.get("from_id")
                if peer_id and peer_id != self.my_id:
                    self.peers[peer_id] = {
                        "id": peer_id,
                        "name": msg.get("name") or msg.get("from_name") or peer_id,
                        "ip": addr[0],
                        "source": "lan",
                        "ts": time.time(),
                        "auth_public_key": msg.get("auth_public_key"),
                        "auth_fingerprint": msg.get("auth_fingerprint"),
                        "rtc_supported": bool(msg.get("rtc_supported", False)),
                    }
            except Exception:
                pass
            now = time.time()
            self.peers = {pid: info for pid, info in self.peers.items() if now - info.get("ts", 0) < PEER_TIMEOUT}
            self.peers_updated.emit(self.peers)
        try:
            sock.close()
        except Exception:
            pass

    def _broadcast_loop(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        while self.running:
            try:
                payload = {
                    "id": self.my_id,
                    "name": self.my_name,
                    "protocol": PROTOCOL,
                    "schema": SCHEMA,
                    "app_version": APP_VERSION,
                    "platform": platform_name(),
                    "rtc_supported": AIORTC_AVAILABLE,
                    "auth_alg": "ed25519",
                    "auth_public_key": self.identity.public_key_b64,
                    "auth_fingerprint": self.identity.fingerprint,
                }
                data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
                sock.sendto(data, ("255.255.255.255", BROADCAST_PORT))
            except Exception:
                pass
            time.sleep(BROADCAST_INTERVAL)
        try:
            sock.close()
        except Exception:
            pass

    def update_name(self, name: str) -> None:
        self.my_name = name

    def stop(self) -> None:
        self.running = False


class TcpSignalingServer(QThread):
    message_received = pyqtSignal(dict, str)
    status_changed = pyqtSignal(str)

    def __init__(self, port: int = TCP_SIGNAL_PORT):
        super().__init__()
        self.port = port
        self.running = True
        self._sock: Optional[socket.socket] = None

    def run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock = sock
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("0.0.0.0", self.port))
            sock.listen(16)
            sock.settimeout(1.0)
            self.status_changed.emit(f"LAN signaling listening on TCP {self.port}")
        except Exception as exc:
            self.status_changed.emit(f"LAN signaling failed: {exc}")
            return

        while self.running:
            try:
                conn, addr = sock.accept()
                threading.Thread(target=self._handle_conn, args=(conn, addr[0]), daemon=True).start()
            except socket.timeout:
                continue
            except Exception:
                break
        try:
            sock.close()
        except Exception:
            pass

    def _handle_conn(self, conn: socket.socket, ip: str) -> None:
        with conn:
            conn.settimeout(5.0)
            try:
                header = read_exact(conn, 4)
                size = struct.unpack("!I", header)[0]
                if size <= 0 or size > MAX_SIGNAL_FRAME:
                    return
                body = read_exact(conn, size)
                msg = json.loads(body.decode("utf-8"))
                self.message_received.emit(msg, ip)
            except Exception:
                return

    def stop(self) -> None:
        self.running = False
        try:
            if self._sock:
                self._sock.close()
        except Exception:
            pass


# ─────────────────────────────────────────────────────────────────────────────
# WebSocket signaling client for internet calling
# ─────────────────────────────────────────────────────────────────────────────


class WebSocketSignalingClient(QThread):
    message_received = pyqtSignal(dict, str)
    status_changed = pyqtSignal(str)
    peer_seen = pyqtSignal(dict)

    def __init__(self, url: str, room: str, my_id: str, my_name: str, identity: DeviceIdentity):
        super().__init__()
        self.url = url.strip()
        self.room = room.strip() or "default"
        self.my_id = my_id
        self.my_name = my_name
        self.identity = identity
        self.running = True
        self.outbox: "queue.Queue[Dict[str, Any]]" = queue.Queue()

    def send(self, msg: Dict[str, Any]) -> None:
        self.outbox.put(msg)

    def run(self) -> None:
        if not AIOHTTP_AVAILABLE:
            self.status_changed.emit("aiohttp missing: WebSocket signaling unavailable")
            return
        asyncio.run(self._run_async())

    def _connect_url(self) -> str:
        parsed = urlparse(self.url or DEFAULT_SIGNALING_URL)
        scheme = parsed.scheme or "ws"
        if scheme == "http":
            scheme = "ws"
        elif scheme == "https":
            scheme = "wss"
        path = parsed.path or "/ws"
        q = dict(parse_qsl(parsed.query))
        q.update({"room": self.room, "peer_id": self.my_id, "name": self.my_name})
        return urlunparse((scheme, parsed.netloc, path, "", urlencode(q), ""))

    async def _run_async(self) -> None:
        url = self._connect_url()
        backoff = 1.0
        while self.running:
            try:
                self.status_changed.emit(f"Connecting signaling: {url}")
                async with aiohttp.ClientSession() as session:
                    async with session.ws_connect(url, heartbeat=20, receive_timeout=None) as ws:
                        self.status_changed.emit("Internet signaling connected")
                        backoff = 1.0
                        hello = self.identity.base_envelope("hello", self.my_id, self.my_name)
                        hello.update({"target_id": "*", "room": self.room, "rtc_supported": AIORTC_AVAILABLE})
                        await ws.send_json(self.identity.sign(hello))

                        recv_task = asyncio.create_task(ws.receive())
                        while self.running:
                            try:
                                while True:
                                    msg = self.outbox.get_nowait()
                                    await ws.send_json(msg)
                            except queue.Empty:
                                pass

                            if recv_task.done():
                                event = recv_task.result()
                                if event.type == aiohttp.WSMsgType.TEXT:
                                    try:
                                        payload = json.loads(event.data)
                                        self.message_received.emit(payload, "websocket")
                                    except Exception:
                                        pass
                                    recv_task = asyncio.create_task(ws.receive())
                                elif event.type in (aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.ERROR):
                                    break
                            await asyncio.sleep(0.02)
            except Exception as exc:
                self.status_changed.emit(f"Signaling disconnected: {exc}")
                await asyncio.sleep(backoff)
                backoff = min(backoff * 1.6, 10.0)

    def stop(self) -> None:
        self.running = False


# ─────────────────────────────────────────────────────────────────────────────
# WebRTC DataChannel media worker compatible with C++ Local Call v2.0.15+
# ─────────────────────────────────────────────────────────────────────────────


class MediaSettings:
    RESOLUTIONS = {
        "144p": (256, 144),
        "240p": (426, 240),
        "360p": (640, 360),
        "480p": (854, 480),
        "720p": (1280, 720),
        "1080p": (1920, 1080),
        "Source": None,
    }
    FPS_OPTS = [15, 24, 30, 60]


@dataclass
class RtcCommand:
    name: str
    data: Dict[str, Any] = field(default_factory=dict)


class LocalCallFrameCodec:
    """LCM1 chunking used by the C++ RtcPeer DataChannel media transport."""

    MAGIC = b"LCM1"
    VERSION = 1
    HEADER_SIZE = 16
    CHUNK_PAYLOAD_SIZE = 16 * 1024
    MAX_FRAME_BYTES = 2 * 1024 * 1024
    MAX_PENDING_FRAMES = 8

    def __init__(self) -> None:
        self._seq = {b"V": 1, b"A": 1}
        self._pending: Dict[bytes, Dict[int, Dict[str, Any]]] = {b"V": {}, b"A": {}}

    def chunk(self, frame: bytes, tag: bytes) -> List[bytes]:
        if not frame or len(frame) > self.MAX_FRAME_BYTES:
            return []
        count = max(1, (len(frame) + self.CHUNK_PAYLOAD_SIZE - 1) // self.CHUNK_PAYLOAD_SIZE)
        if count > 0xFFFF:
            return []
        frame_id = self._seq[tag] & 0xFFFFFFFF
        self._seq[tag] = (self._seq[tag] + 1) & 0xFFFFFFFF
        packets: List[bytes] = []
        for index in range(count):
            offset = index * self.CHUNK_PAYLOAD_SIZE
            payload = frame[offset : offset + self.CHUNK_PAYLOAD_SIZE]
            header = (
                self.MAGIC
                + tag
                + bytes([self.VERSION])
                + struct.pack("!HHIH", index, count, frame_id, len(payload))
            )
            packets.append(header + payload)
        return packets

    def assemble(self, packet: bytes, expected_tag: bytes) -> Optional[bytes]:
        if len(packet) < self.HEADER_SIZE or packet[:4] != self.MAGIC:
            return packet or None
        tag = packet[4:5]
        version = packet[5]
        if tag != expected_tag or version != self.VERSION:
            return None
        index, count, frame_id, length = struct.unpack("!HHIH", packet[6:16])
        if count <= 0 or index >= count or len(packet) != self.HEADER_SIZE + length:
            return None
        pending = self._pending[tag]
        if len(pending) > self.MAX_PENDING_FRAMES:
            oldest = next(iter(pending.keys()))
            pending.pop(oldest, None)
        item = pending.setdefault(frame_id, {"count": count, "chunks": {}, "total": 0})
        if item["count"] != count:
            pending.pop(frame_id, None)
            return None
        if index not in item["chunks"]:
            item["chunks"][index] = packet[self.HEADER_SIZE :]
            item["total"] += length
        if len(item["chunks"]) != count:
            return None
        if item["total"] <= 0 or item["total"] > self.MAX_FRAME_BYTES:
            pending.pop(frame_id, None)
            return None
        frame = b"".join(item["chunks"].get(i, b"") for i in range(count))
        pending.pop(frame_id, None)
        return frame or None


class RtcCallWorker(QThread):
    """
    WebRTC/ICE/DTLS/SCTP worker using the same low-latency DataChannel media
    protocol as the current C++ Local Call build.

    Channel labels:
      - localcall-video: H.264 Annex-B access units wrapped in LCM1 chunks.
      - localcall-audio: raw Opus packets wrapped in LCM1 chunks.
    """

    VIDEO_LABEL = "localcall-video"
    AUDIO_LABEL = "localcall-audio"

    status_changed = pyqtSignal(str)
    outgoing_signal = pyqtSignal(dict)
    remote_video = pyqtSignal(QImage)
    ended = pyqtSignal()

    def __init__(self, my_id: str, my_name: str, peer_id: str, peer_name: str, ice_config_text: str):
        super().__init__()
        self.my_id = my_id
        self.my_name = my_name
        self.peer_id = peer_id
        self.peer_name = peer_name
        self.ice_config_text = ice_config_text
        self.commands: "queue.Queue[RtcCommand]" = queue.Queue()
        self.running = True
        self.pc = None
        self.mode = "camera"
        self.session_id = str(uuid.uuid4())
        self.video_dc = None
        self.audio_dc = None
        self.video_open = False
        self.audio_open = False
        self.muted = False
        self.target_res: Optional[Tuple[int, int]] = (640, 360)
        self.target_fps = 30
        self.frame_codec = LocalCallFrameCodec()
        self._media_started = False
        self._tasks: List[asyncio.Task] = []
        self._cap = None
        self._sct = None
        self._video_encoder = None
        self._video_decoder = None
        self._audio_encoder = None
        self._audio_decoder = None
        self._audio_p = None
        self._audio_input = None
        self._audio_output = None
        self._audio_pts = 0
        self._video_pts = 0

    def run(self) -> None:
        if not AIORTC_AVAILABLE:
            self.status_changed.emit("aiortc missing: secure RTC calls are unavailable")
            return
        asyncio.run(self._run_async())

    def start_outgoing(self, mode: str, session_id: Optional[str] = None) -> None:
        self.commands.put(RtcCommand("start_outgoing", {"mode": mode, "session_id": session_id or str(uuid.uuid4())}))

    def handle_signal(self, msg: Dict[str, Any]) -> None:
        self.commands.put(RtcCommand("signal", {"msg": msg}))

    def set_quality(self, res_key: str, fps: int) -> None:
        self.commands.put(RtcCommand("quality", {"res_key": res_key, "fps": fps}))

    def set_muted(self, muted: bool) -> None:
        self.commands.put(RtcCommand("mute", {"muted": muted}))

    def hangup(self) -> None:
        self.commands.put(RtcCommand("hangup"))

    async def _run_async(self) -> None:
        loop = asyncio.get_running_loop()
        while self.running:
            cmd = await loop.run_in_executor(None, self.commands.get)
            try:
                if cmd.name == "start_outgoing":
                    await self._start_outgoing(cmd.data.get("mode", "camera"), cmd.data.get("session_id"))
                elif cmd.name == "signal":
                    await self._handle_signal_async(cmd.data["msg"])
                elif cmd.name == "quality":
                    self._apply_quality(cmd.data.get("res_key", "360p"), int(cmd.data.get("fps", 30)))
                elif cmd.name == "mute":
                    self.muted = bool(cmd.data.get("muted"))
                elif cmd.name == "hangup":
                    await self._close()
                    break
            except Exception as exc:
                self.status_changed.emit(f"RTC error: {exc}")
        self.ended.emit()

    def _apply_quality(self, res_key: str, fps: int) -> None:
        self.target_res = MediaSettings.RESOLUTIONS.get(res_key, (640, 360))
        self.target_fps = max(1, min(60, int(fps)))
        self.status_changed.emit(f"Quality set: {res_key} / {self.target_fps} FPS")

    async def _new_pc(self, mode: str, initiator: bool) -> None:
        self.mode = mode or "camera"
        config = RTCConfiguration(iceServers=parse_ice_servers(self.ice_config_text))
        self.pc = RTCPeerConnection(configuration=config)

        @self.pc.on("connectionstatechange")
        async def on_connectionstatechange():
            self.status_changed.emit(f"RTC state: {self.pc.connectionState}")
            if self.pc.connectionState == "connected":
                self._maybe_start_media_tasks()
            elif self.pc.connectionState in {"failed", "closed"}:
                await self._close()

        @self.pc.on("iceconnectionstatechange")
        async def on_iceconnectionstatechange():
            self.status_changed.emit(f"ICE state: {self.pc.iceConnectionState}")

        @self.pc.on("icecandidate")
        async def on_icecandidate(candidate):
            if candidate is None or candidate_to_sdp is None:
                return
            try:
                self.outgoing_signal.emit(
                    {
                        "type": "rtc_ice",
                        "target_id": self.peer_id,
                        "rtc_session_id": self.session_id,
                        "candidate": candidate_to_sdp(candidate),
                        "candidate_mid": getattr(candidate, "sdpMid", None) or "0",
                        "candidate_mline": getattr(candidate, "sdpMLineIndex", 0) or 0,
                        "transport": "webrtc-dtls-srtp",
                    }
                )
            except Exception:
                pass

        @self.pc.on("datachannel")
        def on_datachannel(channel):
            self._configure_data_channel(channel)

        if initiator:
            self.video_dc = self.pc.createDataChannel(self.VIDEO_LABEL, ordered=False, maxRetransmits=0)
            self.audio_dc = self.pc.createDataChannel(self.AUDIO_LABEL, ordered=False, maxRetransmits=0)
            self._configure_data_channel(self.video_dc)
            self._configure_data_channel(self.audio_dc)

    def _configure_data_channel(self, channel) -> None:
        label = getattr(channel, "label", "")
        is_video = label == self.VIDEO_LABEL
        is_audio = label == self.AUDIO_LABEL
        if not (is_video or is_audio):
            self.status_changed.emit(f"Ignoring unsupported DataChannel: {label}")
            return
        if is_video:
            self.video_dc = channel
        else:
            self.audio_dc = channel

        @channel.on("open")
        def on_open():
            if is_video:
                self.video_open = True
            else:
                self.audio_open = True
            self.status_changed.emit(f"Media channel open: {label}")
            self._maybe_start_media_tasks()

        @channel.on("close")
        def on_close():
            if is_video:
                self.video_open = False
            else:
                self.audio_open = False
            self.status_changed.emit(f"Media channel closed: {label}")

        @channel.on("message")
        def on_message(message):
            if isinstance(message, str):
                return
            data = bytes(message)
            if is_video:
                frame = self.frame_codec.assemble(data, b"V")
                if frame:
                    self._decode_remote_video(frame)
            else:
                frame = self.frame_codec.assemble(data, b"A")
                if frame:
                    self._decode_remote_audio(frame)

    def _maybe_start_media_tasks(self) -> None:
        if self._media_started or not self.running:
            return
        if self.video_dc is None or self.audio_dc is None:
            return
        # aiortc may report open before connectionstate reaches connected, and vice versa.
        if getattr(self.pc, "connectionState", "") not in {"connected", "connecting"}:
            return
        self._media_started = True
        loop = asyncio.get_event_loop()
        self._tasks.append(loop.create_task(self._video_send_loop()))
        self._tasks.append(loop.create_task(self._audio_send_loop()))
        self.status_changed.emit("Low-latency C++ compatible media started")

    async def _wait_ice_complete(self, timeout: float = 5.0) -> None:
        deadline = time.monotonic() + timeout
        while getattr(self.pc, "iceGatheringState", None) != "complete" and time.monotonic() < deadline:
            await asyncio.sleep(0.05)

    async def _start_outgoing(self, mode: str, session_id: Optional[str]) -> None:
        self.session_id = session_id or self.session_id
        if self.pc is None:
            await self._new_pc(mode, initiator=True)
        offer = await self.pc.createOffer()
        await self.pc.setLocalDescription(offer)
        await self._wait_ice_complete()
        self.outgoing_signal.emit(
            {
                "type": "rtc_offer",
                "target_id": self.peer_id,
                "rtc_session_id": self.session_id,
                "transport": "webrtc-dtls-srtp",
                "sdp_type": self.pc.localDescription.type,
                "sdp": self.pc.localDescription.sdp,
                "mode": mode,
            }
        )
        self.status_changed.emit("RTC DataChannel offer sent")

    async def _handle_signal_async(self, msg: Dict[str, Any]) -> None:
        mtype = msg.get("type")
        if mtype == "rtc_offer":
            self.session_id = msg.get("rtc_session_id") or self.session_id
            if self.pc is None:
                await self._new_pc(msg.get("mode") or "camera", initiator=False)
            await self.pc.setRemoteDescription(RTCSessionDescription(sdp=msg["sdp"], type=msg.get("sdp_type", "offer")))
            answer = await self.pc.createAnswer()
            await self.pc.setLocalDescription(answer)
            await self._wait_ice_complete()
            self.outgoing_signal.emit(
                {
                    "type": "rtc_answer",
                    "target_id": self.peer_id,
                    "rtc_session_id": self.session_id,
                    "transport": "webrtc-dtls-srtp",
                    "sdp_type": self.pc.localDescription.type,
                    "sdp": self.pc.localDescription.sdp,
                    "mode": msg.get("mode") or self.mode,
                }
            )
            self.status_changed.emit("RTC DataChannel answer sent")
        elif mtype == "rtc_answer":
            if self.pc is not None:
                await self.pc.setRemoteDescription(RTCSessionDescription(sdp=msg["sdp"], type=msg.get("sdp_type", "answer")))
                self.status_changed.emit("RTC answer applied")
        elif mtype == "rtc_ice":
            await self._add_remote_candidate(msg)
        elif mtype == "call_end":
            await self._close()

    async def _add_remote_candidate(self, msg: Dict[str, Any]) -> None:
        if self.pc is None or not msg.get("candidate") or candidate_from_sdp is None:
            return
        try:
            cand_text = str(msg.get("candidate") or "")
            if cand_text.startswith("a="):
                cand_text = cand_text[2:]
            candidate = candidate_from_sdp(cand_text)
            candidate.sdpMid = msg.get("candidate_mid") or "0"
            candidate.sdpMLineIndex = int(msg.get("candidate_mline") or 0)
            await self.pc.addIceCandidate(candidate)
        except Exception as exc:
            self.status_changed.emit(f"ICE candidate ignored: {exc}")

    def _open_video_encoder(self, width: int, height: int, fps: int):
        if not AIORTC_AVAILABLE:
            return None
        for codec_name in ("libx264", "h264"):
            try:
                enc = av.CodecContext.create(codec_name, "w")
                enc.width = width
                enc.height = height
                enc.time_base = Fraction(1, fps)
                enc.framerate = Fraction(fps, 1)
                enc.pix_fmt = "yuv420p"
                enc.bit_rate = 800_000
                enc.options = {
                    "preset": "ultrafast",
                    "tune": "zerolatency",
                    "profile": "baseline",
                    "x264-params": f"keyint={fps * 2}:min-keyint={fps * 2}:scenecut=0",
                }
                enc.open()
                return enc
            except Exception:
                continue
        return None

    def _open_audio_encoder(self):
        try:
            enc = av.CodecContext.create("opus", "w")
            enc.sample_rate = 48000
            enc.layout = "mono"
            enc.format = "s16"
            enc.bit_rate = 32_000
            enc.time_base = Fraction(1, 48000)
            enc.open()
            return enc
        except Exception as exc:
            self.status_changed.emit(f"Opus encoder unavailable: {exc}")
            return None

    def _capture_frame(self) -> Optional[np.ndarray]:
        if self.mode == "screen":
            if not SCREEN_SHARE_AVAILABLE or not OPENCV_AVAILABLE:
                return None
            if self._sct is None:
                self._sct = mss.mss()
            monitor = self._sct.monitors[1]
            img = np.array(self._sct.grab(monitor))
            return cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
        if not OPENCV_AVAILABLE:
            return None
        if self._cap is None:
            self._cap = cv2.VideoCapture(0)
            try:
                self._cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            except Exception:
                pass
        ok, frame = self._cap.read()
        return frame if ok else None

    async def _video_send_loop(self) -> None:
        width, height = self.target_res or (640, 360)
        fps = int(self.target_fps or 30)
        enc = self._open_video_encoder(width, height, fps)
        self._video_encoder = enc
        if enc is None:
            self.status_changed.emit("H.264 encoder unavailable; video send disabled")
            return
        delay = 1.0 / max(1, fps)
        while self.running:
            start = time.monotonic()
            if self.video_dc is not None and getattr(self.video_dc, "readyState", "") == "open":
                frame = self._capture_frame()
                if frame is not None:
                    if self.target_res is not None:
                        frame = cv2.resize(frame, self.target_res)
                    try:
                        vf = av.VideoFrame.from_ndarray(frame, format="bgr24")
                        vf = vf.reformat(width=width, height=height, format="yuv420p")
                        vf.pts = self._video_pts
                        vf.time_base = Fraction(1, fps)
                        self._video_pts += 1
                        for packet in enc.encode(vf):
                            self._send_frame(self.video_dc, bytes(packet), b"V")
                    except Exception as exc:
                        self.status_changed.emit(f"Video encode error: {exc}")
                        await asyncio.sleep(0.5)
            elapsed = time.monotonic() - start
            await asyncio.sleep(max(0.001, delay - elapsed))

    async def _audio_send_loop(self) -> None:
        if not AUDIO_AVAILABLE:
            self.status_changed.emit("PyAudio missing; audio send disabled")
            return
        enc = self._open_audio_encoder()
        self._audio_encoder = enc
        if enc is None:
            return
        try:
            self._audio_p = pyaudio.PyAudio()
            self._audio_input = self._audio_p.open(
                format=pyaudio.paInt16,
                channels=1,
                rate=48000,
                input=True,
                frames_per_buffer=480,
            )
        except Exception as exc:
            self.status_changed.emit(f"Microphone unavailable: {exc}")
            return
        loop = asyncio.get_running_loop()
        while self.running:
            try:
                if self.muted:
                    pcm = b"\x00" * (480 * 2)
                    await asyncio.sleep(0.01)
                else:
                    pcm = await loop.run_in_executor(None, lambda: self._audio_input.read(480, exception_on_overflow=False))
                af = av.AudioFrame(format="s16", layout="mono", samples=480)
                af.planes[0].update(pcm)
                af.sample_rate = 48000
                af.pts = self._audio_pts
                af.time_base = Fraction(1, 48000)
                self._audio_pts += 480
                for packet in enc.encode(af):
                    self._send_frame(self.audio_dc, bytes(packet), b"A")
            except Exception:
                await asyncio.sleep(0.02)

    def _send_frame(self, channel, frame: bytes, tag: bytes) -> None:
        if channel is None or not frame:
            return
        if getattr(channel, "readyState", "") != "open":
            return
        for chunk in self.frame_codec.chunk(frame, tag):
            try:
                channel.send(chunk)
            except Exception:
                break

    def _decode_remote_video(self, data: bytes) -> None:
        try:
            if self._video_decoder is None:
                self._video_decoder = av.CodecContext.create("h264", "r")
            for frame in self._video_decoder.decode(av.Packet(data)):
                rgb = frame.to_ndarray(format="rgb24")
                h, w, ch = rgb.shape
                qimg = QImage(rgb.data, w, h, ch * w, QImage.Format.Format_RGB888).copy()
                self.remote_video.emit(qimg)
        except Exception:
            pass

    def _ensure_audio_output(self) -> bool:
        if not AUDIO_AVAILABLE:
            return False
        if self._audio_output is not None:
            return True
        try:
            if self._audio_p is None:
                self._audio_p = pyaudio.PyAudio()
            self._audio_output = self._audio_p.open(
                format=pyaudio.paInt16,
                channels=1,
                rate=48000,
                output=True,
                frames_per_buffer=480,
            )
            return True
        except Exception:
            return False

    def _decode_remote_audio(self, data: bytes) -> None:
        try:
            if not self._ensure_audio_output():
                return
            if self._audio_decoder is None:
                self._audio_decoder = av.CodecContext.create("opus", "r")
            for frame in self._audio_decoder.decode(av.Packet(data)):
                frame = frame.reformat(format="s16", layout="mono", rate=48000)
                arr = frame.to_ndarray()
                if arr.ndim > 1:
                    arr = arr[0]
                self._audio_output.write(arr.astype(np.int16, copy=False).tobytes())
        except Exception:
            pass

    async def _close(self) -> None:
        if not self.running:
            return
        self.running = False
        for task in list(self._tasks):
            task.cancel()
        self._tasks.clear()
        try:
            if self.video_dc is not None:
                self.video_dc.close()
            if self.audio_dc is not None:
                self.audio_dc.close()
            if self.pc is not None:
                await self.pc.close()
        except Exception:
            pass
        try:
            if self._cap is not None:
                self._cap.release()
            if self._sct is not None:
                self._sct.close()
            if self._audio_input is not None:
                self._audio_input.stop_stream()
                self._audio_input.close()
            if self._audio_output is not None:
                self._audio_output.stop_stream()
                self._audio_output.close()
            if self._audio_p is not None:
                self._audio_p.terminate()
        except Exception:
            pass


# ─────────────────────────────────────────────────────────────────────────────
# GUI
# ─────────────────────────────────────────────────────────────────────────────


class App(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.my_id = str(uuid.uuid4())[:8]
        self.my_name = get_funny_name()
        self.local_ip = local_ip_guess()
        self.identity: Optional[DeviceIdentity] = None
        self.trust = PeerTrustStore()
        self.peers: Dict[str, Dict[str, Any]] = {}
        self.active_peer_id: Optional[str] = None
        self.rtc_worker: Optional[RtcCallWorker] = None
        self.ws_client: Optional[WebSocketSignalingClient] = None
        self.muted = False

        self._init_identity()
        self.init_ui()
        self._start_services()
        self._dependency_warning_if_needed()

    def _init_identity(self) -> None:
        if not NACL_AVAILABLE:
            self.identity = None
            return
        self.identity = DeviceIdentity()

    def _start_services(self) -> None:
        if self.identity is not None:
            self.discovery = PeerDiscovery(self.my_id, self.my_name, self.identity)
            self.discovery.peers_updated.connect(self.on_lan_peers)
            self.discovery.start()
        self.tcp_server = TcpSignalingServer()
        self.tcp_server.message_received.connect(self.on_signal_message)
        self.tcp_server.status_changed.connect(self.set_status)
        self.tcp_server.start()

    def _dependency_warning_if_needed(self) -> None:
        missing = []
        if not AIORTC_AVAILABLE:
            missing.append("aiortc/av")
        if not NACL_AVAILABLE:
            missing.append("PyNaCl")
        if not AIOHTTP_AVAILABLE:
            missing.append("aiohttp")
        if not OPENCV_AVAILABLE:
            missing.append("opencv-python")
        if not AUDIO_AVAILABLE:
            missing.append("PyAudio")
        if missing:
            QTimer.singleShot(
                500,
                lambda: QMessageBox.warning(
                    self,
                    "Missing dependencies",
                    "Some features are unavailable because these packages are missing:\n\n"
                    + "\n".join(f"- {x}" for x in missing)
                    + "\n\nInstall with: pip install -r requirements.txt",
                ),
            )

    def init_ui(self) -> None:
        self.setWindowTitle(f"{APP_NAME} — Secure RTC Python")
        self.resize(1180, 760)
        self.setStyleSheet(
            """
            QMainWindow, QWidget { background-color: #111318; color: #E6EAF2; font-family: Segoe UI, Arial, sans-serif; }
            QLineEdit, QComboBox, QTextEdit, QListWidget { background: #1B1F2A; color: #E6EAF2; border: 1px solid #303849; border-radius: 6px; padding: 6px; }
            QPushButton { background: #2D5BFF; color: white; border: 0; border-radius: 7px; padding: 8px 12px; font-weight: 600; }
            QPushButton:hover { background: #466EFF; }
            QPushButton:disabled { background: #3A4050; color: #888; }
            QLabel#accent { color: #7DD3FC; font-weight: 700; }
            QFrame#panel { background: #171B24; border: 1px solid #2C3444; border-radius: 10px; }
            """
        )

        self.stack = QStackedWidget()
        self.setCentralWidget(self.stack)
        self.stack.addWidget(self._build_lobby())
        self.stack.addWidget(self._build_call_room())

    def _build_lobby(self) -> QWidget:
        root = QWidget()
        layout = QVBoxLayout(root)

        header = QFrame(objectName="panel")
        h = QGridLayout(header)
        self.lbl_identity = QLabel(self._identity_text())
        self.lbl_identity.setObjectName("accent")
        btn_name = QPushButton("Edit Profile")
        btn_name.clicked.connect(self.change_name)
        h.addWidget(self.lbl_identity, 0, 0, 1, 5)
        h.addWidget(btn_name, 0, 5)

        self.txt_signal_url = QLineEdit(os.environ.get("LOCALCALL_SIGNALING_URL", DEFAULT_SIGNALING_URL))
        self.txt_room = QLineEdit(os.environ.get("LOCALCALL_ROOM", "default"))
        self.txt_ice = QLineEdit(os.environ.get("LOCALCALL_ICE_SERVERS", DEFAULT_ICE_SERVERS))
        self.btn_connect_ws = QPushButton("Connect Internet Signaling")
        self.btn_connect_ws.clicked.connect(self.toggle_ws_signaling)

        h.addWidget(QLabel("Signaling URL"), 1, 0)
        h.addWidget(self.txt_signal_url, 1, 1, 1, 2)
        h.addWidget(QLabel("Room"), 1, 3)
        h.addWidget(self.txt_room, 1, 4)
        h.addWidget(self.btn_connect_ws, 1, 5)
        h.addWidget(QLabel("ICE/STUN/TURN"), 2, 0)
        h.addWidget(self.txt_ice, 2, 1, 1, 5)
        layout.addWidget(header)

        split = QSplitter(Qt.Orientation.Horizontal)
        left = QFrame(objectName="panel")
        l = QVBoxLayout(left)
        l.addWidget(QLabel("ONLINE PEERS — LAN or signaling room"))
        self.peer_list = QListWidget()
        self.peer_list.itemClicked.connect(self.select_peer)
        l.addWidget(self.peer_list)

        right = QFrame(objectName="panel")
        r = QVBoxLayout(right)
        self.lbl_peer_title = QLabel("Select a peer")
        self.lbl_peer_title.setObjectName("accent")
        r.addWidget(self.lbl_peer_title)
        self.btn_video = QPushButton("Start Secure Video Call")
        self.btn_screen = QPushButton("Start Secure Screen Share")
        self.btn_video.clicked.connect(lambda: self.start_call("camera"))
        self.btn_screen.clicked.connect(lambda: self.start_call("screen"))
        self.btn_video.setEnabled(False)
        self.btn_screen.setEnabled(False)
        r.addWidget(self.btn_video)
        r.addWidget(self.btn_screen)
        self.log = QTextEdit()
        self.log.setReadOnly(True)
        r.addWidget(QLabel("Event log"))
        r.addWidget(self.log, 1)

        split.addWidget(left)
        split.addWidget(right)
        split.setStretchFactor(0, 2)
        split.setStretchFactor(1, 3)
        layout.addWidget(split, 1)

        self.lbl_status = QLabel("Ready")
        layout.addWidget(self.lbl_status)
        return root

    def _build_call_room(self) -> QWidget:
        root = QWidget()
        layout = QVBoxLayout(root)
        self.video_view = QLabel("Secure RTC call initializing…")
        self.video_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.video_view.setStyleSheet("background:#000; border-radius: 10px; color:#9CA3AF; font-size:18px;")
        layout.addWidget(self.video_view, 1)

        controls = QFrame(objectName="panel")
        g = QGridLayout(controls)
        self.combo_res = QComboBox()
        self.combo_res.addItems(MediaSettings.RESOLUTIONS.keys())
        self.combo_res.setCurrentText("360p")
        self.combo_fps = QComboBox()
        self.combo_fps.addItems([str(x) for x in MediaSettings.FPS_OPTS])
        self.combo_fps.setCurrentText("30")
        self.btn_mute = QPushButton("Mute")
        self.btn_hangup = QPushButton("End Call")
        self.btn_hangup.setStyleSheet("background:#DC2626;color:white;border-radius:7px;padding:8px 12px;font-weight:700;")
        self.btn_mute.clicked.connect(self.toggle_mute)
        self.btn_hangup.clicked.connect(self.end_call)
        self.combo_res.currentTextChanged.connect(self.apply_quality)
        self.combo_fps.currentTextChanged.connect(self.apply_quality)
        g.addWidget(QLabel("Resolution"), 0, 0)
        g.addWidget(self.combo_res, 0, 1)
        g.addWidget(QLabel("FPS"), 0, 2)
        g.addWidget(self.combo_fps, 0, 3)
        g.addWidget(self.btn_mute, 0, 4)
        g.addWidget(self.btn_hangup, 0, 5)
        layout.addWidget(controls)
        return root

    def _identity_text(self) -> str:
        fp = self.identity.fingerprint if self.identity else "NO-SIGNING"
        return f"{self.my_name} | ID {self.my_id} | {self.local_ip} | fingerprint {compact_fingerprint(fp)}"

    def set_status(self, text: str) -> None:
        self.lbl_status.setText(text)
        self.log.append(f"[{time.strftime('%H:%M:%S')}] {text}")

    def change_name(self) -> None:
        name, ok = QInputDialog.getText(self, "Profile", "Enter display name:", text=self.my_name)
        if ok and name.strip():
            self.my_name = name.strip()
            self.lbl_identity.setText(self._identity_text())
            if hasattr(self, "discovery"):
                self.discovery.update_name(self.my_name)

    def on_lan_peers(self, peers: Dict[str, Dict[str, Any]]) -> None:
        for pid, info in peers.items():
            self.peers[pid] = info
            pub = info.get("auth_public_key")
            fp = info.get("auth_fingerprint")
            if pub and fp:
                self.trust.learn_or_verify(pid, info.get("name", pid), pub, fp)
        self.refresh_peer_list()

    def refresh_peer_list(self) -> None:
        selected = self.active_peer_id
        self.peer_list.clear()
        for pid, info in sorted(self.peers.items(), key=lambda x: (x[1].get("source", ""), x[1].get("name", ""))):
            fp = info.get("auth_fingerprint") or "unknown"
            source = info.get("source", "?")
            text = f"Online {info.get('name', pid)}  [{source}]  {info.get('ip', '')}  fp:{compact_fingerprint(fp)}"
            item = QListWidgetItem(text)
            item.setData(Qt.ItemDataRole.UserRole, pid)
            self.peer_list.addItem(item)
            if pid == selected:
                self.peer_list.setCurrentItem(item)

    def select_peer(self, item: QListWidgetItem) -> None:
        pid = item.data(Qt.ItemDataRole.UserRole)
        self.active_peer_id = pid
        info = self.peers.get(pid, {})
        self.lbl_peer_title.setText(f"Peer: {info.get('name', pid)}")
        self.btn_video.setEnabled(True)
        self.btn_screen.setEnabled(SCREEN_SHARE_AVAILABLE)

    def toggle_ws_signaling(self) -> None:
        if self.ws_client and self.ws_client.isRunning():
            self.ws_client.stop()
            self.ws_client = None
            self.btn_connect_ws.setText("Connect Internet Signaling")
            self.set_status("Internet signaling disconnected")
            return
        if self.identity is None:
            QMessageBox.critical(self, "Signing unavailable", "PyNaCl is required for secure internet signaling.")
            return
        self.ws_client = WebSocketSignalingClient(
            self.txt_signal_url.text(), self.txt_room.text(), self.my_id, self.my_name, self.identity
        )
        self.ws_client.message_received.connect(self.on_signal_message)
        self.ws_client.status_changed.connect(self.set_status)
        self.ws_client.start()
        self.btn_connect_ws.setText("Disconnect Signaling")

    def make_signed(self, msg_type: str, extra: Dict[str, Any]) -> Dict[str, Any]:
        if self.identity is None:
            raise RuntimeError("signed signaling unavailable")
        msg = self.identity.base_envelope(msg_type, self.my_id, self.my_name)
        msg.update(extra)
        return self.identity.sign(msg)

    def dispatch_signal(self, msg: Dict[str, Any], peer_id: Optional[str] = None) -> None:
        target_id = peer_id or msg.get("target_id") or self.active_peer_id
        if target_id:
            msg["target_id"] = target_id
        signed = self.identity.sign(msg) if self.identity and msg.get("type") in CRITICAL_SIGNED_TYPES else msg

        peer = self.peers.get(target_id or "", {})
        sent = False
        if peer.get("source") == "lan" and peer.get("ip"):
            try:
                send_framed_json(peer["ip"], signed)
                sent = True
            except Exception as exc:
                self.set_status(f"LAN signal failed: {exc}")
        if self.ws_client and self.ws_client.isRunning():
            self.ws_client.send(signed)
            sent = True
        if not sent:
            self.set_status("No signaling route available for selected peer")

    def on_signal_message(self, msg: Dict[str, Any], source: str) -> None:
        sender = msg.get("from_id")
        if not sender or sender == self.my_id:
            return
        target = msg.get("target_id")
        if target not in (None, "", "*", self.my_id):
            return

        name = msg.get("from_name") or sender
        pub = msg.get("auth_public_key")
        fp = msg.get("auth_fingerprint")
        if pub and fp:
            ok, reason = self.trust.learn_or_verify(sender, name, pub, fp)
            if not ok:
                self.set_status(f"Blocked {name}: {reason}")
                return

        if msg.get("type") in CRITICAL_SIGNED_TYPES:
            expected = self.trust.expected_key(sender)
            ok, reason = DeviceIdentity.verify(msg, expected) if NACL_AVAILABLE else (False, "PyNaCl missing")
            if not ok:
                self.set_status(f"Blocked unauthenticated {msg.get('type')} from {name}: {reason}")
                return

        self.peers[sender] = {
            "id": sender,
            "name": name,
            "ip": source if source != "websocket" else "",
            "source": "websocket" if source == "websocket" else "lan",
            "ts": time.time(),
            "auth_public_key": pub,
            "auth_fingerprint": fp,
            "rtc_supported": True,
        }
        self.refresh_peer_list()

        mtype = msg.get("type")
        if mtype == "hello":
            return
        if mtype == "friend_req":
            self.on_friend_request(msg)
        elif mtype == "friend_acc":
            self.set_status(f"Friend accepted by {name}")
        elif mtype == "call_inv":
            self.on_call_invite(msg)
        elif mtype == "call_acc":
            self.on_call_accept(msg)
        elif mtype in {"rtc_offer", "rtc_answer", "rtc_ice"}:
            self.on_rtc_signal(msg)
        elif mtype == "call_end":
            self.set_status(f"Call ended by {name}")
            self.end_call(local_only=True)

    def on_friend_request(self, msg: Dict[str, Any]) -> None:
        peer_id = msg.get("from_id")
        peer_name = msg.get("from_name") or peer_id
        if not peer_id:
            return
        accepted = QMessageBox.question(
            self,
            "Friend request",
            f"{peer_name} wants to connect with this Python client. Accept?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        reply_type = "friend_acc" if accepted == QMessageBox.StandardButton.Yes else "friend_rej"
        reply = self.identity.base_envelope(reply_type, self.my_id, self.my_name) | {"target_id": peer_id}
        self.dispatch_signal(reply, peer_id)
        if reply_type == "friend_acc":
            self.set_status(f"Accepted friend request from {peer_name}")

    def on_call_accept(self, msg: Dict[str, Any]) -> None:
        peer_id = msg.get("from_id")
        peer_name = msg.get("from_name") or peer_id
        if not peer_id:
            return
        pending = getattr(self, "pending_outgoing_call", None) or {}
        session_id = msg.get("rtc_session_id") or pending.get("session_id") or str(uuid.uuid4())
        mode = msg.get("mode") or pending.get("mode") or "camera"
        self.ensure_rtc_worker(peer_id, peer_name)
        self.rtc_worker.start_outgoing(mode, session_id)  # type: ignore[union-attr]
        self.stack.setCurrentIndex(1)
        self.video_view.setText("Secure RTC accepted — negotiating DataChannels…")
        self.set_status(f"Call accepted by {peer_name}; sending C++ compatible offer")

    def on_call_invite(self, msg: Dict[str, Any]) -> None:
        peer_id = msg.get("from_id")
        peer_name = msg.get("from_name") or peer_id
        mode = msg.get("mode") or "camera"
        self.active_peer_id = peer_id
        accepted = QMessageBox.question(
            self,
            "Incoming secure call",
            f"{peer_name} wants to start a secure {mode} call.\n\nAccept?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if accepted == QMessageBox.StandardButton.Yes:
            self.dispatch_signal(
                self.identity.base_envelope("call_acc", self.my_id, self.my_name)
                | {
                    "target_id": peer_id,
                    "mode": mode,
                    "rtc_session_id": msg.get("rtc_session_id"),
                    "transport": "webrtc-dtls-srtp",
                },
                peer_id,
            )
            self.ensure_rtc_worker(peer_id, peer_name)
            self.stack.setCurrentIndex(1)
            self.set_status(f"Accepted secure call from {peer_name}")
        else:
            self.dispatch_signal(
                self.identity.base_envelope("call_rej", self.my_id, self.my_name)
                | {"target_id": peer_id, "rtc_session_id": msg.get("rtc_session_id")},
                peer_id,
            )

    def ensure_rtc_worker(self, peer_id: str, peer_name: str) -> None:
        if self.rtc_worker and self.rtc_worker.isRunning():
            return
        self.rtc_worker = RtcCallWorker(self.my_id, self.my_name, peer_id, peer_name, self.txt_ice.text())
        self.rtc_worker.status_changed.connect(self.set_status)
        self.rtc_worker.outgoing_signal.connect(self.on_rtc_outgoing_signal)
        self.rtc_worker.remote_video.connect(self.draw_video)
        self.rtc_worker.ended.connect(lambda: self.set_status("RTC worker stopped"))
        self.rtc_worker.start()

    def start_call(self, mode: str) -> None:
        if not self.active_peer_id:
            return
        if not AIORTC_AVAILABLE:
            QMessageBox.critical(self, "RTC unavailable", "Install aiortc, av, PyNaCl, and media dependencies using requirements.txt first.")
            return
        peer = self.peers.get(self.active_peer_id, {})
        peer_name = peer.get("name", self.active_peer_id)
        session_id = str(uuid.uuid4())
        self.ensure_rtc_worker(self.active_peer_id, peer_name)
        # C++ Local Call expects call_inv.mode to be "video" or "voice".
        # Keep the Python capture mode separately so screen/camera still work locally.
        cpp_mode = "video" if mode in {"camera", "screen", "video"} else "voice"
        local_mode = "screen" if mode == "screen" else "camera"
        invite = self.identity.base_envelope("call_inv", self.my_id, self.my_name) | {
            "target_id": self.active_peer_id,
            "mode": cpp_mode,
            "rtc_session_id": session_id,
            "transport": "webrtc-dtls-srtp",
        }
        self.pending_outgoing_call = {"peer_id": self.active_peer_id, "mode": local_mode, "session_id": session_id}
        self.dispatch_signal(invite, self.active_peer_id)
        self.video_view.setText("Calling… waiting for peer to accept")
        self.stack.setCurrentIndex(1)

    def on_rtc_signal(self, msg: Dict[str, Any]) -> None:
        peer_id = msg.get("from_id")
        peer_name = msg.get("from_name") or peer_id
        self.ensure_rtc_worker(peer_id, peer_name)
        self.rtc_worker.handle_signal(msg)  # type: ignore[union-attr]
        self.stack.setCurrentIndex(1)

    def on_rtc_outgoing_signal(self, msg: Dict[str, Any]) -> None:
        base = self.identity.base_envelope(msg["type"], self.my_id, self.my_name)
        base.update(msg)
        self.dispatch_signal(base, msg.get("target_id"))

    def draw_video(self, qimg: QImage) -> None:
        pix = QPixmap.fromImage(qimg)
        self.video_view.setPixmap(
            pix.scaled(self.video_view.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
        )

    def apply_quality(self) -> None:
        if self.rtc_worker:
            try:
                self.rtc_worker.set_quality(self.combo_res.currentText(), int(self.combo_fps.currentText()))
            except Exception:
                pass

    def toggle_mute(self) -> None:
        self.muted = not self.muted
        if self.rtc_worker:
            self.rtc_worker.set_muted(self.muted)
        self.btn_mute.setText("Unmute" if self.muted else "Mute")

    def end_call(self, local_only: bool = False) -> None:
        if self.rtc_worker:
            self.rtc_worker.hangup()
            self.rtc_worker = None
        if not local_only and self.active_peer_id and self.identity:
            msg = self.identity.base_envelope("call_end", self.my_id, self.my_name) | {"target_id": self.active_peer_id}
            self.dispatch_signal(msg, self.active_peer_id)
        self.stack.setCurrentIndex(0)
        self.video_view.setText("Call ended")

    def closeEvent(self, event) -> None:
        try:
            if hasattr(self, "discovery"):
                self.discovery.stop()
            if hasattr(self, "tcp_server"):
                self.tcp_server.stop()
            if self.ws_client:
                self.ws_client.stop()
            if self.rtc_worker:
                self.rtc_worker.hangup()
        finally:
            super().closeEvent(event)


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────


def main() -> int:
    app = QApplication(sys.argv)
    window = App()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
