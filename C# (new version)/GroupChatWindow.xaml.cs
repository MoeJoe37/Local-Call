using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using Microsoft.Win32;

namespace LocalCallPro;

public partial class GroupChatWindow : Window
{
    private readonly GroupInfo                       _group;
    private readonly string                          _myId;
    private readonly string                          _myName;
    private readonly ChatStore                       _store;
    private readonly string                          _convKey;
    private readonly ObservableCollection<ChatMessageVm> _msgs = [];
    private readonly VoiceNoteRecorder               _vnRec = new();

    public event Action<GroupInfo, SigMsg>? BroadcastRequested;
    public event Action<GroupInfo, string>? GroupCallRequested;

    public GroupChatWindow(GroupInfo group, string myId, string myName, ChatStore store)
    {
        InitializeComponent();
        _group   = group;
        _myId    = myId;
        _myName  = myName;
        _store   = store;
        _convKey = $"grp-{group.GroupId}";

        Title            = $"Group: {group.Name}";
        TxtGroupName.Text = group.Name;
        MsgList.ItemsSource  = _msgs;
        MemberList.ItemsSource = group.Members.Select(m => $"🟢 {m.Name}").ToList();

        foreach (var m in _store.Load(_convKey))
            _msgs.Add(new ChatMessageVm(m, _myId));

        ScrollToBottom();
    }

    public void ReceiveMessage(SigMsg sig)
    {
        var m = new ChatMessage
        {
            Kind      = sig.Type == SigType.GrpVoice ? MessageKind.VoiceNote
                      : sig.Type == SigType.GrpFile  ? (IsImage(sig.Mime) ? MessageKind.Image : MessageKind.File)
                      : MessageKind.Text,
            FromId    = sig.FromId,
            FromName  = sig.FromName,
            Text      = sig.Text,
            FileName  = sig.FileName,
            Mime      = sig.Mime,
            Data      = sig.Data != null ? Convert.FromBase64String(sig.Data) : null,
            IsMine    = false,
            Timestamp = DateTimeOffset.FromUnixTimeMilliseconds(sig.Ts).LocalDateTime
        };
        Dispatcher.Invoke(() => { AddMsg(m, save: true); ScrollToBottom(); });
    }

    private void BtnSend_Click(object sender, RoutedEventArgs e)      => SendText();
    private void TxtInput_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) { SendText(); e.Handled = true; }
    }

    private void SendText()
    {
        var text = TxtInput.Text.Trim();
        if (string.IsNullOrEmpty(text)) return;
        TxtInput.Clear();

        var sig = BuildSig(SigType.GrpText);
        sig.Text = text;
        BroadcastRequested?.Invoke(_group, sig);

        var msg = BuildOutgoing(MessageKind.Text);
        msg.Text = text;
        AddMsg(msg, save: true);
        ScrollToBottom();
    }

    private void BtnSendImage_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Filter = "Images & Videos|*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.mp4;*.mkv;*.mov|All Files|*.*"
        };
        if (dlg.ShowDialog() == true) SendFile(dlg.FileName);
    }

    private void BtnSendFile_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog();
        if (dlg.ShowDialog() == true) SendFile(dlg.FileName);
    }

    private void SendFile(string path)
    {
        var info = new FileInfo(path);
        if (info.Length > MediaSettings.FileMaxBytes)
        { MessageBox.Show("File too large (max 50 MB)."); return; }

        var data = File.ReadAllBytes(path);
        var mime = GuessMime(info.Extension);
        var kind = IsImage(mime) ? MessageKind.Image : MessageKind.File;

        var sig      = BuildSig(SigType.GrpFile);
        sig.FileName = info.Name;
        sig.Mime     = mime;
        sig.Data     = Convert.ToBase64String(data);
        BroadcastRequested?.Invoke(_group, sig);

        var msg      = BuildOutgoing(kind);
        msg.FileName = info.Name;
        msg.Mime     = mime;
        msg.Data     = data;
        AddMsg(msg, save: true);
        ScrollToBottom();
    }

    private void BtnVoiceNote_MouseDown(object sender, MouseButtonEventArgs e)
    {
        _vnRec.Start();
        TxtRecording.Visibility = Visibility.Visible;
    }

    private void BtnVoiceNote_MouseUp(object sender, MouseButtonEventArgs e)
    {
        TxtRecording.Visibility = Visibility.Collapsed;
        var wav = _vnRec.Stop();
        if (wav.Length < 100) return;

        var sig  = BuildSig(SigType.GrpVoice);
        sig.Data = Convert.ToBase64String(wav);
        BroadcastRequested?.Invoke(_group, sig);

        var msg      = BuildOutgoing(MessageKind.VoiceNote);
        msg.FileName = "voice_note.wav";
        msg.Data     = wav;
        AddMsg(msg, save: true);
        ScrollToBottom();
    }

    private void BtnGroupCall_Click(object sender, RoutedEventArgs e)  => GroupCallRequested?.Invoke(_group, "voice");
    private void BtnGroupVideo_Click(object sender, RoutedEventArgs e) => GroupCallRequested?.Invoke(_group, "video");

    private void AddMsg(ChatMessage m, bool save)
    {
        _msgs.Add(new ChatMessageVm(m, _myId));
        if (save) _store.Append(_convKey, m);
    }

    private ChatMessage BuildOutgoing(MessageKind kind) => new()
    {
        Kind = kind, FromId = _myId, FromName = _myName, IsMine = true, Timestamp = DateTime.Now
    };

    private SigMsg BuildSig(string type) => new()
    {
        Type = type, FromId = _myId, FromName = _myName,
        GroupId = _group.GroupId, Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
    };

    private void ScrollToBottom() =>
        Dispatcher.InvokeAsync(() => Scroller.ScrollToEnd(),
            System.Windows.Threading.DispatcherPriority.Loaded);

    private static bool IsImage(string? m) => m != null && m.StartsWith("image/");

    private static string GuessMime(string ext) => ext.ToLowerInvariant() switch
    {
        ".png" => "image/png", ".jpg" or ".jpeg" => "image/jpeg", ".gif" => "image/gif",
        ".mp4" => "video/mp4", ".mkv" => "video/x-matroska", _ => "application/octet-stream"
    };

    private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e) =>
        _vnRec.Dispose();
}
