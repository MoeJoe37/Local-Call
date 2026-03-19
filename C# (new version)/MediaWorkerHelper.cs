using System.Runtime.InteropServices;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using OpenCvSharp;

namespace LocalCallPro;

public static class MediaWorkerHelper
{
    public static BitmapSource MatToBitmapSource(Mat mat)
    {
        using var rgb    = new Mat();
        Cv2.CvtColor(mat, rgb, ColorConversionCodes.BGR2RGB);

        int w      = rgb.Width;
        int h      = rgb.Height;
        int stride = w * 3;
        var pixels = new byte[h * stride];
        Marshal.Copy(rgb.Data, pixels, 0, pixels.Length);

        var bs = BitmapSource.Create(w, h, 96, 96,
            PixelFormats.Rgb24, null, pixels, stride);
        bs.Freeze();
        return bs;
    }
}
