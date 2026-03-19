using System;

namespace LocalCallPro;

public class PendingRequest
{
    public string FromId   { get; set; } = "";
    public string FromName { get; set; } = "";
    public string FromIp   { get; set; } = "";
    public long   Ts       { get; set; } = DateTimeOffset.Now.ToUnixTimeMilliseconds();
}
