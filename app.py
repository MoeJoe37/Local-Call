#app
import sys
import socket
import threading
import json
import time
import struct
import uuid
import random
import numpy as np
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, 
    QLabel, QLineEdit, QPushButton, QTextEdit, QListWidget, 
    QListWidgetItem, QStackedWidget, QComboBox, QFrame, QGridLayout,
    QInputDialog, QMessageBox, QSplitter
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal, QSize, QTimer
from PyQt6.QtGui import QImage, QPixmap, QFont, QIcon, QColor

# --- Dependencies Check ---
try:
    import cv2
    OPENCV_AVAILABLE = True
except ImportError:
    OPENCV_AVAILABLE = False

try:
    import pyaudio
    AUDIO_AVAILABLE = True
except ImportError:
    AUDIO_AVAILABLE = False

try:
    import mss
    SCREEN_SHARE_AVAILABLE = True
except ImportError:
    SCREEN_SHARE_AVAILABLE = False

# --- Constants ---
BROADCAST_PORT = 50005
MEDIA_PORT_START = 50100
BUFFER_SIZE = 65536
BROADCAST_INTERVAL = 2
PEER_TIMEOUT = 8

def get_funny_name():
    adjs = ["Silly", "Brave", "Goofy", "Turbo", "Fancy", "Sleepy", "Hyper", "Invisible"]
    nouns = ["Hamster", "Potato", "Ninja", "Wizard", "Toaster", "Unicorn", "Panda", "Cactus"]
    return f"{random.choice(adjs)} {random.choice(nouns)}"

class MediaSettings:
    RESOLUTIONS = {
        "144p": (256, 144),
        "240p": (426, 240),
        "360p": (640, 360),
        "480p": (854, 480),
        "720p": (1280, 720),
        "1080p": (1920, 1080),
        "Source": None
    }
    FPS_OPTS = [30, 60, 90, 120, "Source"]

