using System;
using System.Threading;
using System.Windows.Media.Imaging;
using OpenCvSharp;

namespace LocalCallPro;

/// <summary>Reads from local webcam and fires FrameReceived — for the self-view PiP in calls.</summary>
public class LocalCameraPreview
{
    private Thread?       _thread;
    private volatile bool _running;

    public event Action<BitmapSource>? FrameReceived;

    public void Start()
    {
        _running = true;
        _thread  = new Thread(Run) { IsBackground = true, Name = "LocalPreview" };
        _thread.Start();
    }

    public void Stop()
    {
        _running = false;
        _thread?.Join(2000);
    }

    private void Run()
    {
        VideoCapture? cap = null;
        try
        {
            cap = new VideoCapture(0);
            if (!cap.IsOpened()) return;

            using var frame = new Mat();
            while (_running)
            {
                if (!cap.Read(frame) || frame.Empty()) { Thread.Sleep(33); continue; }

                using var small = new Mat();
                Cv2.Resize(frame, small, new Size(320, 240));
                var bs = MediaWorkerHelper.MatToBitmapSource(small);
                FrameReceived?.Invoke(bs);
                Thread.Sleep(33); // ~30 fps
            }
        }
        catch { }
        finally { cap?.Release(); cap?.Dispose(); }
    }
}
