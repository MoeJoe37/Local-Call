using System.Windows;
using System.Windows.Media;

namespace LocalCallPro;

/// <summary>
/// WPF binding wrapper for FriendInfo.
/// IsFormer = true means the friendship was removed but chat history is still accessible.
/// </summary>
public class FriendItemVm(FriendInfo f, bool isFormer = false)
    : System.ComponentModel.INotifyPropertyChanged
{
    public FriendInfo Friend   => f;

    public string Id           => f.Id;
    public string Name         => f.Name;
    public string Ip           => f.Ip;
    public bool   IsFormer     => isFormer;

    public string StatusColor  => isFormer ? "#333333" : f.StatusColor;
    public int    UnreadCount  => f.UnreadCount;

    // Former friends show with a strikethrough-style dim name and lock icon
    public string DisplayName  => isFormer ? $"🔒 {f.Name}" : f.Name;
    public double NameOpacity  => isFormer ? 0.45 : 1.0;

    public Visibility UnreadBadgeVisibility =>
        f.UnreadCount > 0 ? Visibility.Visible : Visibility.Collapsed;

    public event System.ComponentModel.PropertyChangedEventHandler? PropertyChanged;

    public void Refresh() =>
        PropertyChanged?.Invoke(this, new System.ComponentModel.PropertyChangedEventArgs(null));
}