class MediaWorker(QThread):
    change_pixmap_signal = pyqtSignal(QImage)
    
    def __init__(self, mode="camera", target_ip=None, port=None, is_receiver=False):
        super().__init__()
        self.mode = mode
        self.target_ip = target_ip
        self.port = port
        self.is_receiver = is_receiver
        self.running = True
        self.muted = False
        self.target_res = (640, 360)
        self.target_fps = 30

    def run(self):
        if self.is_receiver: self.run_receiver()
        else: self.run_sender()

    def run_sender(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        
        if self.mode == "audio" and AUDIO_AVAILABLE:
            p = pyaudio.PyAudio()
            stream = p.open(format=pyaudio.paInt16, channels=1, rate=44100,
                            input=True, frames_per_buffer=1024)
            while self.running:
                try:
                    data = stream.read(1024, exception_on_overflow=False)
                    if self.muted: data = b'\x00' * len(data)
                    sock.sendto(data, (self.target_ip, self.port))
                except: break
            stream.stop_stream(); stream.close(); p.terminate()

        elif self.mode in ["camera", "screen"]:
            cap = None
            sct = None
            if self.mode == "camera" and OPENCV_AVAILABLE:
                cap = cv2.VideoCapture(0)
            elif self.mode == "screen" and SCREEN_SHARE_AVAILABLE:
                sct = mss.mss()

            while self.running:
                start_time = time.time()
                frame = None
                if self.mode == "camera" and cap:
                    ret, frame = cap.read()
                elif self.mode == "screen" and sct:
                    monitor = sct.monitors[1]
                    sct_img = sct.grab(monitor)
                    frame = np.array(sct_img)
                    frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)

                if frame is not None:
                    if self.target_res:
                        frame = cv2.resize(frame, self.target_res)
                    _, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 60])
                    data = buffer.tobytes()
                    max_chunk = 60000
                    for i in range(0, len(data), max_chunk):
                        chunk = data[i:i+max_chunk]
                        flag = 1 if i + max_chunk >= len(data) else 0
                        header = struct.pack("?I", flag, len(chunk))
                        sock.sendto(header + chunk, (self.target_ip, self.port))
                
                if isinstance(self.target_fps, int):
                    elapsed = time.time() - start_time
                    delay = max(0, (1.0 / self.target_fps) - elapsed)
                    time.sleep(delay)

            if cap: cap.release()
            if sct: sct.close()
        sock.close()

    def run_receiver(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try: sock.bind(('0.0.0.0', self.port))
        except: return

        if self.mode == "audio":
            p = pyaudio.PyAudio()
            stream = p.open(format=pyaudio.paInt16, channels=1, rate=44100, output=True)
            while self.running:
                try:
                    data, _ = sock.recvfrom(BUFFER_SIZE)
                    if data: stream.write(data)
                except: break
            stream.stop_stream(); stream.close(); p.terminate()
        else:
            frame_data = b""
            while self.running:
                try:
                    packet, _ = sock.recvfrom(BUFFER_SIZE)
                    header_size = struct.calcsize("?I")
                    flag, size = struct.unpack("?I", packet[:header_size])
                    frame_data += packet[header_size:]
                    if flag:
                        nparr = np.frombuffer(frame_data, np.uint8)
                        frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
                        if frame is not None:
                            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                            h, w, ch = rgb.shape
                            qimg = QImage(rgb.data, w, h, ch * w, QImage.Format.Format_RGB888)
                            self.change_pixmap_signal.emit(qimg)
                        frame_data = b""
                except: break
        sock.close()

    def stop(self):
        self.running = False
        self.wait()

class PeerDiscovery(QThread):
    peers_updated = pyqtSignal(dict)
    def __init__(self, my_id, my_name):
        super().__init__()
        self.my_id, self.my_name = my_id, my_name
        self.peers = {}
        self.running = True

    def run(self):
        threading.Thread(target=self.broadcast, daemon=True).start()
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind(('', BROADCAST_PORT))
        sock.settimeout(1.0)
        while self.running:
            try:
                data, addr = sock.recvfrom(1024)
                msg = json.loads(data.decode())
                if msg['id'] != self.my_id:
                    self.peers[msg['id']] = {'name': msg['name'], 'ip': addr[0], 'ts': time.time()}
            except: pass
            now = time.time()
            self.peers = {id: info for id, info in self.peers.items() if now - info['ts'] < PEER_TIMEOUT}
            self.peers_updated.emit(self.peers)
        sock.close()

    def broadcast(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        while self.running:
            try:
                data = json.dumps({'id': self.my_id, 'name': self.my_name}).encode()
                sock.sendto(data, ('<broadcast>', BROADCAST_PORT))
            except: pass
            time.sleep(BROADCAST_INTERVAL)
        sock.close()

    def update_name(self, name):
        self.my_name = name

    def stop(self): self.running = False

class App(QMainWindow):
    def __init__(self):
        super().__init__()
        self.my_id = str(uuid.uuid4())[:6]
        self.my_name = get_funny_name()
        
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            self.local_ip = s.getsockname()[0]
            s.close()
        except:
            self.local_ip = "127.0.0.1"

        self.workers = []
        self.active_peer_ip = None
        self.init_ui()
        
        self.discovery = PeerDiscovery(self.my_id, self.my_name)
        self.discovery.peers_updated.connect(self.refresh_peers)
        self.discovery.start()

    def init_ui(self):
        self.setWindowTitle("Local Call Pro")
        self.resize(1100, 700)
        self.setStyleSheet("background-color: #121212; color: #E0E0E0; font-family: 'Segoe UI', Sans-Serif;")

        self.stack = QStackedWidget()
        self.setCentralWidget(self.stack)

        # PAGE 1: LOBBY
        lobby = QWidget()
        l_layout = QVBoxLayout(lobby)
        
        top_bar = QHBoxLayout()
        self.lbl_welcome = QLabel(f"Hello, {self.my_name} ({self.local_ip})")
        self.lbl_welcome.setStyleSheet("font-size: 16px; color: #BB86FC;")
        btn_change_name = QPushButton("Edit Profile")
        btn_change_name.setFixedSize(120, 30)
        btn_change_name.setStyleSheet("background: #333; border-radius: 4px;")
        btn_change_name.clicked.connect(self.change_name)
        top_bar.addWidget(self.lbl_welcome)
        top_bar.addStretch()
        top_bar.addWidget(btn_change_name)
        l_layout.addLayout(top_bar)

        self.lobby_splitter = QSplitter(Qt.Orientation.Horizontal)
        
        peer_cont = QWidget()
        pc_layout = QVBoxLayout(peer_cont)
        pc_layout.addWidget(QLabel("ONLINE PEERS (Select to Invite)"))
        self.peer_list = QListWidget()
        self.peer_list.setStyleSheet("background: #1E1E1E; border: 1px solid #333; font-size: 15px;")
        self.peer_list.itemClicked.connect(self.show_invitation_panel)
        pc_layout.addWidget(self.peer_list)
        
        self.peer_panel = QFrame()
        self.peer_panel.setStyleSheet("background: #181818; border-left: 2px solid #333;")
        self.pp_layout = QVBoxLayout(self.peer_panel)
        self.pp_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.peer_panel.setVisible(False)
        
        self.lbl_peer_title = QLabel("Peer Details")
        self.lbl_peer_title.setStyleSheet("font-size: 20px; font-weight: bold; color: #03DAC6; margin-bottom: 20px;")
        self.pp_layout.addWidget(self.lbl_peer_title)
        
        self.btn_send_chat = QPushButton("💬 Chat & Text")
        self.btn_start_call = QPushButton("📞 Voice/Video Call")
        self.btn_share_screen = QPushButton("🖥 Share Screen")
        self.btn_send_media = QPushButton("📂 Send Files/Media")
        
        for b in [self.btn_send_chat, self.btn_start_call, self.btn_share_screen, self.btn_send_media]:
            b.setFixedHeight(50)
            b.setStyleSheet("background: #252525; border: 1px solid #444; text-align: left; padding-left: 15px;")
            self.pp_layout.addWidget(b)

        self.btn_start_call.clicked.connect(lambda: self.setup_session("camera"))
        self.btn_share_screen.clicked.connect(lambda: self.setup_session("screen"))
        
        self.lobby_splitter.addWidget(peer_cont)
        self.lobby_splitter.addWidget(self.peer_panel)
        self.lobby_splitter.setStretchFactor(0, 1)
        self.lobby_splitter.setStretchFactor(1, 1)
        
        l_layout.addWidget(self.lobby_splitter)
        self.stack.addWidget(lobby)

        # PAGE 2: CALL ROOM
        call_room = QWidget()
        c_layout = QVBoxLayout(call_room)
        self.video_view = QLabel("Connecting...")
        self.video_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.video_view.setStyleSheet("background: #000; border-radius: 10px;")
        c_layout.addWidget(self.video_view, 1)

        controls = QFrame()
        controls.setFixedHeight(120)
        controls.setStyleSheet("background: #1F1F1F; border-top: 2px solid #333; padding: 10px;")
        ctrl_layout = QGridLayout(controls)

        self.btn_mute = QPushButton("🎤 Mute")
        self.btn_hangup = QPushButton("🛑 End Call")
        self.combo_res = QComboBox(); self.combo_res.addItems(MediaSettings.RESOLUTIONS.keys())
        self.combo_fps = QComboBox(); self.combo_fps.addItems([str(x) for x in MediaSettings.FPS_OPTS])
        
        ctrl_layout.addWidget(QLabel("Quality:"), 0, 0); ctrl_layout.addWidget(self.combo_res, 0, 1)
        ctrl_layout.addWidget(QLabel("FPS:"), 1, 0); ctrl_layout.addWidget(self.combo_fps, 1, 1)
        ctrl_layout.addWidget(self.btn_mute, 0, 2, 2, 1)
        ctrl_layout.addWidget(self.btn_hangup, 0, 3, 2, 1)

        self.btn_mute.clicked.connect(self.toggle_mute)
        self.btn_hangup.clicked.connect(self.end_session)
        self.combo_res.currentTextChanged.connect(self.update_quality)
        self.combo_fps.currentTextChanged.connect(self.update_quality)
        
        c_layout.addWidget(controls)
        self.stack.addWidget(call_room)

    def change_name(self):
        name, ok = QInputDialog.getText(self, "Profile", "Enter New Name:", text=self.my_name)
        if ok and name:
            self.my_name = name
            self.lbl_welcome.setText(f"Hello, {self.my_name} ({self.local_ip})")
            if hasattr(self, 'discovery'):
                self.discovery.update_name(name)

    def refresh_peers(self, peers):
        self.peer_list.clear()
        for id, info in peers.items():
            label = f"🟢 {info['name']} ({info['ip']})"
            item = QListWidgetItem(label)
            item.setData(Qt.ItemDataRole.UserRole, info['ip'])
            item.setData(Qt.ItemDataRole.DisplayRole, info['name'])
            self.peer_list.addItem(item)

    def show_invitation_panel(self, item):
        self.active_peer_ip = item.data(Qt.ItemDataRole.UserRole)
        peer_name = item.data(Qt.ItemDataRole.DisplayRole)
        self.lbl_peer_title.setText(f"Interacting with {peer_name}")
        self.peer_panel.setVisible(True)

    def setup_session(self, mode):
        if not self.active_peer_ip: return
        self.stack.setCurrentIndex(1)
        
        v_send = MediaWorker(mode=mode, target_ip=self.active_peer_ip, port=MEDIA_PORT_START)
        v_recv = MediaWorker(mode=mode, port=MEDIA_PORT_START, is_receiver=True)
        a_send = MediaWorker(mode="audio", target_ip=self.active_peer_ip, port=MEDIA_PORT_START+5)
        a_recv = MediaWorker(mode="audio", port=MEDIA_PORT_START+5, is_receiver=True)
        
        v_recv.change_pixmap_signal.connect(self.draw_video)
        self.workers = [v_send, v_recv, a_send, a_recv]
        self.audio_sender = a_send
        self.video_sender = v_send
        
        for w in self.workers: w.start()
        self.update_quality()

    def draw_video(self, qimg):
        pix = QPixmap.fromImage(qimg)
        self.video_view.setPixmap(pix.scaled(self.video_view.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))

    def update_quality(self):
        if hasattr(self, 'video_sender'):
            res_key = self.combo_res.currentText()
            fps_val = self.combo_fps.currentText()
            self.video_sender.target_res = MediaSettings.RESOLUTIONS[res_key]
            self.video_sender.target_fps = int(fps_val) if fps_val.isdigit() else fps_val

    def toggle_mute(self):
        if hasattr(self, 'audio_sender'):
            self.audio_sender.muted = not self.audio_sender.muted
            self.btn_mute.setText("🎤 Unmute" if self.audio_sender.muted else "🎤 Mute")

    def end_session(self):
        for w in self.workers: w.stop()
        self.workers = []
        self.stack.setCurrentIndex(0)
        self.video_view.setText("Call Ended")

    def closeEvent(self, e):
        self.discovery.stop()
        self.end_session()
        super().closeEvent(e)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    ex = App()
    ex.show()
    sys.exit(app.exec())