using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using NAudio.Wave;

namespace LocalCallPro;

public partial class MainWindow : Window
{
    // ── Identity ──────────────────────────────────────────────────────────────
    private readonly string _myId;
    private          string _myName;
    private readonly string _localIp;

    // ── Services ──────────────────────────────────────────────────────────────
    private readonly PeerDiscovery   _discovery;
    private readonly SignalingServer _sigServer;
    private readonly FriendManager  _friendMgr;
    private readonly ChatStore       _chatStore;

    // ── Peer / friend lists ───────────────────────────────────────────────────
    private readonly ObservableCollection<PeerInfo>     _peers     = [];
    private readonly ObservableCollection<FriendItemVm> _friendVms = [];

    // ── Active chat state ─────────────────────────────────────────────────────
    private FriendInfo? _activeFriend;
    private GroupInfo?  _activeGroup;
    private readonly ObservableCollection<ChatMessageVm> _chatMsgs  = [];
    private readonly ObservableCollection<ChatMessageVm> _groupMsgs = [];
    private string _chatConvKey  = "";
    private string _groupConvKey = "";

    // ── Voice note recorders ──────────────────────────────────────────────────
    private readonly VoiceNoteRecorder _vnRec      = new();
    private readonly VoiceNoteRecorder _vnRecGroup = new();

    // ── Call windows ─────────────────────────────────────────────────────────
    private CallWindow? _callWin;
    private string?     _pendingCallMode;

    // IDs we've SENT a request to — used only to update the "sent" UI feedback.
    // NO retry loop. SendReliable (3 attempts) handles delivery.
    private readonly HashSet<string> _sentReqIds = [];

    // ═══════════════════════════════════════════════════════════════════════════
    //  STARTUP
    // ═══════════════════════════════════════════════════════════════════════════

    public MainWindow()
    {
        InitializeComponent();

        _myId    = Guid.NewGuid().ToString()[..8];
        _myName  = Helpers.GetFunnyName();
        _localIp = Helpers.GetLocalIp();

        _friendMgr = new FriendManager();
        _chatStore = new ChatStore();

        // Load current friends
        foreach (var f in _friendMgr.Friends)
            _friendVms.Add(new FriendItemVm(f, isFormer: false));

        // Load former friends as greyed-out read-only sidebar entries
        LoadFormerFriends();

        FriendsList.ItemsSource  = _friendVms;
        GroupsList.ItemsSource   = _friendMgr.Groups;
        PeerList.ItemsSource     = _peers;
        RequestsList.ItemsSource = _friendMgr.Pending;
        ChatMsgList.ItemsSource  = _chatMsgs;
        GroupMsgList.ItemsSource = _groupMsgs;

        TxtMyName.Text = $"{_myName}  ·  {_localIp}";
        SyncGroupMembers();
        RefreshRequestsBadge();

        _discovery = new PeerDiscovery(_myId, _myName);
        _discovery.PeersUpdated += OnPeersUpdated;
        _discovery.PeersUpdated += UpdateFriendOnlineStatus;
        _discovery.DiagLog      += msg => Dispatcher.InvokeAsync(() => TxtStatus.Text = msg);
        _discovery.Start();

        _sigServer = new SignalingServer();
        _sigServer.SetIdentity(_myId, _myName);
        _sigServer.MessageReceived += OnSignalReceived;
        _sigServer.Start();

        Closing += (_, _) => { _discovery.Stop(); _sigServer.Stop(); _vnRec.Dispose(); _vnRecGroup.Dispose(); };
    }

