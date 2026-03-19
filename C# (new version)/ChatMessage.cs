using System;
using System.Windows.Input;
using System.Windows.Media;

namespace LocalCallPro;

public enum MessageKind { Text, Image, File, VoiceNote, System, CallEvent }

public class ChatMessage
{
    public MessageKind Kind      { get; set; } = MessageKind.Text;
    public string   FromId       { get; set; } = "";
    public string   FromName     { get; set; } = "";
    public string?  Text         { get; set; }
    public string?  FileName     { get; set; }
    public string?  Mime         { get; set; }
    public byte[]?  Data         { get; set; }   // raw bytes for file / voice / image
    public bool     IsMine       { get; set; }
    public DateTime Timestamp    { get; set; } = DateTime.Now;

    // UI-only (not persisted)
    public ImageSource? ImageSource   { get; set; }
    public ICommand?    ActionCommand { get; set; } // play / save / open

    public string TimeStr => Timestamp.ToString("HH:mm");
    public string BubbleColor => IsMine ? "#3D2B6B" : "#2A2A2A";
    public string NameDisplay => IsMine ? "" : FromName;
}

/// <summary>Lightweight ICommand for button bindings inside DataTemplates.</summary>
public sealed class RelayCommand(Action<object?> execute) : ICommand
{
    public bool CanExecute(object? p) => true;
    public void Execute(object? p)    => execute(p);
#pragma warning disable CS0067
    public event EventHandler? CanExecuteChanged;
#pragma warning restore CS0067
}
