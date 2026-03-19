using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;

namespace LocalCallPro;

public partial class GroupManageDialog : Window
{
    private readonly GroupInfo         _group;
    private readonly string            _myId;
    private readonly List<FriendInfo>  _allFriends;

    // Actions queued by the dialog — executed by MainWindow after Close
    public List<Action> PendingActions { get; } = [];

    public GroupManageDialog(GroupInfo group, string myId, List<FriendInfo> allFriends, Window owner)
    {
        InitializeComponent();
        Owner       = owner;
        _group      = group;
        _myId       = myId;
        _allFriends = allFriends;

        TxtGroupTitle.Text = group.Name;
        bool isOwner = group.IsOwner(myId);
        TxtOwnerNote.Text  = isOwner ? "You are the owner" : group.IsHelper(myId) ? "You are a helper" : "Member";

        if (isOwner) { DangerRow.Visibility = Visibility.Visible; AddMemberRow.Visibility = Visibility.Visible; }

        // Populate add-member combo with friends NOT already in the group
        var notInGroup = allFriends
            .Where(f => f.IsOnline && !group.MemberIds.Contains(f.Id))
            .ToList();
        CmbAddMember.ItemsSource  = notInGroup;
        CmbAddMember.DisplayMemberPath = "Name";

        Rebuild();
    }

