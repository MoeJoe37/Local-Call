using System;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace LocalCallPro;

public static class SignalingClient
{
    /// <summary>
    /// Send with automatic retry (up to 3 attempts, 600 ms apart).
    /// Returns true if delivery was confirmed.
    /// </summary>
    public static async Task<bool> SendWithRetryAsync(string ip, SigMsg msg, int attempts = 3)
    {
        for (int i = 0; i < attempts; i++)
        {
            if (await TrySendOnceAsync(ip, msg)) return true;
            if (i < attempts - 1) await Task.Delay(600);
        }
        return false;
    }

    /// <summary>Fire-and-forget — retries in background. Use for non-critical signals.</summary>
    public static void Send(string ip, SigMsg msg) =>
        Task.Run(() => SendWithRetryAsync(ip, msg, attempts: 1));

    /// <summary>Fire-and-forget with retries. Use for critical signals (friend accept, call invite).</summary>
    public static void SendReliable(string ip, SigMsg msg) =>
        Task.Run(() => SendWithRetryAsync(ip, msg, attempts: 3));

    private static async Task<bool> TrySendOnceAsync(string ip, SigMsg msg)
    {
        try
        {
            var body   = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(msg));
            var lenPfx = new byte[]
            {
                (byte)(body.Length >> 24),
                (byte)(body.Length >> 16),
                (byte)(body.Length >>  8),
                (byte)(body.Length)
            };

            using var cts    = new CancellationTokenSource(TimeSpan.FromSeconds(4));
            using var client = new TcpClient();
            await client.ConnectAsync(ip, MediaSettings.SignalingPort, cts.Token);

            var stream = client.GetStream();
            await stream.WriteAsync(lenPfx, cts.Token);
            await stream.WriteAsync(body,   cts.Token);
            await stream.FlushAsync(cts.Token);
            return true;
        }
        catch { return false; }
    }
}
