using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace LocalCallPro;

/// <summary>All message type string constants used over the TCP signaling channel.</summary>
public static class SigType
{
    // Friend management
    public const string FriendReq  = "friend_req";
    public const string FriendAcc  = "friend_acc";
    public const string FriendRej  = "friend_rej";
    public const string FriendDel  = "friend_del";

    // 1-to-1 chat
    public const string ChatText   = "chat_text";
    public const string ChatFile   = "chat_file";   // images / videos / any file (base64)
    public const string ChatVoice  = "chat_voice";  // voice note (WAV base64)

    // 1-to-1 calls & screen share
    public const string CallInv    = "call_inv";    // mode: "voice" | "video"
    public const string CallAcc    = "call_acc";
    public const string CallRej    = "call_rej";
    public const string CallEnd    = "call_end";
    public const string ScreenInv  = "screen_inv";
    public const string ScreenEnd  = "screen_end";

    // Group management
    public const string GrpInv     = "grp_inv";
    public const string GrpAcc     = "grp_acc";
    public const string GrpRej     = "grp_rej";
    public const string GrpLeave   = "grp_leave";

    // Group chat
    public const string GrpText    = "grp_text";
    public const string GrpFile    = "grp_file";
    public const string GrpVoice   = "grp_voice";

    // Group management extended
    public const string GrpKick      = "grp_kick";       // owner/helper kicks a member
    public const string GrpAddMember = "grp_add";        // owner adds a new member
    public const string GrpDelete    = "grp_delete";     // owner deletes the group
    public const string GrpPromote   = "grp_promote";    // owner promotes to helper
    public const string GrpDemote    = "grp_demote";     // owner demotes helper
    public const string GrpPerm      = "grp_perm";       // set member permissions

    // Group call
    public const string GrpCallInv = "grp_call_inv";
    public const string GrpCallAcc = "grp_call_acc";
    public const string GrpCallRej = "grp_call_rej";
    public const string GrpCallEnd = "grp_call_end";
}

public class MemberDto
{
    [JsonPropertyName("id")]   public string Id   { get; set; } = "";
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("ip")]   public string Ip   { get; set; } = "";
}

public class SigMsg
{
    [JsonPropertyName("type")]        public string          Type      { get; set; } = "";
    [JsonPropertyName("from_id")]     public string          FromId    { get; set; } = "";
    [JsonPropertyName("from_name")]   public string          FromName  { get; set; } = "";
    [JsonPropertyName("text")]        public string?         Text      { get; set; }
    [JsonPropertyName("file_name")]   public string?         FileName  { get; set; }
    [JsonPropertyName("mime")]        public string?         Mime      { get; set; }
    [JsonPropertyName("data")]        public string?         Data      { get; set; } // base64
    [JsonPropertyName("group_id")]    public string?         GroupId   { get; set; }
    [JsonPropertyName("group_name")]  public string?         GroupName { get; set; }
    [JsonPropertyName("members")]     public List<MemberDto>? Members  { get; set; }
    [JsonPropertyName("mode")]        public string?         Mode      { get; set; } // voice|video
    [JsonPropertyName("target_id")]   public string?         TargetId  { get; set; } // for kick/perm
    [JsonPropertyName("owner_id")]    public string?         OwnerId   { get; set; }
    [JsonPropertyName("perm_msg")]    public bool?           PermMsg   { get; set; }
    [JsonPropertyName("perm_file")]   public bool?           PermFile  { get; set; }
    [JsonPropertyName("perm_call")]   public bool?           PermCall  { get; set; }
    [JsonPropertyName("ts")]          public long            Ts        { get; set; }
}
