using System;
using System.Diagnostics;
using System.Security.Principal;

namespace LocalCallPro;

public static class FirewallHelper
{
    private const string App = "Local Call";

    public static void EnsureRules()
    {
        if (RulesExist()) return;
        if (IsElevated()) AddRules();
        else              ElevateAndAdd();
    }

    public static void RunFirewallSetup() => AddRules();

    private static void AddRules()
    {
        // Discovery UDP
        Rm($"{App} UDP");        Add($"{App} UDP",       "UDP", "in",  MediaSettings.BroadcastPort.ToString());
        Rm($"{App} UDP Out");    Add($"{App} UDP Out",   "UDP", "out", MediaSettings.BroadcastPort.ToString());
        // Signaling TCP
        Rm($"{App} TCP");        Add($"{App} TCP",       "TCP", "in",  MediaSettings.SignalingPort.ToString());
        Rm($"{App} TCP Out");    Add($"{App} TCP Out",   "TCP", "out", MediaSettings.SignalingPort.ToString());
        // Media audio UDP
        Rm($"{App} Audio In");   Add($"{App} Audio In",  "UDP", "in",  MediaSettings.MediaAudioPort.ToString());
        Rm($"{App} Audio Out");  Add($"{App} Audio Out", "UDP", "out", MediaSettings.MediaAudioPort.ToString());
        // Media video UDP
        Rm($"{App} Video In");   Add($"{App} Video In",  "UDP", "in",  MediaSettings.MediaVideoPort.ToString());
        Rm($"{App} Video Out");  Add($"{App} Video Out", "UDP", "out", MediaSettings.MediaVideoPort.ToString());
    }

    private static bool RulesExist()
    {
        try
        {
            var p = Process.Start(new ProcessStartInfo("netsh",
                $"advfirewall firewall show rule name=\"{App} Audio In\"")
            { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true })!;
            var o = p.StandardOutput.ReadToEnd(); p.WaitForExit(3000);
            return o.Contains(App);
        }
        catch { return false; }
    }

    private static void Rm(string name)   => Netsh($"advfirewall firewall delete rule name=\"{name}\"");
    private static void Add(string name, string proto, string dir, string port)
    {
        var field = dir == "in" ? "localport" : "remoteport";
        Netsh($"advfirewall firewall add rule name=\"{name}\" dir={dir} action=allow protocol={proto} {field}={port}");
    }

    private static bool IsElevated()
    {
        try { using var id = WindowsIdentity.GetCurrent();
              return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator); }
        catch { return false; }
    }

    private static void ElevateAndAdd()
    {
        try
        {
            var exe = Process.GetCurrentProcess().MainModule?.FileName ?? "";
            if (string.IsNullOrEmpty(exe)) { AddRules(); return; }
            using var p = Process.Start(new ProcessStartInfo
            {
                FileName = exe, Arguments = "/firewall",
                UseShellExecute = true, Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden
            });
            p?.WaitForExit(15000);
        }
        catch { AddRules(); }
    }

    private static void Netsh(string args)
    {
        try
        {
            using var p = Process.Start(new ProcessStartInfo("netsh", args)
            { UseShellExecute = false, CreateNoWindow = true,
              RedirectStandardOutput = true, RedirectStandardError = true });
            p?.WaitForExit(5000);
        }
        catch { }
    }
}
