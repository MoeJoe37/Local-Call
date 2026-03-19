using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace LocalCallPro;

/// <summary>Stores chat history per conversation key to disk.</summary>
public class ChatStore
{
    private static readonly string DataDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Local Call", "chats");

    private readonly Dictionary<string, List<StoredMessage>> _cache = [];

    public ChatStore() => Directory.CreateDirectory(DataDir);

    public List<ChatMessage> Load(string convKey)
    {
        if (!_cache.TryGetValue(convKey, out var stored))
        {
            stored = LoadFromDisk(convKey);
            _cache[convKey] = stored;
        }
        return stored.Select(ToMessage).ToList();
    }

    public void Append(string convKey, ChatMessage m)
    {
        if (!_cache.TryGetValue(convKey, out var stored))
        {
            stored = LoadFromDisk(convKey);
            _cache[convKey] = stored;
        }
        stored.Add(ToStored(m));
        SaveToDisk(convKey, stored);
    }

    private static string FilePath(string key) =>
        Path.Combine(DataDir, $"{SanitiseKey(key)}.json");

    private static string SanitiseKey(string k) =>
        string.Concat(k.Select(c => char.IsLetterOrDigit(c) || c == '-' ? c : '_'));

    private static List<StoredMessage> LoadFromDisk(string key)
    {
        try
        {
            var p = FilePath(key);
            if (!File.Exists(p)) return [];
            return JsonSerializer.Deserialize<List<StoredMessage>>(File.ReadAllText(p)) ?? [];
        }
        catch { return []; }
    }

    private static void SaveToDisk(string key, List<StoredMessage> list)
    {
        try { File.WriteAllText(FilePath(key), JsonSerializer.Serialize(list)); }
        catch { }
    }

    private static ChatMessage ToMessage(StoredMessage s) => new()
    {
        Kind      = Enum.TryParse<MessageKind>(s.Kind, out var k) ? k : MessageKind.Text,
        FromId    = s.FromId,
        FromName  = s.FromName,
        Text      = s.Text,
        FileName  = s.FileName,
        Mime      = s.Mime,
        Data      = s.Data != null ? Convert.FromBase64String(s.Data) : null,
        IsMine    = s.IsMine,
        Timestamp = DateTimeOffset.FromUnixTimeMilliseconds(s.Ts).LocalDateTime
    };

    private static StoredMessage ToStored(ChatMessage m) => new()
    {
        Kind     = m.Kind.ToString(),
        FromId   = m.FromId,
        FromName = m.FromName,
        Text     = m.Text,
        FileName = m.FileName,
        Mime     = m.Mime,
        Data     = m.Data != null ? Convert.ToBase64String(m.Data) : null,
        IsMine   = m.IsMine,
        Ts       = new DateTimeOffset(m.Timestamp).ToUnixTimeMilliseconds()
    };

    private class StoredMessage
    {
        public string  Kind     { get; set; } = "Text";
        public string  FromId   { get; set; } = "";
        public string  FromName { get; set; } = "";
        public string? Text     { get; set; }
        public string? FileName { get; set; }
        public string? Mime     { get; set; }
        public string? Data     { get; set; }
        public bool    IsMine   { get; set; }
        public long    Ts       { get; set; }
    }
}
