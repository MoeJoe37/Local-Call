using System;

namespace LocalCallPro;

public class PeerInfo
{
    public string   Id       { get; set; } = "";
    public string   Name     { get; set; } = "";
    public string   Ip       { get; set; } = "";
    public DateTime LastSeen { get; set; }

    public override string ToString() => $"{Name}  ({Ip})";
}
