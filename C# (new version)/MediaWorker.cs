using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using NAudio.Wave;
using OpenCvSharp;

namespace LocalCallPro;

public enum MediaMode { Camera, Screen, Audio }

public class MediaWorker
{
    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int nIndex);
    private static (int W, int H) PrimaryScreenSize() => (GetSystemMetrics(0), GetSystemMetrics(1));

    private readonly MediaMode _mode;
    private readonly string?   _targetIp;
    private readonly int       _port;
    private readonly bool      _isReceiver;
    private Thread?            _thread;
    private volatile bool      _running;

    public bool            Muted        { get; set; }
    public (int W, int H)? TargetRes    { get; set; } = (640, 360);
    public int             TargetFps    { get; set; } = 30;
    public bool            UseSourceFps { get; set; }
    public bool            MuteAudioOnScreen { get; set; }  // screen share: no mic

    /// <summary>Video frame received (frozen BitmapSource, cross-thread safe).</summary>
    public event Action<BitmapSource>? FrameReceived;
    /// <summary>Fires once when the first data packet arrives (audio or video).</summary>
    public event Action? Connected;

    public MediaWorker(MediaMode mode, string? targetIp, int port, bool isReceiver = false)
    {
        _mode       = mode;
        _targetIp   = targetIp;
        _port       = port;
        _isReceiver = isReceiver;
    }

    public void Start()
    {
        _running = true;
        _thread  = new Thread(_isReceiver ? RunReceiver : RunSender)
        {
            IsBackground = true,
            Name         = $"{_mode}{(_isReceiver ? "Recv" : "Send")}"
        };
        _thread.Start();
    }

    public void Stop()
    {
        _running = false;
        _thread?.Join(3000);
    }

    // ── Sender ───────────────────────────────────────────────────────────────

    private void RunSender()
    {
        using var sock = new UdpClient();
        var ep = new IPEndPoint(IPAddress.Parse(_targetIp!), _port);
        switch (_mode)
        {
            case MediaMode.Audio:  RunAudioSender(sock, ep);  break;
            case MediaMode.Camera: RunCameraSender(sock, ep); break;
            case MediaMode.Screen: RunScreenSender(sock, ep); break;
        }
    }

    private void RunAudioSender(UdpClient sock, IPEndPoint ep)
    {
        WaveInEvent? waveIn = null;
        try
        {
            waveIn = new WaveInEvent { WaveFormat = new WaveFormat(44100, 16, 1) };
            waveIn.DataAvailable += (_, e) =>
            {
                if (!_running) return;
                if (MuteAudioOnScreen || Muted)
                {
                    // Send silence so the receiver knows we're still connected
                    var silence = new byte[e.BytesRecorded];
                    try { sock.Send(silence, silence.Length, ep); } catch { }
                }
                else
                {
                    var buf = new byte[e.BytesRecorded];
                    Buffer.BlockCopy(e.Buffer, 0, buf, 0, e.BytesRecorded);
                    try { sock.Send(buf, buf.Length, ep); } catch { }
                }
            };
            waveIn.StartRecording();
            while (_running) Thread.Sleep(100);
            waveIn.StopRecording();
        }
        catch { /* microphone unavailable */ }
        finally { waveIn?.Dispose(); }
    }

    private void RunCameraSender(UdpClient sock, IPEndPoint ep)
    {
        VideoCapture? cap = null;
        try
        {
            cap = new VideoCapture(0);
            if (!cap.IsOpened()) return;
            using var frame = new Mat();
            while (_running)
            {
                var start = DateTime.UtcNow;
                if (!cap.Read(frame) || frame.Empty()) { Thread.Sleep(10); continue; }
                SendVideoFrame(sock, ep, frame);
                ThrottleFps(start);
            }
        }
        catch { }
        finally { cap?.Release(); cap?.Dispose(); }
    }

    private void RunScreenSender(UdpClient sock, IPEndPoint ep)
    {
        while (_running)
        {
            var start = DateTime.UtcNow;
            try
            {
                using var bmp = CaptureScreen();
                if (bmp != null)
                {
                    using var mat = BitmapToMat(bmp);
                    SendVideoFrame(sock, ep, mat);
                }
            }
            catch { }
            ThrottleFps(start);
        }
    }

    private void ThrottleFps(DateTime start)
    {
        if (UseSourceFps) return;
        var delay = (1.0 / TargetFps) - (DateTime.UtcNow - start).TotalSeconds;
        if (delay > 0) Thread.Sleep((int)(delay * 1000));
    }

    private void SendVideoFrame(UdpClient sock, IPEndPoint ep, Mat frame)
    {
        Mat? resized = null;
        Mat  toSend  = frame;
        try
        {
            if (TargetRes.HasValue)
            {
                resized = new Mat();
                Cv2.Resize(frame, resized, new OpenCvSharp.Size(TargetRes.Value.W, TargetRes.Value.H));
                toSend = resized;
            }
            Cv2.ImEncode(".jpg", toSend, out var jpg,
                new ImageEncodingParam(ImwriteFlags.JpegQuality, 60));

            const int maxChunk = 60000;
            for (int i = 0; i < jpg.Length; i += maxChunk)
            {
                int  end      = Math.Min(i + maxChunk, jpg.Length);
                bool isLast   = end >= jpg.Length;
                int  chunkLen = end - i;
                var  packet   = new byte[8 + chunkLen];
                packet[0] = isLast ? (byte)1 : (byte)0;
                BitConverter.GetBytes((uint)chunkLen).CopyTo(packet, 4);
                Array.Copy(jpg, i, packet, 8, chunkLen);
                try { sock.Send(packet, packet.Length, ep); } catch { }
            }
        }
        finally { resized?.Dispose(); }
    }

    // ── Receiver ─────────────────────────────────────────────────────────────

    private void RunReceiver()
    {
        using var sock = new UdpClient();
        sock.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        sock.Client.Bind(new IPEndPoint(IPAddress.Any, _port));
        sock.Client.ReceiveTimeout = 1000;
        var remoteEp = new IPEndPoint(IPAddress.Any, 0);

        if (_mode == MediaMode.Audio) RunAudioReceiver(sock, remoteEp);
        else                          RunVideoReceiver(sock, remoteEp);
    }

    private bool _connectedFired;
    private void FireConnected()
    {
        if (_connectedFired) return;
        _connectedFired = true;
        Connected?.Invoke();
    }

    private void RunAudioReceiver(UdpClient sock, IPEndPoint remoteEp)
    {
        WaveOutEvent?         waveOut = null;
        BufferedWaveProvider? buffer  = null;
        try
        {
            buffer  = new BufferedWaveProvider(new WaveFormat(44100, 16, 1)) { DiscardOnBufferOverflow = true };
            waveOut = new WaveOutEvent();
            waveOut.Init(buffer);
            waveOut.Play();
            while (_running)
            {
                try
                {
                    var data = sock.Receive(ref remoteEp);
                    FireConnected();   // ← first packet = connected
                    buffer.AddSamples(data, 0, data.Length);
                }
                catch (SocketException) { /* timeout */ }
                catch { break; }
            }
            waveOut.Stop();
        }
        catch { }
        finally { waveOut?.Dispose(); }
    }

    private void RunVideoReceiver(UdpClient sock, IPEndPoint remoteEp)
    {
        var frameData = new MemoryStream();
        while (_running)
        {
            try
            {
                var packet = sock.Receive(ref remoteEp);
                if (packet.Length < MediaSettings.FrameHeaderSize) continue;
                FireConnected();   // ← first packet = connected
                bool isLast = packet[0] != 0;
                frameData.Write(packet, MediaSettings.FrameHeaderSize,
                                packet.Length - MediaSettings.FrameHeaderSize);
                if (!isLast) continue;
                var raw = frameData.ToArray();
                frameData.SetLength(0);
                using var mat = Cv2.ImDecode(raw, ImreadModes.Color);
                if (mat == null || mat.Empty()) continue;
                FrameReceived?.Invoke(MediaWorkerHelper.MatToBitmapSource(mat));
            }
            catch (SocketException) { /* timeout */ }
            catch { break; }
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private static System.Drawing.Bitmap? CaptureScreen()
    {
        try
        {
            var (w, h) = PrimaryScreenSize();
            var bmp    = new System.Drawing.Bitmap(w, h);
            using var g = System.Drawing.Graphics.FromImage(bmp);
            g.CopyFromScreen(0, 0, 0, 0, new System.Drawing.Size(w, h));
            return bmp;
        }
        catch { return null; }
    }

    private static Mat BitmapToMat(System.Drawing.Bitmap bmp)
    {
        using var ms = new MemoryStream();
        bmp.Save(ms, System.Drawing.Imaging.ImageFormat.Bmp);
        return Cv2.ImDecode(ms.ToArray(), ImreadModes.Color);
    }

    private static BitmapSource MatToBitmapSource(Mat mat) =>
        MediaWorkerHelper.MatToBitmapSource(mat);
}
