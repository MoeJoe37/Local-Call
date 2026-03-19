using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Text.Json.Serialization;

namespace LocalCallPro;

public class FriendInfo : INotifyPropertyChanged
{
    public string Id   { get; set; } = "";
    public string Name { get; set; } = "";
    public string Ip   { get; set; } = "";  // last known LAN IP

    private bool _isOnline;
    [JsonIgnore]
    public bool IsOnline
    {
        get => _isOnline;
        set { _isOnline = value; OnPropertyChanged(); OnPropertyChanged(nameof(StatusColor)); }
    }

    private int _unread;
    [JsonIgnore]
    public int UnreadCount
    {
        get => _unread;
        set { _unread = value; OnPropertyChanged(); }
    }

    [JsonIgnore]
    public string StatusColor => IsOnline ? "#03DAC6" : "#555555";

    public override string ToString() => Name;

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? n = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
}
