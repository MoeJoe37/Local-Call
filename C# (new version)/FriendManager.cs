using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace LocalCallPro;

public class FriendManager
{
    private static readonly string DataDir =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Local Call");

    private static readonly string FriendsFile = Path.Combine(DataDir, "friends.json");
    private static readonly string GroupsFile   = Path.Combine(DataDir, "groups.json");
    private static readonly string PendingFile  = Path.Combine(DataDir, "pending.json");
    private static readonly string BlockedFile  = Path.Combine(DataDir, "blocked.json");
    // Former = removed friends whose name+id we keep so the sidebar can show them grayed-out
    private static readonly string FormerFile   = Path.Combine(DataDir, "former_friends.json");

    public ObservableCollection<FriendInfo>     Friends       { get; } = [];
    public ObservableCollection<GroupInfo>      Groups        { get; } = [];
    public ObservableCollection<PendingRequest> Pending       { get; } = [];
    // Former friends: removed but chat history is still readable
    public ObservableCollection<FriendInfo>     FormerFriends { get; } = [];
    // Blocked peer IDs (declined OR removed) — FriendReqs from them are silently dropped
    public HashSet<string>                      BlockedIds    { get; } = [];

    public FriendManager()
    {
        Directory.CreateDirectory(DataDir);
        Load();
    }

    // ── Friends ───────────────────────────────────────────────────────────────

    public bool       HasFriend(string id)  => Friends.Any(f => f.Id == id);
    public FriendInfo? GetFriend(string id) => Friends.FirstOrDefault(f => f.Id == id);

    public void AddFriend(FriendInfo f)
    {
        if (HasFriend(f.Id)) return;
        RemovePending(f.Id);
        // If they were formerly removed, restore them (no longer former/blocked)
        var old = FormerFriends.FirstOrDefault(x => x.Id == f.Id);
        if (old != null) { FormerFriends.Remove(old); SaveFormer(); }
        BlockedIds.Remove(f.Id);
        SaveBlocked();
        Friends.Add(f);
        Save();
    }

    public void RemoveFriend(string id)
    {
        var f = GetFriend(id);
        if (f == null) return;
        Friends.Remove(f);
        Save();

        // Keep as former so chat history stays accessible in the sidebar
        if (FormerFriends.All(x => x.Id != id))
        {
            FormerFriends.Add(new FriendInfo { Id = f.Id, Name = f.Name, Ip = f.Ip });
            SaveFormer();
        }

        // Block so their future FriendReqs are silently ignored
        BlockedIds.Add(id);
        SaveBlocked();

        // Remove from groups
        foreach (var g in Groups)
        {
            g.MemberIds.Remove(id);
            g.Members.RemoveAll(m => m.Id == id);
        }
        SaveGroups();
    }

    public void UpdateFriendIp(string id, string ip)
    {
        var f = GetFriend(id);
        if (f != null) { f.Ip = ip; Save(); }
    }

    // ── Pending ───────────────────────────────────────────────────────────────

    public bool HasPending(string fromId) => Pending.Any(p => p.FromId == fromId);

    public void AddPending(PendingRequest req)
    {
        if (HasFriend(req.FromId) || HasPending(req.FromId)) return;
        Pending.Add(req);
        SavePending();
    }

    public void RemovePending(string fromId)
    {
        var p = Pending.FirstOrDefault(x => x.FromId == fromId);
        if (p != null) { Pending.Remove(p); SavePending(); }
    }

    // ── Blocked ───────────────────────────────────────────────────────────────

    public bool IsBlocked(string id)  => BlockedIds.Contains(id);
    public void Block(string id)      { BlockedIds.Add(id);    SaveBlocked(); }
    public void Unblock(string id)    { BlockedIds.Remove(id); SaveBlocked(); }

    // ── Groups ────────────────────────────────────────────────────────────────

    public void AddGroup(GroupInfo g) { Groups.Add(g); SaveGroups(); }
    public void RemoveGroup(string gid)
    {
        var g = Groups.FirstOrDefault(x => x.GroupId == gid);
        if (g != null) { Groups.Remove(g); SaveGroups(); }
    }

    // ── Persistence ───────────────────────────────────────────────────────────

    private void Load()
    {
        TryLoad<List<FriendInfo>>(FriendsFile,
            l => { if (l != null) foreach (var f in l) Friends.Add(f); });
        TryLoad<List<GroupInfo>>(GroupsFile,
            l => { if (l != null) foreach (var g in l) Groups.Add(g); });
        TryLoad<List<PendingRequest>>(PendingFile,
            l => { if (l != null) foreach (var p in l) Pending.Add(p); });
        TryLoad<List<string>>(BlockedFile,
            l => { if (l != null) foreach (var id in l) BlockedIds.Add(id); });
        TryLoad<List<FriendInfo>>(FormerFile,
            l => { if (l != null) foreach (var f in l) FormerFriends.Add(f); });
    }

    private static void TryLoad<T>(string path, Action<T?> apply)
    {
        try { if (File.Exists(path)) apply(JsonSerializer.Deserialize<T>(File.ReadAllText(path))); }
        catch { }
    }

    private void Save()       => TrySave(FriendsFile, Friends.ToList());
    private void SaveGroups() => TrySave(GroupsFile,  Groups.ToList());
    public  void SaveGroupsPublic() => SaveGroups();  // called from MainWindow after dialog changes
    private void SavePending()=> TrySave(PendingFile, Pending.ToList());
    private void SaveBlocked()=> TrySave(BlockedFile, BlockedIds.ToList());
    private void SaveFormer() => TrySave(FormerFile,  FormerFriends.ToList());

    private static void TrySave(string path, object obj)
    {
        try { File.WriteAllText(path, JsonSerializer.Serialize(obj)); } catch { }
    }
}
