using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using NAudio.Wave;

namespace LocalCallPro;

/// <summary>
/// 1-to-1 chat window.
/// Injected with the shared ChatStore so messages survive window close/reopen.
/// </summary>
public partial class ChatWindow : Window
{
    private readonly FriendInfo          _peer;
    private readonly string              _myId;
    private readonly string              _myName;
    private readonly ChatStore           _store;
    private readonly string              _convKey;
    private readonly ObservableCollection<ChatMessageVm> _msgs = [];

    private CallWindow?        _callWin;
    private VoiceNoteRecorder  _vnRec = new();

    // ── events back to MainWindow ─────────────────────────────────────────────
    public event Action<FriendInfo, CallMode>? CallRequested;

    public ChatWindow(FriendInfo peer, string myId, string myName, ChatStore store)
    {
        InitializeComponent();
        _peer    = peer;
        _myId    = myId;
        _myName  = myName;
        _store   = store;
        _convKey = string.Join("-", new[] { myId, peer.Id }.OrderBy(x => x));

        Title                = $"Chat with {peer.Name}";
        TxtPeerName.Text     = peer.Name;
        StatusDot.Fill       = peer.IsOnline ? Brushes.Teal : Brushes.Gray;
        MsgList.ItemsSource  = _msgs;

        // Load history
        foreach (var m in _store.Load(_convKey))
            _msgs.Add(new ChatMessageVm(m, _myId));

        ScrollToBottom();
    }