    private void LoadFormerFriends()
    {
        foreach (var f in _friendMgr.FormerFriends)
            if (_friendVms.All(v => v.Id != f.Id))
                _friendVms.Add(new FriendItemVm(f, isFormer: true));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  PANEL SWITCHING
    // ═══════════════════════════════════════════════════════════════════════════

    private void ShowDiscover()
    {
        PanelDiscover.Visibility   = Visibility.Visible;
        PanelChat.Visibility       = Visibility.Collapsed;
        PanelGroupChat.Visibility  = Visibility.Collapsed;
        _activeFriend = null;
        _activeGroup  = null;
        FriendsList.SelectedItem = null;
        GroupsList.SelectedItem  = null;
    }

    private void ShowChat(FriendInfo f)
    {
        _activeFriend = f;
        _activeGroup  = null;

        // Clear unread
        f.UnreadCount = 0;
        _friendVms.FirstOrDefault(v => v.Id == f.Id)?.Refresh();

        // Load history once per conversation key
        var key = string.Join("-", new[] { _myId, f.Id }.OrderBy(x => x));
        if (_chatConvKey != key)
        {
            _chatConvKey = key;
            _chatMsgs.Clear();
            Task.Run(() =>
            {
                var history = _chatStore.Load(key);
                Dispatcher.InvokeAsync(() =>
                {
                    foreach (var m in history) _chatMsgs.Add(new ChatMessageVm(m, _myId));
                    ScrollChat();
                });
            });
        }

        TxtChatName.Text   = f.Name;
        ChatStatusDot.Fill = f.IsOnline
            ? new SolidColorBrush(Color.FromRgb(3, 218, 198))
            : new SolidColorBrush(Color.FromRgb(80, 80, 80));

        // Determine if this is a removed friend (read-only) or active friend
        bool isFormer = _friendMgr.FormerFriends.Any(x => x.Id == f.Id);
        SetChatReadOnly(isFormer);

        PanelDiscover.Visibility  = Visibility.Collapsed;
        PanelGroupChat.Visibility = Visibility.Collapsed;
        PanelChat.Visibility      = Visibility.Visible;
        if (!isFormer) TxtChatInput.Focus();
        ScrollChat();
    }

    private void SetChatReadOnly(bool readOnly)
    {
        ChatReadOnlyBanner.Visibility = readOnly ? Visibility.Visible : Visibility.Collapsed;
        ChatToolbar.Visibility        = readOnly ? Visibility.Collapsed : Visibility.Visible;
        ChatInputBar.Visibility       = readOnly ? Visibility.Collapsed : Visibility.Visible;
        BtnChatVoice.IsEnabled        = !readOnly;
        BtnChatVideo.IsEnabled        = !readOnly;
    }

    private void ShowReadOnlyBanner()
    {
        // Keep the chat panel visible but switch to read-only mode
        SetChatReadOnly(true);
    }

    private void ShowGroupChat(GroupInfo g)
    {
        _activeGroup  = g;
        _activeFriend = null;

        var key = $"grp-{g.GroupId}";
        if (_groupConvKey != key)
        {
            _groupConvKey = key;
            _groupMsgs.Clear();
            SyncGroupMembers(g);
            Task.Run(() =>
            {
                var history = _chatStore.Load(key);
                Dispatcher.InvokeAsync(() =>
                {
                    foreach (var m in history) _groupMsgs.Add(new ChatMessageVm(m, _myId));
                    ScrollGroupChat();
                });
            });
        }

        TxtGroupName.Text    = g.Name;
        GrpMemberList.ItemsSource = g.Members.Select(m => $"● {m.Name}").ToList();

        PanelDiscover.Visibility = Visibility.Collapsed;
        PanelChat.Visibility     = Visibility.Collapsed;
        PanelGroupChat.Visibility = Visibility.Visible;
        TxtGroupInput.Focus();
        ScrollGroupChat();
    }

    private void ScrollChat() =>
        Dispatcher.InvokeAsync(() =>
        {
            if (ChatMsgList.Items.Count > 0)
                ChatMsgList.ScrollIntoView(ChatMsgList.Items[^1]);
        }, System.Windows.Threading.DispatcherPriority.Loaded);

    private void ScrollGroupChat() =>
        Dispatcher.InvokeAsync(() =>
        {
            if (GroupMsgList.Items.Count > 0)
                GroupMsgList.ScrollIntoView(GroupMsgList.Items[^1]);
        }, System.Windows.Threading.DispatcherPriority.Loaded);

    // ═══════════════════════════════════════════════════════════════════════════
    //  SIDEBAR CLICKS
    // ═══════════════════════════════════════════════════════════════════════════

    private void FriendsList_Click(object sender, MouseButtonEventArgs e)
    {
        if (FriendsList.SelectedItem is not FriendItemVm vm) return;
        // Open chat for both active and former friends (former = read-only)
        ShowChat(vm.Friend);
    }

    private void GroupsList_Click(object sender, MouseButtonEventArgs e)
    {
        if (GroupsList.SelectedItem is GroupInfo g)
            ShowGroupChat(g);
    }

    private void BtnDiscover_Click(object sender, RoutedEventArgs e) => ShowDiscover();

    private void BtnChatBack_Click(object sender, RoutedEventArgs e)  => ShowDiscover();
    private void BtnGroupBack_Click(object sender, RoutedEventArgs e) => ShowDiscover();

    // ═══════════════════════════════════════════════════════════════════════════
    //  PEER DISCOVERY
    // ═══════════════════════════════════════════════════════════════════════════

    private void OnPeersUpdated(Dictionary<string, PeerInfo> peers)
    {
        Dispatcher.InvokeAsync(() =>
        {
            _peers.Clear();
            foreach (var p in peers.Values)
                if (!_friendMgr.HasFriend(p.Id))
                    _peers.Add(p);

            EmptyState.Visibility = _peers.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            if (_peers.Count > 0)
                TxtStatus.Text = $"✓ {_peers.Count} peer(s) visible.";

            // Clean up _sentReqIds for peers that are now friends
            foreach (var id in _sentReqIds.ToList())
                if (_friendMgr.HasFriend(id)) _sentReqIds.Remove(id);
        });
    }

    private void UpdateFriendOnlineStatus(Dictionary<string, PeerInfo> peers)
    {
        Dispatcher.InvokeAsync(() =>
        {
            foreach (var vm in _friendVms)
            {
                var online = peers.TryGetValue(vm.Id, out var p);
                vm.Friend.IsOnline = online;
                if (online && p != null) _friendMgr.UpdateFriendIp(vm.Id, p.Ip);
                vm.Refresh();
                // live-update status dot if their chat is open
                if (_activeFriend?.Id == vm.Id)
                    ChatStatusDot.Fill = online
                        ? new SolidColorBrush(Color.FromRgb(3, 218, 198))
                        : new SolidColorBrush(Color.FromRgb(80, 80, 80));
            }
        });
    }

    private void BtnRefresh_Click(object sender, RoutedEventArgs e)
    {
        TxtStatus.Text = "Rescanning…";
        _peers.Clear();
        EmptyState.Visibility = Visibility.Visible;
        _discovery.ForceRescan();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  ADD PEER (send friend request)
    // ═══════════════════════════════════════════════════════════════════════════

    private void BtnAddPeer_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as Button)?.Tag is not PeerInfo peer) return;
        if (_friendMgr.HasFriend(peer.Id)) return;
        if (_sentReqIds.Contains(peer.Id)) { TxtStatus.Text = $"Request already sent to {peer.Name}."; return; }

