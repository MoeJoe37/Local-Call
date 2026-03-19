using System;
using System.Collections.Generic;
using System.Windows;

namespace LocalCallPro;

public enum CallMode { Voice, VideoCamera, VideoScreen }

public partial class CallWindow : Window
{
    private readonly string            _peerIp;
    private readonly string            _peerName;
    private readonly CallMode          _mode;
    private readonly string            _myId;
    private readonly string            _myName;
    private readonly List<MediaWorker> _workers = [];
    private MediaWorker?               _audioSender;
    private MediaWorker?               _videoSender;
    private LocalCameraPreview?        _selfPreview;
    private bool                       _muted;
    private bool                       _screenOn;
    private bool                       _cameraOn = true;
    private volatile bool              _closing;

    public event Action? HangupRequested;

    public CallWindow(string peerIp, string peerName, CallMode mode, string myId, string myName)
    {
        InitializeComponent();
        _peerIp   = peerIp;
        _peerName = peerName;
        _mode     = mode;
        _myId     = myId;
        _myName   = myName;

        TxtCallWith.Text = peerName;
        TxtStatus.Text   = "Connecting…";

        // Voice-only — no video controls
        if (mode == CallMode.Voice)
        {
            RemoteVideo.Visibility = Visibility.Collapsed;
            LocalVideo.Visibility  = Visibility.Collapsed;
            BtnCamera.IsEnabled    = false;
            BtnScreen.IsEnabled    = false;
        }

        // Screen share — show the audio checkbox
        if (mode == CallMode.VideoScreen)
            ScreenAudioPanel.Visibility = Visibility.Visible;

        StartMedia();
    }

    // ── Media lifecycle ───────────────────────────────────────────────────────

    private void StartMedia()
    {
        // Audio (always)
        var aSend = new MediaWorker(LocalCallPro.MediaMode.Audio, _peerIp, MediaSettings.MediaAudioPort);
        var aRecv = new MediaWorker(LocalCallPro.MediaMode.Audio, null,    MediaSettings.MediaAudioPort, isReceiver: true);

        // When audio data arrives the call is connected — hide overlay
        aRecv.Connected += OnMediaConnected;

        _workers.AddRange([aSend, aRecv]);
        _audioSender = aSend;

        if (_mode != CallMode.Voice)
        {
            var vMode = _mode == CallMode.VideoScreen
                ? LocalCallPro.MediaMode.Screen
                : LocalCallPro.MediaMode.Camera;

            var vSend = new MediaWorker(vMode, _peerIp, MediaSettings.MediaVideoPort);
            var vRecv = new MediaWorker(LocalCallPro.MediaMode.Camera, null,
                                        MediaSettings.MediaVideoPort, isReceiver: true);
            vRecv.FrameReceived += OnRemoteFrame;
            vRecv.Connected     += OnMediaConnected;
            _workers.AddRange([vSend, vRecv]);
            _videoSender = vSend;

            // Self-view PiP
            if (_mode == CallMode.VideoCamera)
            {
                _selfPreview = new LocalCameraPreview();
                _selfPreview.FrameReceived += OnLocalFrame;
                _selfPreview.Start();
            }
        }

        // Apply screen-audio checkbox initial state
        if (_mode == CallMode.VideoScreen)
            _audioSender.MuteAudioOnScreen = !(ChkScreenAudio.IsChecked ?? true);

        foreach (var w in _workers) w.Start();
    }

    private void OnMediaConnected() =>
        Dispatcher.InvokeAsync(() =>
        {
            ConnectingOverlay.Visibility = Visibility.Collapsed;
            TxtStatus.Text = "● Connected";
        });

    private void OnRemoteFrame(System.Windows.Media.Imaging.BitmapSource frame) =>
        Dispatcher.InvokeAsync(() =>
        {
            RemoteVideo.Source           = frame;
            ConnectingOverlay.Visibility = Visibility.Collapsed;
        });

    private void OnLocalFrame(System.Windows.Media.Imaging.BitmapSource frame) =>
        Dispatcher.InvokeAsync(() => LocalVideo.Source = frame);

    private void StopMedia()
    {
        _selfPreview?.Stop();
        _selfPreview = null;
        foreach (var w in _workers) w.Stop();
        _workers.Clear();
        _audioSender = null;
        _videoSender = null;
    }

    // ── Controls ──────────────────────────────────────────────────────────────

    private void BtnMute_Click(object sender, RoutedEventArgs e)
    {
        if (_audioSender is null) return;
        _muted             = !_muted;
        _audioSender.Muted = _muted;
        BtnMute.Content    = _muted ? "🔇 Unmute" : "🎤 Mute";
    }

    private void BtnCamera_Click(object sender, RoutedEventArgs e)
    {
        if (_selfPreview == null && !_cameraOn)
        {
            _selfPreview = new LocalCameraPreview();
            _selfPreview.FrameReceived += OnLocalFrame;
            _selfPreview.Start();
            BtnCamera.Content = "📷 Camera";
        }
        else if (_selfPreview != null && _cameraOn)
        {
            _selfPreview.Stop();
            _selfPreview = null;
            LocalVideo.Source = null;
            BtnCamera.Content = "📷 Cam Off";
        }
        _cameraOn = !_cameraOn;
    }

    private void BtnScreen_Click(object sender, RoutedEventArgs e)
    {
        _screenOn = !_screenOn;
        BtnScreen.Content           = _screenOn ? "🖥 Stop" : "🖥 Screen";
        ScreenAudioPanel.Visibility = _screenOn ? Visibility.Visible : Visibility.Collapsed;

        if (_videoSender != null)
        {
            _videoSender.Stop();
            _workers.Remove(_videoSender);
            var m = _screenOn ? LocalCallPro.MediaMode.Screen : LocalCallPro.MediaMode.Camera;
            _videoSender = new MediaWorker(m, _peerIp, MediaSettings.MediaVideoPort);
            _workers.Add(_videoSender);
            _videoSender.Start();
        }

        // Update audio mute state when toggling screen share
        if (_audioSender != null)
            _audioSender.MuteAudioOnScreen = _screenOn && !(ChkScreenAudio.IsChecked ?? true);
    }

    private void ChkScreenAudio_Changed(object sender, RoutedEventArgs e)
    {
        if (_audioSender == null) return;
        _audioSender.MuteAudioOnScreen = _screenOn && !(ChkScreenAudio.IsChecked ?? true);
    }

    private void BtnHangup_Click(object sender, RoutedEventArgs e)
    {
        HangupRequested?.Invoke();
        DoClose();
    }

    private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
    {
        if (!_closing) HangupRequested?.Invoke();
        StopMedia();
    }

    public void DoClose()
    {
        _closing = true;
        StopMedia();
        Dispatcher.InvokeAsync(Close);
    }
}
