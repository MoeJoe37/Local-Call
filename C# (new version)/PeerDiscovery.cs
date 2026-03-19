using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace LocalCallPro;

/// <summary>
/// LAN peer discovery using three parallel strategies:
///
///   1. UDP broadcast   – 255.255.255.255 + subnet broadcast (e.g. 192.168.1.255)
///   2. UDP multicast   – group 239.255.42.99  (works through managed switches)
///   3. TCP subnet scan – probes SignalingPort across /24  (works when all UDP blocked)
///
/// A SINGLE listener socket handles both broadcast + multicast to avoid port conflicts.
/// Windows Firewall rules are added by FirewallHelper before this class starts.
/// DiagLog fires human-readable status → shown in the status bar.
/// </summary>
public class PeerDiscovery
{
    private readonly string _myId;
    private          string _myName;

    private readonly ConcurrentDictionary<string, PeerInfo> _peers = new();
    private volatile bool _running;

    private static readonly IPAddress MulticastGroup =
        IPAddress.Parse("239.255.42.99");

    public event Action<Dictionary<string, PeerInfo>>? PeersUpdated;
    public event Action<string>?                        DiagLog;

    public PeerDiscovery(string myId, string myName)
    {
        _myId   = myId;
        _myName = myName;
    }

    public void UpdateName(string name) => _myName = name;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    public void Start()
    {
        _running = true;
        Bg(RunSender,   "LC-Send");
        Bg(RunListener, "LC-Listen");
        Bg(RunPrune,    "LC-Prune");

        // TCP scan: first pass after 2 s, then every 25 s
        Task.Run(async () =>
        {
            await Task.Delay(2_000);
            while (_running)
            {
                RunTcpScan();
                await Task.Delay(25_000);
            }
        });
    }

    public void Stop()   => _running = false;
    public void ForceRescan() => Task.Run(RunTcpScan);

    private void Bg(ThreadStart fn, string name) =>
        new Thread(fn) { IsBackground = true, Name = name }.Start();

    // ═══════════════════════════════════════════════════════════════════════════
    //  SENDER  –  broadcasts to every target every 2 s
    // ═══════════════════════════════════════════════════════════════════════════

    private void RunSender()
    {
        UdpClient? sock = null;
        try
        {
            sock = new UdpClient(AddressFamily.InterNetwork);
            sock.EnableBroadcast   = true;
            sock.MulticastLoopback = false;
            sock.Ttl               = 4;

            Log("Sender ready");

            while (_running)
            {
                var data    = Encode();
                var targets = BuildTargets();

                foreach (var ep in targets)
                {
                    try { sock.Send(data, data.Length, ep); }
                    catch { /* one interface may be down */ }
                }

                Thread.Sleep(MediaSettings.BroadcastIntervalMs);
            }
        }
        catch (Exception ex) { Log($"Sender error: {ex.Message}"); }
        finally { sock?.Dispose(); }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  LISTENER  –  single socket receives broadcast + multicast
    // ═══════════════════════════════════════════════════════════════════════════

    private void RunListener()
    {
        UdpClient? sock = null;
        try
        {
            // Build a raw socket so we can set all options before bind
            var raw = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            raw.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress,     true);
            raw.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast,        true);
            raw.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ExclusiveAddressUse, false);
            raw.Bind(new IPEndPoint(IPAddress.Any, MediaSettings.BroadcastPort));
            raw.ReceiveTimeout = 1_000;

            sock = new UdpClient { Client = raw };

            // Join multicast on every suitable interface
            int mcJoined = 0;
            foreach (var ip in LocalIPv4s())
            {
                try { sock.JoinMulticastGroup(MulticastGroup, ip); mcJoined++; }
                catch { /* interface may not support multicast */ }
            }
            // Fallback join with no interface specified
            try { sock.JoinMulticastGroup(MulticastGroup); mcJoined++; } catch { }