        _sentReqIds.Add(peer.Id);
        // SendReliable = 3 attempts × 600 ms — enough for reliable LAN delivery
        SignalingClient.SendReliable(peer.Ip, new SigMsg
        {
            Type = SigType.FriendReq, FromId = _myId, FromName = _myName,
            Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
        });
        TxtStatus.Text = $"Friend request sent to {peer.Name}…";
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  SIGNALING — dispatch
    // ═══════════════════════════════════════════════════════════════════════════

    private void OnSignalReceived(SigMsg msg, string ip) =>
        Dispatcher.InvokeAsync(() => Dispatch(msg, ip));

    private void Dispatch(SigMsg msg, string ip)
    {
        switch (msg.Type)
        {
            case SigType.FriendReq:  HandleFriendReq(msg, ip);  break;
            case SigType.FriendAcc:  HandleFriendAcc(msg, ip);  break;
            case SigType.FriendRej:  ShowToast("Declined", $"{msg.FromName} declined your request."); break;
            case SigType.FriendDel:  HandleFriendDel(msg.FromId); break;
            case SigType.ChatText:
            case SigType.ChatFile:
            case SigType.ChatVoice:  HandleChatMsg(msg, ip);    break;
            case SigType.CallInv:    HandleCallInv(msg, ip);    break;
            case SigType.CallAcc:    HandleCallAcc(msg);        break;
            case SigType.CallRej:    ShowToast("Declined", $"{msg.FromName} declined the call."); break;
            case SigType.CallEnd:    _callWin?.DoClose(); _callWin = null; break;
            case SigType.GrpInv:     HandleGrpInv(msg, ip);    break;
            case SigType.GrpLeave:   HandleGrpLeave(msg);      break;
            case SigType.GrpText:
            case SigType.GrpFile:
            case SigType.GrpVoice:   HandleGroupMsg(msg);      break;
            case SigType.GrpKick:    HandleGrpKick(msg);       break;
            case SigType.GrpDelete:  HandleGrpDelete(msg);     break;
            case SigType.GrpPromote: HandleGrpPromote(msg);    break;
            case SigType.GrpDemote:  HandleGrpDemote(msg);     break;
            case SigType.GrpPerm:    HandleGrpPerm(msg);       break;
            case SigType.GrpAddMember: HandleGrpAddMember(msg, ip); break;
        }
    }

    // ── Friend flow ──────────────────────────────────────────────────────────

    private void HandleFriendReq(SigMsg msg, string ip)
    {
        // Blocked = previously declined or removed — silently ignore
        if (_friendMgr.IsBlocked(msg.FromId)) return;

        // Already a friend — they may have missed our FriendAcc, resend it silently
        if (_friendMgr.HasFriend(msg.FromId))
        {
            SignalingClient.Send(ip, new SigMsg
            {
                Type = SigType.FriendAcc, FromId = _myId, FromName = _myName,
                Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
            });
            return;
        }

        // Already in pending inbox — silently ignore duplicates
        if (_friendMgr.HasPending(msg.FromId)) return;

        // Store in persistent inbox so user sees it even if they dismiss the popup
        var req = new PendingRequest { FromId = msg.FromId, FromName = msg.FromName, FromIp = ip };
        _friendMgr.AddPending(req);
        RefreshRequestsBadge();

        // Show a one-time toast (non-blocking, no retry)
        ShowToast("Friend Request", $"{msg.FromName} wants to connect — see Requests.");
    }

    private void HandleFriendAcc(SigMsg msg, string ip)
    {
        // Already added (duplicate FriendAcc) — ignore silently
        if (_friendMgr.HasFriend(msg.FromId)) return;

        _sentReqIds.Remove(msg.FromId);
        var f = new FriendInfo { Id = msg.FromId, Name = msg.FromName, Ip = ip };
        CommitAddFriend(f);
        ShowToast("Friend added! 🎉", $"{msg.FromName} accepted your request.");
    }

    // ── Request inbox (sidebar Accept / Decline buttons) ─────────────────────

