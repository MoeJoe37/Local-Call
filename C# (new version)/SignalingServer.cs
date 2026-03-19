using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace LocalCallPro;

/// <summary>
/// TCP listener on <see cref="MediaSettings.SignalingPort"/>.
///
/// Normal messages: one length-prefixed JSON SigMsg → fire MessageReceived.
/// Discovery probes (type = "disc_probe"): reply with a "disc_resp" and don't fire MessageReceived.
/// </summary>
public class SignalingServer
{
    private TcpListener?  _listener;
    private volatile bool _running;
    private string        _myId   = "";
    private string        _myName = "";

    /// <summary>Fired on a background thread. Marshal to UI thread before touching WPF.</summary>
    public event Action<SigMsg, string>? MessageReceived;

    public void SetIdentity(string id, string name) { _myId = id; _myName = name; }

    public void Start()
    {
        _running  = true;
        _listener = new TcpListener(IPAddress.Any, MediaSettings.SignalingPort);
        _listener.Server.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        _listener.Start();
        new Thread(AcceptLoop) { IsBackground = true, Name = "SigAccept" }.Start();
    }

    public void Stop()
    {
        _running = false;
        try { _listener?.Stop(); } catch { }
    }

    private void AcceptLoop()
    {
        while (_running)
        {
            try
            {
                var client = _listener!.AcceptTcpClient();
                new Thread(() => HandleClient(client)) { IsBackground = true }.Start();
            }
            catch { if (_running) Thread.Sleep(500); }
        }
    }

    private void HandleClient(TcpClient client)
    {
        var ip = ((IPEndPoint)client.Client.RemoteEndPoint!).Address.ToString();
        try
        {
            using (client)
            {
                using var stream = client.GetStream();
                stream.ReadTimeout = 10_000;

                var hdr = new byte[4];
                if (!ReadFully(stream, hdr)) return;
                var len = (hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3];
                if (len <= 0 || len > 60 * 1024 * 1024) return;

                var body = new byte[len];
                if (!ReadFully(stream, body)) return;

                var msg = JsonSerializer.Deserialize<SigMsg>(Encoding.UTF8.GetString(body));
                if (msg == null) return;

                // Discovery probe — respond with our identity and DO NOT raise MessageReceived
                if (msg.Type == "disc_probe")
                {
                    var resp = BuildReply("disc_resp");
                    stream.WriteTimeout = 2_000;
                    stream.Write(resp, 0, resp.Length);
                    stream.Flush();
                    return;
                }

                MessageReceived?.Invoke(msg, ip);
            }
        }
        catch { /* disconnected / parse error */ }
    }

    private byte[] BuildReply(string type)
    {
        var body = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new SigMsg
        {
            Type     = type,
            FromId   = _myId,
            FromName = _myName,
            Ts       = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
        }));
        var result = new byte[4 + body.Length];
        result[0] = (byte)(body.Length >> 24);
        result[1] = (byte)(body.Length >> 16);
        result[2] = (byte)(body.Length >>  8);
        result[3] = (byte)(body.Length);
        Buffer.BlockCopy(body, 0, result, 4, body.Length);
        return result;
    }

    private static bool ReadFully(NetworkStream s, byte[] buf)
    {
        int off = 0;
        while (off < buf.Length)
        {
            int n = s.Read(buf, off, buf.Length - off);
            if (n == 0) return false;
            off += n;
        }
        return true;
    }
}