    private void Rebuild()
    {
        MemberRows.Children.Clear();
        bool actorIsOwner  = _group.IsOwner(_myId);
        bool actorIsHelper = _group.IsHelper(_myId);

        foreach (var memberId in _group.MemberIds)
        {
            var friend = _allFriends.FirstOrDefault(f => f.Id == memberId);
            if (friend == null) continue;

            var perms = _group.GetPermissions(memberId);
            bool isOwner  = _group.IsOwner(memberId);
            bool isHelper = _group.IsHelper(memberId);
            bool canAct   = _group.CanManage(_myId, memberId) && memberId != _myId;
            bool isMe     = memberId == _myId;

            // Row container
            var row = new Border
            {
                Background = isOwner ? System.Windows.Media.Brushes.Transparent : System.Windows.Media.Brushes.Transparent,
                Padding    = new Thickness(8, 6, 8, 6),
                BorderBrush   = new System.Windows.Media.SolidColorBrush(
                    System.Windows.Media.Color.FromRgb(30, 30, 46)),
                BorderThickness = new Thickness(0, 0, 0, 1)
            };
            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            // Name + role badge
            var badge    = isOwner ? " 👑" : isHelper ? " 🛡" : isMe ? " (you)" : "";
            var nameTxt  = new TextBlock
            {
                Text       = friend.Name + badge,
                FontSize   = 13,
                Foreground = System.Windows.Media.Brushes.LightGray,
                VerticalAlignment = VerticalAlignment.Center,
                Margin     = new Thickness(0, 0, 8, 0)
            };
            Grid.SetColumn(nameTxt, 1);
            grid.Children.Add(nameTxt);

            // Permissions checkboxes (if actor can manage this member)
            if (canAct)
            {
                var permPanel = new StackPanel { Orientation = Orientation.Horizontal };
                permPanel.Children.Add(MakePermCheck("💬", perms.CanSendMessages, v =>
                {
                    _group.GetPermissions(memberId).CanSendMessages = v;
                    QueuePermChange(memberId);
                }));
                permPanel.Children.Add(MakePermCheck("📎", perms.CanSendFiles, v =>
                {
                    _group.GetPermissions(memberId).CanSendFiles = v;
                    QueuePermChange(memberId);
                }));
                permPanel.Children.Add(MakePermCheck("📞", perms.CanStartCalls, v =>
                {
                    _group.GetPermissions(memberId).CanStartCalls = v;
                    QueuePermChange(memberId);
                }));
                Grid.SetColumn(permPanel, 2);
                grid.Children.Add(permPanel);

                // Action buttons for owner
                if (actorIsOwner && !isOwner)
                {
                    var btnPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(8, 0, 0, 0) };

                    if (!isHelper)
                    {
                        var promote = MakeBtn("🛡 Promote", () =>
                        {
                            _group.HelperIds.Add(memberId);
                            QueueSignal(() => PendingActions.Add(() =>
                                SignalingClient.Send(friend.Ip, new SigMsg
                                {
                                    Type = SigType.GrpPromote, GroupId = _group.GroupId,
                                    TargetId = memberId,
                                    FromId = _myId, FromName = "",
                                    Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                                })));
                            Rebuild();
                        });
                        btnPanel.Children.Add(promote);
                    }
                    else
                    {
                        var demote = MakeBtn("⬇ Demote", () =>
                        {
                            _group.HelperIds.Remove(memberId);
                            QueueSignal(() => PendingActions.Add(() =>
                                SignalingClient.Send(friend.Ip, new SigMsg
                                {
                                    Type = SigType.GrpDemote, GroupId = _group.GroupId,
                                    TargetId = memberId,
                                    FromId = _myId, FromName = "",
                                    Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                                })));
                            Rebuild();
                        });
                        btnPanel.Children.Add(demote);
                    }

                    var kick = MakeBtn("✕ Kick", () =>
                    {
                        if (MessageBox.Show($"Remove {friend.Name} from the group?",
                            "Kick member", MessageBoxButton.YesNo) != MessageBoxResult.Yes) return;
                        _group.MemberIds.Remove(memberId);
                        _group.Members.RemoveAll(m => m.Id == memberId);
                        QueueSignal(() => PendingActions.Add(() =>
                            SignalingClient.Send(friend.Ip, new SigMsg
                            {
                                Type = SigType.GrpKick, GroupId = _group.GroupId,
                                TargetId = memberId,
                                FromId = _myId, FromName = "",
                                Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
                            })));
                        Rebuild();
                    }, isBad: true);
                    btnPanel.Children.Add(kick);

                    // Append button panel after perms
                    var outer = new StackPanel { Orientation = Orientation.Horizontal };
                    Grid.SetColumn(outer, 2);
                    outer.Children.Add(permPanel);
                    outer.Children.Add(btnPanel);
                    grid.Children.Remove(permPanel);
                    grid.Children.Add(outer);
                }
            }

            row.Child = grid;
            MemberRows.Children.Add(row);
        }
    }

    private static CheckBox MakePermCheck(string icon, bool initial, Action<bool> onChanged)
    {
        var cb = new CheckBox
        {
            Content   = icon,
            IsChecked = initial,
            ToolTip   = icon switch { "💬" => "Can send messages", "📎" => "Can send files", "📞" => "Can start calls", _ => "" },
            Margin    = new Thickness(2, 0, 2, 0),
            Cursor    = System.Windows.Input.Cursors.Hand
        };
        cb.Checked   += (_, _) => onChanged(true);
        cb.Unchecked += (_, _) => onChanged(false);
        return cb;
    }

    private static Button MakeBtn(string label, Action onClick, bool isBad = false)
    {
        var btn = new Button
        {
            Content    = label,
            FontSize   = 10,
            Height     = 22,
            Padding    = new Thickness(6, 0, 6, 0),
            Margin     = new Thickness(3, 0, 0, 0),
            Background = isBad
                ? new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(42, 16, 16))
                : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(30, 30, 46)),
            Foreground = isBad
                ? new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(207, 68, 68))
                : System.Windows.Media.Brushes.Gray,
            BorderThickness = new Thickness(0),
            Cursor = System.Windows.Input.Cursors.Hand
        };
        btn.Click += (_, _) => onClick();
        return btn;
    }

    private void QueuePermChange(string memberId)
    {
        var friend = _allFriends.FirstOrDefault(f => f.Id == memberId);
        if (friend == null) return;
        var perms = _group.Permissions.TryGetValue(memberId, out var p) ? p : new GroupPermissions();
        if (!_group.Permissions.ContainsKey(memberId)) _group.Permissions[memberId] = perms;
        PendingActions.Add(() => SignalingClient.Send(friend.Ip, new SigMsg
        {
            Type = SigType.GrpPerm, GroupId = _group.GroupId,
            TargetId = memberId,
            PermMsg  = perms.CanSendMessages,
            PermFile = perms.CanSendFiles,
            PermCall = perms.CanStartCalls,
            FromId = _myId, FromName = "",
            Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
        }));
    }

    private static void QueueSignal(Action a) => a(); // immediate for UI, actual send deferred

    private void BtnAddMember_Click(object sender, RoutedEventArgs e)
    {
        if (CmbAddMember.SelectedItem is not FriendInfo friend) return;
        _group.MemberIds.Add(friend.Id);
        _group.Members.Add(friend);
        PendingActions.Add(() => SignalingClient.Send(friend.Ip, new SigMsg
        {
            Type = SigType.GrpInv, GroupId = _group.GroupId, GroupName = _group.Name,
            Members = _group.Members.Select(m => new MemberDto { Id = m.Id, Name = m.Name, Ip = m.Ip }).ToList(),
            FromId = _myId, FromName = "",
            Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
        }));
        Rebuild();
        // Refresh combo
        var notIn = _allFriends.Where(f => f.IsOnline && !_group.MemberIds.Contains(f.Id)).ToList();
        CmbAddMember.ItemsSource = notIn;
    }

    private void BtnDeleteGroup_Click(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show($"Delete group \"{_group.Name}\"?\nThis will remove it for all members.",
            "Delete Group", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;

        foreach (var memberId in _group.MemberIds)
        {
            var f = _allFriends.FirstOrDefault(x => x.Id == memberId);
            if (f == null) continue;
            PendingActions.Add(() => SignalingClient.Send(f.Ip, new SigMsg
            {
                Type = SigType.GrpDelete, GroupId = _group.GroupId,
                FromId = _myId, FromName = "",
                Ts = DateTimeOffset.Now.ToUnixTimeMilliseconds()
            }));
        }
        Tag = "deleted"; // signal to caller
        DialogResult = true;
    }
}