    private void BtnAcceptRequest_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as Button)?.Tag is not PendingRequest req) return;
        _friendMgr.RemovePending(req.FromId);
        RefreshRequestsBadge();
        var f = new FriendInfo { Id = req.FromId, Name = req.FromName, Ip = req.FromIp };
        CommitAddFriend(f);
        // Send FriendAcc — reliable (3 attempts) so User1 definitely gets it
        SignalingClient.SendReliable(req.FromIp, new SigMsg
        {
            Type = SigType.FriendAcc, FromId = _myId, FromName = _myName,
            Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
        });
    }

    private void BtnDeclineRequest_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as Button)?.Tag is not PendingRequest req) return;
        _friendMgr.RemovePending(req.FromId);
        // Block so repeated requests from this peer are silently ignored
        _friendMgr.Block(req.FromId);
        RefreshRequestsBadge();
        SignalingClient.Send(req.FromIp, new SigMsg
        {
            Type = SigType.FriendRej, FromId = _myId, FromName = _myName,
            Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
        });
    }

    // ── Shared commit helper ──────────────────────────────────────────────────

    private void CommitAddFriend(FriendInfo f)
    {
        _friendMgr.AddFriend(f);

        // If they exist as former (greyed), replace with active VM
        var existingVm = _friendVms.FirstOrDefault(v => v.Id == f.Id);
        if (existingVm != null)
        {
            var idx = _friendVms.IndexOf(existingVm);
            _friendVms[idx] = new FriendItemVm(f, isFormer: false);
        }
        else
        {
            _friendVms.Add(new FriendItemVm(f, isFormer: false));
        }

        // Remove from discover list if visible
        var old = _peers.FirstOrDefault(p => p.Id == f.Id);
        if (old != null) _peers.Remove(old);
        RefreshRequestsBadge();
    }

    private void AddFriendToList(FriendInfo f) => CommitAddFriend(f); // keep old callers working

    // ── Badge helper ──────────────────────────────────────────────────────────

    private void RefreshRequestsBadge()
    {
        var count = _friendMgr.Pending.Count;
        RequestsSection.Visibility = count > 0 ? Visibility.Visible : Visibility.Collapsed;
        TxtRequestCount.Text       = count.ToString();
    }

    private void HandleFriendDel(string fromId)
    {
        _friendMgr.RemoveFriend(fromId);  // saves FormerFriends + BlockedIds

        // Downgrade their sidebar entry to former (greyed-out) instead of removing
        var vm = _friendVms.FirstOrDefault(v => v.Id == fromId);
        if (vm != null)
        {
            var idx = _friendVms.IndexOf(vm);
            if (idx >= 0)
                _friendVms[idx] = new FriendItemVm(vm.Friend, isFormer: true);
        }

        // If their chat is currently open, make it read-only
        if (_activeFriend?.Id == fromId)
            ShowReadOnlyBanner();

        ShowToast("Removed", "A friend removed you. Chat history is now read-only.");
    }

    // ── Chat messages ─────────────────────────────────────────────────────────

    private void HandleChatMsg(SigMsg msg, string ip)
    {
        _friendMgr.UpdateFriendIp(msg.FromId, ip);
        var friend = _friendMgr.GetFriend(msg.FromId);
        if (friend == null) return;

        var cm = SigToMessage(msg, false);

        if (_activeFriend?.Id == msg.FromId)
        {
            // Chat is open — append directly
            var key = string.Join("-", new[] { _myId, msg.FromId }.OrderBy(x => x));
            _chatMsgs.Add(new ChatMessageVm(cm, _myId));
            _chatStore.Append(key, cm);
            ScrollChat();
        }
        else
        {
            // Chat not open — bump unread badge + toast
            friend.UnreadCount++;
            _friendVms.FirstOrDefault(v => v.Id == msg.FromId)?.Refresh();
            ShowToast($"{msg.FromName}", msg.Text ?? "📎 attachment");
            // Still persist to history
            var key = string.Join("-", new[] { _myId, msg.FromId }.OrderBy(x => x));
            _chatStore.Append(key, cm);
        }
    }

    // ── Calls ─────────────────────────────────────────────────────────────────

    private void HandleCallInv(SigMsg msg, string ip)
    {
        _friendMgr.UpdateFriendIp(msg.FromId, ip);
        var mode = msg.Mode ?? "voice";
        new NotificationWindow("Incoming call",
            $"{msg.FromName} is calling ({mode}).",
            [
                ("Answer", () =>
                {
                    SignalingClient.Send(ip, new SigMsg
                    {
                        Type = SigType.CallAcc, FromId = _myId, FromName = _myName,
                        Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                    });
                    OpenCallWindow(ip, msg.FromName,
                        mode == "video" ? CallMode.VideoCamera : CallMode.Voice);
                }),
                ("Decline", () => SignalingClient.Send(ip, new SigMsg
                {
                    Type = SigType.CallRej, FromId = _myId, FromName = _myName,
                    Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                }))
            ]).Show();
    }

    private void HandleCallAcc(SigMsg msg)
    {
        var f = _friendMgr.GetFriend(msg.FromId);
        if (f == null) return;
        OpenCallWindow(f.Ip, f.Name, _pendingCallMode == "video" ? CallMode.VideoCamera : CallMode.Voice);
    }

    private void OpenCallWindow(string ip, string name, CallMode mode)
    {
        if (_callWin != null) return;
        _callWin = new CallWindow(ip, name, mode, _myId, _myName);
        _callWin.HangupRequested += () =>
        {
            var f = _friendMgr.Friends.FirstOrDefault(x => x.Ip == ip);
            if (f != null)
                SignalingClient.Send(ip, new SigMsg
                {
                    Type = SigType.CallEnd, FromId = _myId, FromName = _myName,
                    Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                });
            _callWin = null;
        };
        _callWin.Closed += (_, _) => _callWin = null;
        _callWin.Show();
    }

    // ── Group management ──────────────────────────────────────────────────────

    private void HandleGrpInv(SigMsg msg, string ip)
    {
        new NotificationWindow("Group invite",
            $"{msg.FromName} invited you to \"{msg.GroupName}\".",
            [
                ("Join", () =>
                {
                    var g = new GroupInfo { GroupId = msg.GroupId!, Name = msg.GroupName!, OwnerId = msg.OwnerId ?? msg.FromId };
                    if (msg.Members != null)
                        foreach (var m in msg.Members)
                        {
                            g.MemberIds.Add(m.Id);
                            var f = _friendMgr.GetFriend(m.Id);
                            if (f != null) g.Members.Add(f);
                        }
                    _friendMgr.AddGroup(g);
                    SignalingClient.Send(ip, new SigMsg
                    {
                        Type = SigType.GrpAcc, FromId = _myId, FromName = _myName,
                        GroupId = g.GroupId, GroupName = g.Name,
                        Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                    });
                }),
                ("Decline", null)
            ]).Show();
    }

    private void HandleGrpLeave(SigMsg msg)
    {
        if (msg.GroupId == null) return;
        var g = _friendMgr.Groups.FirstOrDefault(x => x.GroupId == msg.GroupId);
        if (g == null) return;
        g.MemberIds.Remove(msg.FromId);
        g.Members.RemoveAll(m => m.Id == msg.FromId);
        if (_activeGroup?.GroupId == msg.GroupId)
            GrpMemberList.ItemsSource = g.Members.Select(m => $"● {m.Name}").ToList();
    }

    private void HandleGroupMsg(SigMsg msg)
    {
        if (msg.GroupId == null) return;

        // Check permissions before showing message
        var grp = _friendMgr.Groups.FirstOrDefault(x => x.GroupId == msg.GroupId);
        if (grp != null)
        {
            var perms = grp.GetPermissions(msg.FromId);
            if (msg.Type == SigType.GrpFile  && !perms.CanSendFiles)    return;
            if (msg.Type == SigType.GrpText  && !perms.CanSendMessages) return;
        }

        var cm  = SigToMessage(msg, false);
        var key = $"grp-{msg.GroupId}";
        _chatStore.Append(key, cm);

        if (_activeGroup?.GroupId == msg.GroupId)
        {
            _groupMsgs.Add(new ChatMessageVm(cm, _myId));
            ScrollGroupChat();
        }
        else
            ShowToast($"[Group] {msg.FromName}", msg.Text ?? "📎 attachment");
    }

    private void HandleGrpKick(SigMsg msg)
    {
        if (msg.GroupId == null) return;
        if (msg.TargetId == _myId)
        {
            // We were kicked
            var g = _friendMgr.Groups.FirstOrDefault(x => x.GroupId == msg.GroupId);
            _friendMgr.RemoveGroup(msg.GroupId);
            if (_activeGroup?.GroupId == msg.GroupId) ShowDiscover();
            ShowToast("Removed from group", $"You were removed from \"{g?.Name}\".");
        }
        else
        {
            // Someone else was kicked — update local group state
            var g = _friendMgr.Groups.FirstOrDefault(x => x.GroupId == msg.GroupId);
            if (g == null) return;
            g.MemberIds.Remove(msg.TargetId);
            g.Members.RemoveAll(m => m.Id == msg.TargetId);
            if (_activeGroup?.GroupId == msg.GroupId)
                GrpMemberList.ItemsSource = g.Members.Select(m => $"● {m.Name}").ToList();
        }
    }

    private void HandleGrpDelete(SigMsg msg)
    {
        if (msg.GroupId == null) return;
        var g = _friendMgr.Groups.FirstOrDefault(x => x.GroupId == msg.GroupId);
        _friendMgr.RemoveGroup(msg.GroupId);
        if (_activeGroup?.GroupId == msg.GroupId) ShowDiscover();
        ShowToast("Group deleted", $"The group \"{g?.Name}\" was deleted by the owner.");
    }

    private void HandleGrpPromote(SigMsg msg)
    {
        if (msg.GroupId == null || msg.TargetId == null) return;
        var g = _friendMgr.Groups.FirstOrDefault(x => x.GroupId == msg.GroupId);
        if (g == null) return;
        if (!g.HelperIds.Contains(msg.TargetId)) g.HelperIds.Add(msg.TargetId);
        _friendMgr.SaveGroupsPublic();
        if (msg.TargetId == _myId)
            ShowToast("Promoted!", $"You are now a helper in \"{g.Name}\".");
    }

    private void HandleGrpDemote(SigMsg msg)
    {
        if (msg.GroupId == null || msg.TargetId == null) return;
        var g = _friendMgr.Groups.FirstOrDefault(x => x.GroupId == msg.GroupId);
        if (g == null) return;
        g.HelperIds.Remove(msg.TargetId);
        _friendMgr.SaveGroupsPublic();
    }

    private void HandleGrpPerm(SigMsg msg)
    {
        if (msg.GroupId == null || msg.TargetId == null) return;
        var g = _friendMgr.Groups.FirstOrDefault(x => x.GroupId == msg.GroupId);
        if (g == null) return;
        if (!g.Permissions.ContainsKey(msg.TargetId))
            g.Permissions[msg.TargetId] = new GroupPermissions();
        var p = g.Permissions[msg.TargetId];
        if (msg.PermMsg  != null) p.CanSendMessages = msg.PermMsg.Value;
        if (msg.PermFile != null) p.CanSendFiles    = msg.PermFile.Value;
        if (msg.PermCall != null) p.CanStartCalls   = msg.PermCall.Value;
        _friendMgr.SaveGroupsPublic();
    }

    private void HandleGrpAddMember(SigMsg msg, string ip)
    {
        // Re-use GrpInv path
        HandleGrpInv(msg, ip);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  CHAT INPUT HANDLERS
    // ═══════════════════════════════════════════════════════════════════════════

    private void TxtChatInput_KeyDown(object s, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) { SendChatText(); e.Handled = true; }
    }
    private void BtnChatSend_Click(object s, RoutedEventArgs e) => SendChatText();

    private void SendChatText()
    {
        var text = TxtChatInput.Text.Trim();
        if (string.IsNullOrEmpty(text) || _activeFriend == null) return;
        // Don't send if no longer friends
        if (!_friendMgr.HasFriend(_activeFriend.Id)) return;
        TxtChatInput.Clear();
        var sig = BuildSig(SigType.ChatText); sig.Text = text;
        SignalingClient.Send(_activeFriend.Ip, sig);
        var cm = new ChatMessage { Kind = MessageKind.Text, FromId = _myId, FromName = _myName,
            IsMine = true, Text = text, Timestamp = DateTime.Now };
        _chatMsgs.Add(new ChatMessageVm(cm, _myId));
        _chatStore.Append(_chatConvKey, cm);
        ScrollChat();
    }

    private void BtnChatSendImage_Click(object s, RoutedEventArgs e) => SendFile(isGroup: false, imagesOnly: true);
    private void BtnChatSendFile_Click(object s, RoutedEventArgs e)  => SendFile(isGroup: false, imagesOnly: false);

    private void BtnVoiceNote_MouseDown(object s, MouseButtonEventArgs e)
    {
        _vnRec.Start();
        TxtRecording.Visibility = Visibility.Visible;
    }
    private void BtnVoiceNote_MouseUp(object s, MouseButtonEventArgs e)
    {
        TxtRecording.Visibility = Visibility.Collapsed;
        var wav = _vnRec.Stop();
        if (wav.Length < 100 || _activeFriend == null) return;
        var sig = BuildSig(SigType.ChatVoice); sig.Data = Convert.ToBase64String(wav);
        SignalingClient.Send(_activeFriend.Ip, sig);
        var cm = new ChatMessage { Kind = MessageKind.VoiceNote, FromId = _myId, FromName = _myName,
            IsMine = true, FileName = "voice_note.wav", Data = wav, Timestamp = DateTime.Now };
        _chatMsgs.Add(new ChatMessageVm(cm, _myId));
        _chatStore.Append(_chatConvKey, cm);
        ScrollChat();
    }

    private void BtnChatVoice_Click(object s, RoutedEventArgs e)
    {
        if (_activeFriend == null) return;
        SendCallInvite(_activeFriend, "voice");
    }
    private void BtnChatVideo_Click(object s, RoutedEventArgs e)
    {
        if (_activeFriend == null) return;
        SendCallInvite(_activeFriend, "video");
    }

    private void SendCallInvite(FriendInfo f, string mode)
    {
        if (!f.IsOnline) { MessageBox.Show($"{f.Name} is offline."); return; }
        _pendingCallMode = mode;
        SignalingClient.Send(f.Ip, new SigMsg
        {
            Type = SigType.CallInv, FromId = _myId, FromName = _myName, Mode = mode,
            Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
        });
        ShowToast("Calling…", $"Waiting for {f.Name}…");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  GROUP CHAT INPUT HANDLERS
    // ═══════════════════════════════════════════════════════════════════════════

    private void TxtGroupInput_KeyDown(object s, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) { SendGroupText(); e.Handled = true; }
    }
    private void BtnGroupSend_Click(object s, RoutedEventArgs e) => SendGroupText();

    private void SendGroupText()
    {
        var text = TxtGroupInput.Text.Trim();
        if (string.IsNullOrEmpty(text) || _activeGroup == null) return;

        // Check own permissions
        var myPerms = _activeGroup.GetPermissions(_myId);
        if (!myPerms.CanSendMessages && !_activeGroup.IsOwner(_myId) && !_activeGroup.IsHelper(_myId))
        { ShowToast("Restricted", "You cannot send messages in this group."); return; }

        TxtGroupInput.Clear();
        var sig = BuildSig(SigType.GrpText);
        sig.Text    = text;
        sig.GroupId = _activeGroup.GroupId;
        BroadcastToGroup(_activeGroup, sig);
        var cm = new ChatMessage { Kind = MessageKind.Text, FromId = _myId, FromName = _myName,
            IsMine = true, Text = text, Timestamp = DateTime.Now };
        _groupMsgs.Add(new ChatMessageVm(cm, _myId));
        _chatStore.Append(_groupConvKey, cm);
        ScrollGroupChat();
    }

    private void BtnGroupSendImage_Click(object s, RoutedEventArgs e) => SendFile(isGroup: true, imagesOnly: true);
    private void BtnGroupSendFile_Click(object s, RoutedEventArgs e)  => SendFile(isGroup: true, imagesOnly: false);

    private void BtnGroupVoiceNote_MouseDown(object s, MouseButtonEventArgs e)
    {
        _vnRecGroup.Start();
        TxtGroupRecording.Visibility = Visibility.Visible;
    }
    private void BtnGroupVoiceNote_MouseUp(object s, MouseButtonEventArgs e)
    {
        TxtGroupRecording.Visibility = Visibility.Collapsed;
        var wav = _vnRecGroup.Stop();
        if (wav.Length < 100 || _activeGroup == null) return;
        var sig = BuildSig(SigType.GrpVoice);
        sig.Data    = Convert.ToBase64String(wav);
        sig.GroupId = _activeGroup.GroupId;
        BroadcastToGroup(_activeGroup, sig);
        var cm = new ChatMessage { Kind = MessageKind.VoiceNote, FromId = _myId, FromName = _myName,
            IsMine = true, FileName = "voice_note.wav", Data = wav, Timestamp = DateTime.Now };
        _groupMsgs.Add(new ChatMessageVm(cm, _myId));
        _chatStore.Append(_groupConvKey, cm);
        ScrollGroupChat();
    }

    private void BtnGroupCall_Click(object s, RoutedEventArgs e)
    {
        if (_activeGroup == null) return;
        foreach (var mid in _activeGroup.MemberIds)
        {
            var f = _friendMgr.GetFriend(mid);
            if (f?.IsOnline == true)
                SignalingClient.Send(f.Ip, new SigMsg
                {
                    Type = SigType.GrpCallInv, FromId = _myId, FromName = _myName,
                    GroupId = _activeGroup.GroupId, GroupName = _activeGroup.Name,
                    Mode = "voice", Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                });
        }
        ShowToast("Group call started", "Invites sent to all online members.");
    }
    private void BtnGroupVideo_Click(object s, RoutedEventArgs e) => BtnGroupCall_Click(s, e);

    // ── Send file / image ─────────────────────────────────────────────────────

    private void SendFile(bool isGroup, bool imagesOnly)
    {
        var dlg = new OpenFileDialog
        {
            Filter = imagesOnly
                ? "Images & Videos|*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.mp4;*.mkv;*.mov|All|*.*"
                : "All Files|*.*",
            Title = imagesOnly ? "Select image or video" : "Select file to send"
        };
        if (dlg.ShowDialog() != true) return;
        var info = new FileInfo(dlg.FileName);
        if (info.Length > MediaSettings.FileMaxBytes)
        { MessageBox.Show("File too large (max 50 MB)."); return; }

        byte[] data;
        try { data = File.ReadAllBytes(dlg.FileName); }
        catch { MessageBox.Show("Could not read file."); return; }

        var mime = GuessMime(info.Extension);
        var kind = mime.StartsWith("image/") ? MessageKind.Image : MessageKind.File;

        var cm = new ChatMessage
        {
            Kind = kind, FromId = _myId, FromName = _myName, IsMine = true,
            FileName = info.Name, Mime = mime, Data = data, Timestamp = DateTime.Now
        };

        if (isGroup && _activeGroup != null)
        {
            var sig = BuildSig(SigType.GrpFile);
            sig.FileName = info.Name; sig.Mime = mime;
            sig.Data = Convert.ToBase64String(data);
            sig.GroupId = _activeGroup.GroupId;
            BroadcastToGroup(_activeGroup, sig);
            _groupMsgs.Add(new ChatMessageVm(cm, _myId));
            _chatStore.Append(_groupConvKey, cm);
            ScrollGroupChat();
        }
        else if (!isGroup && _activeFriend != null)
        {
            var sig = BuildSig(SigType.ChatFile);
            sig.FileName = info.Name; sig.Mime = mime;
            sig.Data = Convert.ToBase64String(data);
            SignalingClient.Send(_activeFriend.Ip, sig);
            _chatMsgs.Add(new ChatMessageVm(cm, _myId));
            _chatStore.Append(_chatConvKey, cm);
            ScrollChat();
        }
    }

    // ── Broadcast to group ────────────────────────────────────────────────────

    private void BroadcastToGroup(GroupInfo g, SigMsg sig)
    {
        foreach (var mid in g.MemberIds)
        {
            var f = _friendMgr.GetFriend(mid);
            if (f != null && !string.IsNullOrEmpty(f.Ip))
                SignalingClient.Send(f.Ip, sig);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  CONTEXT MENU HANDLERS
    // ═══════════════════════════════════════════════════════════════════════════

    private void CtxOpenChat_Click(object s, RoutedEventArgs e)
    { if (FriendsList.SelectedItem is FriendItemVm vm) ShowChat(vm.Friend); }

    private void CtxVoiceCall_Click(object s, RoutedEventArgs e)
    {
        if (FriendsList.SelectedItem is FriendItemVm vm && !vm.IsFormer)
            SendCallInvite(vm.Friend, "voice");
    }

    private void CtxVideoCall_Click(object s, RoutedEventArgs e)
    {
        if (FriendsList.SelectedItem is FriendItemVm vm && !vm.IsFormer)
            SendCallInvite(vm.Friend, "video");
    }

    private void CtxRemoveFriend_Click(object s, RoutedEventArgs e)
    {
        if (FriendsList.SelectedItem is not FriendItemVm vm) return;
        if (vm.IsFormer) return; // already removed — nothing to do

        if (MessageBox.Show(
            $"Remove {vm.Name}?\n\nYou'll keep your chat history (read-only). To chat again, you'll need to send a new friend request.",
            "Remove friend", MessageBoxButton.YesNo, MessageBoxImage.Warning)
            != MessageBoxResult.Yes) return;

        if (!string.IsNullOrEmpty(vm.Ip))
            SignalingClient.Send(vm.Ip, new SigMsg
            {
                Type = SigType.FriendDel, FromId = _myId, FromName = _myName,
                Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
            });

        _friendMgr.RemoveFriend(vm.Id);  // saves to FormerFriends + BlockedIds

        // Downgrade the existing VM to a former (greyed-out) entry — don't remove it
        var idx = _friendVms.IndexOf(vm);
        if (idx >= 0)
            _friendVms[idx] = new FriendItemVm(vm.Friend, isFormer: true);

        // If their chat is open, make it read-only but keep it visible
        if (_activeFriend?.Id == vm.Id)
            ShowReadOnlyBanner();
    }

    private void CtxOpenGroupChat_Click(object s, RoutedEventArgs e)
    { if (GroupsList.SelectedItem is GroupInfo g) ShowGroupChat(g); }

    private void CtxGroupCall_Click(object s, RoutedEventArgs e)
    {
        if (GroupsList.SelectedItem is GroupInfo g)
        {
            _activeGroup = g;
            BtnGroupCall_Click(s, e);
        }
    }

    private void CtxLeaveGroup_Click(object s, RoutedEventArgs e)
    {
        if (GroupsList.SelectedItem is not GroupInfo g) return;
        foreach (var mid in g.MemberIds)
        {
            var f = _friendMgr.GetFriend(mid);
            if (f != null)
                SignalingClient.Send(f.Ip, new SigMsg
                {
                    Type = SigType.GrpLeave, FromId = _myId, FromName = _myName,
                    GroupId = g.GroupId, Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                });
        }
        _friendMgr.RemoveGroup(g.GroupId);
        if (_activeGroup?.GroupId == g.GroupId) ShowDiscover();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  GROUPS — create
    // ═══════════════════════════════════════════════════════════════════════════

    private void BtnNewGroup_Click(object s, RoutedEventArgs e)
    {
        var online = _friendVms
            .Where(f => !f.IsFormer && f.Friend.IsOnline)
            .Select(f => f.Friend)
            .ToList();

        if (online.Count == 0)
        {
            MessageBox.Show("You need at least one online friend to create a group.");
            return;
        }

        var dlg = new GroupCreateDialog(online, this);
        if (dlg.ShowDialog() != true) return;

        var g = new GroupInfo
        {
            Name      = dlg.GroupName,
            OwnerId   = _myId,
            MemberIds = dlg.Selected.Select(f => f.Id).ToList(),
            Members   = dlg.Selected
        };
        _friendMgr.AddGroup(g);

        foreach (var f in dlg.Selected)
            SignalingClient.Send(f.Ip, new SigMsg
            {
                Type      = SigType.GrpInv,
                FromId    = _myId,
                FromName  = _myName,
                GroupId   = g.GroupId,
                GroupName = g.Name,
                OwnerId   = g.OwnerId,
                Members   = g.Members.Select(m => new MemberDto { Id = m.Id, Name = m.Name, Ip = m.Ip }).ToList(),
                Ts        = DateTimeOffset.Now.ToUnixTimeMilliseconds()
            });
    }

    private void CtxManageGroup_Click(object s, RoutedEventArgs e)
    {
        if (GroupsList.SelectedItem is not GroupInfo g) return;
        SyncGroupMembers(g);

        var dlg = new GroupManageDialog(g, _myId,
            _friendMgr.Friends.ToList(), this);
        dlg.ShowDialog();

        // Execute any queued signaling actions
        foreach (var act in dlg.PendingActions)
            act();

        // If owner deleted the group
        if (dlg.Tag as string == "deleted")
        {
            _friendMgr.RemoveGroup(g.GroupId);
            if (_activeGroup?.GroupId == g.GroupId) ShowDiscover();
            return;
        }

        // Persist updated group (permissions, members changed)
        _friendMgr.SaveGroupsPublic();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  PROFILE
    // ═══════════════════════════════════════════════════════════════════════════

    private void BtnEditProfile_Click(object s, RoutedEventArgs e)
    {
        var dlg = new InputDialog("Edit Profile", "Display name:", _myName) { Owner = this };
        if (dlg.ShowDialog() != true || string.IsNullOrWhiteSpace(dlg.Result)) return;
        _myName = dlg.Result;
        _discovery.UpdateName(_myName);
        _sigServer.SetIdentity(_myId, _myName);
        TxtMyName.Text = $"{_myName}  ·  {_localIp}";
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  HELPERS
    // ═══════════════════════════════════════════════════════════════════════════

    private SigMsg BuildSig(string type) => new()
    {
        Type = type, FromId = _myId, FromName = _myName,
        Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
    };

    private static ChatMessage SigToMessage(SigMsg sig, bool isMine) => new()
    {
        Kind      = sig.Type == SigType.ChatVoice || sig.Type == SigType.GrpVoice
                    ? MessageKind.VoiceNote
                    : (sig.Type == SigType.ChatFile || sig.Type == SigType.GrpFile)
                        ? (sig.Mime?.StartsWith("image/") == true ? MessageKind.Image : MessageKind.File)
                        : MessageKind.Text,
        FromId    = sig.FromId,
        FromName  = sig.FromName,
        Text      = sig.Text,
        FileName  = sig.FileName,
        Mime      = sig.Mime,
        Data      = sig.Data != null ? Convert.FromBase64String(sig.Data) : null,
        IsMine    = isMine,
        Timestamp = DateTimeOffset.FromUnixTimeMilliseconds(sig.Ts).LocalDateTime
    };

    private static void ShowToast(string title, string body) =>
        new NotificationWindow(title, body, autoCloseSec: 4).Show();

    private void SyncGroupMembers(GroupInfo? single = null)
    {
        var groups = single != null ? new[] { single } : _friendMgr.Groups.ToArray();
        foreach (var g in groups)
        {
            g.Members.Clear();
            foreach (var id in g.MemberIds)
            {
                var f = _friendMgr.GetFriend(id);
                if (f != null) g.Members.Add(f);
            }
        }
    }

    private static string GuessMime(string ext) => ext.ToLowerInvariant() switch
    {
        ".png" => "image/png", ".jpg" or ".jpeg" => "image/jpeg",
        ".gif" => "image/gif", ".bmp" => "image/bmp",
        ".mp4" => "video/mp4", ".mkv" => "video/x-matroska",
        ".mov" => "video/quicktime", _ => "application/octet-stream"
    };

    // ═══════════════════════════════════════════════════════════════════════════
    //  DARK TITLE BAR
    // ═══════════════════════════════════════════════════════════════════════════

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(
        IntPtr hwnd, int attr, ref int attrValue, int attrSize);
    private const int DWMWA_DARK = 20;

    private void Window_SourceInitialized(object sender, EventArgs e)
    {
        try
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            int v = 1;
            DwmSetWindowAttribute(hwnd, DWMWA_DARK, ref v, Marshal.SizeOf(v));
        }
        catch { }

        try
        {
            var path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "icon.ico");
            if (!File.Exists(path))
                path = Path.Combine(Directory.GetCurrentDirectory(), "icon.ico");
            if (File.Exists(path))
                Icon = new BitmapImage(new Uri(path, UriKind.Absolute));
        }
        catch { }
    }
}
