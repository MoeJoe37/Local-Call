using System;
using System.Net;
using System.Net.Sockets;

namespace LocalCallPro;

public static class Helpers
{
    private static readonly string[] Adjs  = ["Silly","Brave","Goofy","Turbo","Fancy","Sleepy","Hyper","Invisible"];
    private static readonly string[] Nouns = ["Hamster","Potato","Ninja","Wizard","Toaster","Unicorn","Panda","Cactus"];
    private static readonly Random   Rng   = new();

    public static string GetFunnyName() =>
        $"{Adjs[Rng.Next(Adjs.Length)]} {Nouns[Rng.Next(Nouns.Length)]}";

    public static string GetLocalIp()
    {
        try
        {
            using var s = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            s.Connect("8.8.8.8", 80);
            return ((IPEndPoint)s.LocalEndPoint!).Address.ToString();
        }
        catch { return "127.0.0.1"; }
    }
}
