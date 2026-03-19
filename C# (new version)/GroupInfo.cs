using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace LocalCallPro;

public class GroupPermissions
{
    public bool CanSendMessages { get; set; } = true;
    public bool CanSendFiles    { get; set; } = true;
    public bool CanStartCalls   { get; set; } = true;
    public bool IsHelper        { get; set; } = false; // helpers can change permissions of regular members
}

public class GroupInfo
{
    public string      GroupId     { get; set; } = Guid.NewGuid().ToString()[..8];
    public string      Name        { get; set; } = "";
    public string      OwnerId     { get; set; } = "";
    public List<string> MemberIds  { get; set; } = [];
    public List<string> HelperIds  { get; set; } = [];
    // Key = member ID, value = their permissions (missing = full permissions)
    public Dictionary<string, GroupPermissions> Permissions { get; set; } = [];

    [JsonIgnore] public List<FriendInfo> Members { get; set; } = [];

    public bool IsOwner(string id)  => id == OwnerId;
    public bool IsHelper(string id) => HelperIds.Contains(id);
    public bool CanManage(string actorId, string targetId)
        => IsOwner(actorId) || (IsHelper(actorId) && !IsOwner(targetId) && !IsHelper(targetId));

    public GroupPermissions GetPermissions(string memberId)
        => Permissions.TryGetValue(memberId, out var p) ? p : new GroupPermissions();

    public override string ToString() => Name;
}