    // ── Called by MainWindow when an incoming message arrives ─────────────────
    public void ReceiveMessage(SigMsg sig)
    {
        var m = new ChatMessage
        {
            Kind      = sig.Type == SigType.ChatVoice ? MessageKind.VoiceNote
                      : sig.Type == SigType.ChatFile  ? (IsImage(sig.Mime) ? MessageKind.Image : MessageKind.File)
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
        Dispatcher.Invoke(() =>
        {
            AddMessage(m, save: true);
            ScrollToBottom();
        });
    }

    public void SetOnlineStatus(bool online) =>
        Dispatcher.Invoke(() => StatusDot.Fill = online ? Brushes.Teal : Brushes.Gray);

    // ── Send text ──────────────────────────────────────────────────────────────

    private void BtnSend_Click(object sender, RoutedEventArgs e) => SendText();
    private void TxtInput_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && !Keyboard.IsKeyDown(Key.LeftShift))
        { SendText(); e.Handled = true; }
    }

    private void SendText()
    {
        var text = TxtInput.Text.Trim();
        if (string.IsNullOrEmpty(text) || !_peer.IsOnline) return;
        TxtInput.Clear();

        var msg = BuildOutgoing(MessageKind.Text);
        msg.Text = text;

        var sig = BuildSig(SigType.ChatText);
        sig.Text = text;
        SignalingClient.Send(_peer.Ip, sig);
        AddMessage(msg, save: true);
        ScrollToBottom();
    }

    // ── Send image / video ────────────────────────────────────────────────────

    private void BtnSendImage_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Filter = "Images & Videos|*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.mp4;*.mkv;*.mov;*.avi|All Files|*.*",
            Title  = "Select image or video"
        };
        if (dlg.ShowDialog() != true) return;
        SendFileBytes(dlg.FileName);
    }

    // ── Send any file ─────────────────────────────────────────────────────────

    private void BtnSendFile_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Title = "Select file to send" };
        if (dlg.ShowDialog() != true) return;
        SendFileBytes(dlg.FileName);
    }

    private void SendFileBytes(string path)
    {
        var info = new FileInfo(path);
        if (info.Length > MediaSettings.FileMaxBytes)
        { MessageBox.Show("File too large (max 50 MB)", "Local Call"); return; }

        byte[] data;
        try { data = File.ReadAllBytes(path); }
        catch { MessageBox.Show("Could not read file.", "Local Call"); return; }

        var mime    = GuessMime(info.Extension);
        var msgKind = IsImage(mime) ? MessageKind.Image : MessageKind.File;

        var msg     = BuildOutgoing(msgKind);
        msg.FileName = info.Name;
        msg.Mime     = mime;
        msg.Data     = data;

        if (msgKind == MessageKind.Image)
            msg.ImageSource = LoadBitmap(data);

        var sig      = BuildSig(IsImage(mime) ? SigType.ChatFile : SigType.ChatFile);
        sig.FileName = info.Name;
        sig.Mime     = mime;
        sig.Data     = Convert.ToBase64String(data);
        SignalingClient.Send(_peer.Ip, sig);

        AddMessage(msg, save: true);
        ScrollToBottom();
    }

    // ── Voice note ────────────────────────────────────────────────────────────

    private void BtnVoiceNote_MouseDown(object sender, MouseButtonEventArgs e)
    {
        _vnRec.Start();
        TxtRecording.Visibility = Visibility.Visible;
        BtnVoiceNote.Foreground = Brushes.OrangeRed;
    }

    private void BtnVoiceNote_MouseUp(object sender, MouseButtonEventArgs e)
    {
        TxtRecording.Visibility = Visibility.Collapsed;
        BtnVoiceNote.Foreground = Brushes.Gray;
        var wav = _vnRec.Stop();
        if (wav.Length < 100) return;

        var msg      = BuildOutgoing(MessageKind.VoiceNote);
        msg.FileName = "voice_note.wav";
        msg.Data     = wav;

        var sig  = BuildSig(SigType.ChatVoice);
        sig.Data = Convert.ToBase64String(wav);
        SignalingClient.Send(_peer.Ip, sig);

        AddMessage(msg, save: true);
        ScrollToBottom();
    }

    // ── Calls ─────────────────────────────────────────────────────────────────

    private void BtnVoiceCall_Click(object sender, RoutedEventArgs e)  => RequestCall(CallMode.Voice);
    private void BtnVideoCall_Click(object sender, RoutedEventArgs e)  => RequestCall(CallMode.VideoCamera);

    private void RequestCall(CallMode mode) => CallRequested?.Invoke(_peer, mode);

    /// <summary>Called by MainWindow when remote peer accepts a call.</summary>
    public void OpenCallWindow(CallMode mode, string myId, string myName)
    {
        if (_callWin != null) return;
        _callWin = new CallWindow(_peer.Ip, _peer.Name, mode, myId, myName);
        _callWin.HangupRequested += () =>
        {
            var endSig = BuildSig(SigType.CallEnd);
            SignalingClient.Send(_peer.Ip, endSig);
            _callWin = null;
        };
        _callWin.Closed += (_, _) => _callWin = null;
        _callWin.Show();
    }

    public void CloseCallWindow()
    {
        _callWin?.DoClose();
        _callWin = null;
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private void AddMessage(ChatMessage m, bool save)
    {
        var vm = new ChatMessageVm(m, _myId);
        _msgs.Add(vm);
        if (save) _store.Append(_convKey, m);
    }

    private ChatMessage BuildOutgoing(MessageKind kind) => new()
    {
        Kind      = kind,
        FromId    = _myId,
        FromName  = _myName,
        IsMine    = true,
        Timestamp = DateTime.Now
    };

    private SigMsg BuildSig(string type) => new()
    {
        Type     = type,
        FromId   = _myId,
        FromName = _myName,
        Ts       = DateTimeOffset.Now.ToUnixTimeMilliseconds()
    };

    private void ScrollToBottom()
    {
        if (_msgs.Count > 0)
            Dispatcher.InvokeAsync(() => Scroller.ScrollToEnd(), System.Windows.Threading.DispatcherPriority.Loaded);
    }

    private static BitmapImage? LoadBitmap(byte[] data)
    {
        try
        {
            var bi = new BitmapImage();
            bi.BeginInit();
            bi.StreamSource   = new MemoryStream(data);
            bi.CacheOption    = BitmapCacheOption.OnLoad;
            bi.EndInit();
            bi.Freeze();
            return bi;
        }
        catch { return null; }
    }

    private static bool IsImage(string? mime) =>
        mime != null && (mime.StartsWith("image/") || mime == "image/gif");

    private static string GuessMime(string ext) => ext.ToLowerInvariant() switch
    {
        ".png"  => "image/png",
        ".jpg" or ".jpeg" => "image/jpeg",
        ".gif"  => "image/gif",
        ".bmp"  => "image/bmp",
        ".mp4"  => "video/mp4",
        ".mkv"  => "video/x-matroska",
        ".mov"  => "video/quicktime",
        ".avi"  => "video/x-msvideo",
        ".pdf"  => "application/pdf",
        _       => "application/octet-stream"
    };

    private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e) =>
        _vnRec.Dispose();
}