            Log($"Listener bound :{MediaSettings.BroadcastPort} — multicast joined on {mcJoined} interface(s)");

            var remote = new IPEndPoint(IPAddress.Any, 0);
            while (_running)
            {
                try
                {
                    var data = sock.Receive(ref remote);
                    ParsePacket(data, remote.Address.ToString(), "udp");
                }
                catch (SocketException se) when (se.SocketErrorCode == SocketError.TimedOut) { }
                catch (SocketException se) when (se.SocketErrorCode == SocketError.Interrupted) { break; }
                catch (SocketException se) { Log($"Listener recv err: {se.SocketErrorCode}"); Thread.Sleep(200); }
                catch { /* malformed — ignore */ }
            }
        }
        catch (SocketException se)
        {
            Log($"⚠ Cannot bind UDP :{MediaSettings.BroadcastPort} — {se.SocketErrorCode}. " +
                "Check Windows Firewall / run as Administrator once to add rules.");
        }
        catch (Exception ex) { Log($"Listener fatal: {ex.Message}"); }
        finally { sock?.Dispose(); }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  TCP SUBNET SCAN
    // ═══════════════════════════════════════════════════════════════════════════

    private void RunTcpScan()
    {
        var locals = LocalIPv4s().ToList();
        if (locals.Count == 0) return;

        Log($"TCP scan starting ({locals.Count} interface(s))…");
        int found = 0;

        foreach (var local in locals)
        {
            var b = local.GetAddressBytes();
            var targets = Enumerable.Range(1, 254)
                .Select(i => new[] { b[0], b[1], b[2], (byte)i })
                .Select(x => new IPAddress(x))
                .Where(ip => !ip.Equals(local))
                .ToList();

            Parallel.ForEach(targets,
                new ParallelOptions { MaxDegreeOfParallelism = 60 },
                target =>
                {
                    if (!_running) return;
                    if (TcpProbe(target.ToString()))
                        Interlocked.Increment(ref found);
                });
        }

        Log($"TCP scan done — {found} peer(s) found");
    }

    private bool TcpProbe(string ip)
    {
        TcpClient? client = null;
        try
        {
            client = new TcpClient();
            client.ReceiveTimeout = 200;
            client.SendTimeout    = 200;

            // ConnectAsync with cancellation is cleaner than BeginConnect on .NET 8
            using var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(150));
            try { client.ConnectAsync(ip, MediaSettings.SignalingPort, cts.Token).AsTask().Wait(cts.Token); }
            catch (OperationCanceledException) { return false; }

            if (!client.Connected) return false;

            var stream = client.GetStream();
            stream.WriteTimeout = 200;
            stream.ReadTimeout  = 400;

            // Length-prefixed probe
            var probe = WireEncode(new SigMsg
            {
                Type     = "disc_probe",
                FromId   = _myId,
                FromName = _myName,
                Ts       = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            });
            stream.Write(probe, 0, probe.Length);
            stream.Flush();

            // Read response
            var hdr = new byte[4];
            if (!FullRead(stream, hdr)) return false;
            var len = (hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3];
            if (len <= 0 || len > 8192) return false;
            var body = new byte[len];
            if (!FullRead(stream, body)) return false;

            var resp = JsonSerializer.Deserialize<SigMsg>(Encoding.UTF8.GetString(body));
            if (resp?.Type != "disc_resp" || string.IsNullOrEmpty(resp.FromId)) return false;
            if (resp.FromId == _myId) return false;

            AddPeer(resp.FromId, resp.FromName ?? ip, ip, "tcp");
            return true;
        }
        catch { return false; }
        finally { client?.Dispose(); }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  PRUNE + PUBLISH
    // ═══════════════════════════════════════════════════════════════════════════

    private void RunPrune()
    {
        while (_running)
        {
            Thread.Sleep(1_000);
            var cutoff = DateTime.UtcNow.AddSeconds(-MediaSettings.PeerTimeoutSeconds);
            foreach (var kv in _peers)
                if (kv.Value.LastSeen < cutoff)
                    _peers.TryRemove(kv.Key, out _);

            PeersUpdated?.Invoke(new Dictionary<string, PeerInfo>(_peers));
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  HELPERS
    // ═══════════════════════════════════════════════════════════════════════════

    private byte[] Encode()
    {
        var json = JsonSerializer.Serialize(new { id = _myId, name = _myName });
        return Encoding.UTF8.GetBytes(json);
    }

    private static byte[] WireEncode(SigMsg msg)
    {
        var body = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(msg));
        var out_ = new byte[4 + body.Length];
        out_[0] = (byte)(body.Length >> 24);
        out_[1] = (byte)(body.Length >> 16);
        out_[2] = (byte)(body.Length >>  8);
        out_[3] = (byte)(body.Length);
        Buffer.BlockCopy(body, 0, out_, 4, body.Length);
        return out_;
    }

    private void ParsePacket(byte[] data, string senderIp, string via)
    {
        try
        {
            using var doc = JsonDocument.Parse(Encoding.UTF8.GetString(data));
            var root = doc.RootElement;
            var id   = root.GetProperty("id").GetString()   ?? "";
            var name = root.GetProperty("name").GetString() ?? id;
            if (string.IsNullOrEmpty(id) || id == _myId) return;
            AddPeer(id, name, senderIp, via);
        }
        catch { }
    }

    private void AddPeer(string id, string name, string ip, string via)
    {
        bool isNew = !_peers.ContainsKey(id);
        _peers[id] = new PeerInfo { Id = id, Name = name, Ip = ip, LastSeen = DateTime.UtcNow };
        if (isNew) Log($"✓ Found: {name} ({ip}) via {via}");
    }

    private void Log(string msg) =>
        DiagLog?.Invoke($"[{DateTime.Now:HH:mm:ss}] {msg}");

    // ── Send to all broadcast + multicast targets ──────────────────────────────

    private static List<IPEndPoint> BuildTargets()
    {
        var eps = new List<IPEndPoint>
        {
            new(IPAddress.Broadcast,  MediaSettings.BroadcastPort),   // 255.255.255.255
            new(MulticastGroup,       MediaSettings.BroadcastPort),   // multicast
        };

        foreach (var iface in SafeInterfaces())
        {
            foreach (var ua in iface.GetIPProperties().UnicastAddresses)
            {
                if (ua.Address.AddressFamily != AddressFamily.InterNetwork) continue;
                var mask = ua.IPv4Mask;
                if (mask == null) continue;
                var ip = ua.Address.GetAddressBytes();
                var m  = mask.GetAddressBytes();
                var bc = new byte[4];
                for (int i = 0; i < 4; i++) bc[i] = (byte)(ip[i] | ~m[i]);
                var bcast = new IPEndPoint(new IPAddress(bc), MediaSettings.BroadcastPort);
                if (!eps.Any(e => e.Address.Equals(bcast.Address)))
                    eps.Add(bcast);
            }
        }
        return eps;
    }

    private static IEnumerable<IPAddress> LocalIPv4s()
    {
        return SafeInterfaces()
            .SelectMany(n => n.GetIPProperties().UnicastAddresses)
            .Where(ua => ua.Address.AddressFamily == AddressFamily.InterNetwork)
            .Select(ua => ua.Address);
    }

    private static IEnumerable<NetworkInterface> SafeInterfaces()
    {
        try
        {
            return NetworkInterface.GetAllNetworkInterfaces()
                .Where(n => n.OperationalStatus == OperationalStatus.Up
                         && n.NetworkInterfaceType != NetworkInterfaceType.Loopback
                         && n.NetworkInterfaceType != NetworkInterfaceType.Tunnel);
        }
        catch { return []; }
    }

    private static bool FullRead(NetworkStream s, byte[] buf)
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