// ── View-model wrapper for DataTemplate bindings ──────────────────────────────

public class ChatMessageVm
{
    private readonly ChatMessage _m;
    private readonly string      _myId;

    public ChatMessageVm(ChatMessage m, string myId)
    {
        _m    = m;
        _myId = myId;

        // Build action command
        if (m.Kind == MessageKind.Image || m.Kind == MessageKind.File)
        {
            m.ActionCommand = new RelayCommand(_ =>
            {
                if (m.Data == null) return;
                var defaultName = m.FileName ?? $"file{GuessMimeExt(m.Mime)}";
                var dlg = new Microsoft.Win32.SaveFileDialog
                {
                    FileName   = defaultName,
                    DefaultExt = Path.GetExtension(defaultName),
                    Title      = "Save file"
                };
                if (dlg.ShowDialog() != true) return;
                File.WriteAllBytes(dlg.FileName, m.Data);
                System.Diagnostics.Process.Start(
                    new System.Diagnostics.ProcessStartInfo(dlg.FileName) { UseShellExecute = true });
            });
        }
        else if (m.Kind == MessageKind.VoiceNote)
        {
            m.ActionCommand = new RelayCommand(_ =>
            {
                if (m.Data == null) return;
                Task.Run(() =>
                {
                    try
                    {
                        var ms = new MemoryStream(m.Data);
                        using var reader = new WaveFileReader(ms);
                        using var output = new WaveOutEvent();
                        output.Init(reader);
                        output.Play();
                        while (output.PlaybackState == PlaybackState.Playing)
                            System.Threading.Thread.Sleep(100);
                    }
                    catch { }
                });
            });
        }

        if (m.Kind == MessageKind.Image && m.ImageSource == null && m.Data != null)
        {
            try
            {
                var bi = new BitmapImage();
                bi.BeginInit();
                bi.StreamSource = new MemoryStream(m.Data);
                bi.CacheOption  = BitmapCacheOption.OnLoad;
                bi.EndInit();
                bi.Freeze();
                m.ImageSource = bi;
            }
            catch { }
        }
    }

    public string       Text          => _m.Text ?? "";
    public string       FileName      => _m.FileName ?? "";
    public string       TimeStr       => _m.TimeStr;
    public string       BubbleColor   => _m.BubbleColor;
    public string       NameDisplay   => _m.NameDisplay;
    public ImageSource? ImageSource   => _m.ImageSource;
    public ICommand?    ActionCommand => _m.ActionCommand;

    public HorizontalAlignment BubbleAlign =>
        _m.IsMine ? HorizontalAlignment.Right : HorizontalAlignment.Left;

    public Visibility TextVisibility  => _m.Kind == MessageKind.Text ? Visibility.Visible : Visibility.Collapsed;
    public Visibility ImageVisibility => _m.Kind == MessageKind.Image ? Visibility.Visible : Visibility.Collapsed;
    public Visibility FileVisibility  => _m.Kind == MessageKind.File ? Visibility.Visible : Visibility.Collapsed;
    public Visibility VoiceVisibility => _m.Kind == MessageKind.VoiceNote ? Visibility.Visible : Visibility.Collapsed;
    public Visibility NameVisibility  =>
        string.IsNullOrEmpty(_m.NameDisplay) ? Visibility.Collapsed : Visibility.Visible;

    private static string GuessMimeExt(string? mime) => mime switch
    {
        "image/png"  => ".png",
        "image/jpeg" => ".jpg",
        "image/gif"  => ".gif",
        "video/mp4"  => ".mp4",
        _            => ".bin"
    };
}
