using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace LocalCallPro;

public partial class NotificationWindow : Window
{
    private DispatcherTimer? _timer;

    public NotificationWindow(string title, string body, (string Label, Action? Act)[]? buttons = null, int autoCloseSec = 0)
    {
        InitializeComponent();
        TxtTitle.Text = title;
        TxtBody.Text  = body;

        if (buttons != null)
        {
            foreach (var (label, act) in buttons)
            {
                var btn = new Button
                {
                    Content         = label,
                    Height          = 30,
                    MinWidth        = 70,
                    Margin          = new Thickness(0, 0, 8, 0),
                    Background      = label.Contains("Acc") || label.Contains("Accept") ? System.Windows.Media.Brushes.DimGray : System.Windows.Media.Brushes.DimGray,
                    Foreground      = System.Windows.Media.Brushes.White,
                    BorderThickness = new Thickness(0),
                    Cursor          = System.Windows.Input.Cursors.Hand,
                    Padding         = new Thickness(12, 0, 12, 0)
                };
                var capturedAct = act;
                btn.Click += (_, _) => { capturedAct?.Invoke(); Close(); };
                ButtonPanel.Children.Add(btn);
            }
        }
        else if (autoCloseSec > 0)
        {
            _timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(autoCloseSec) };
            _timer.Tick += (_, _) => Close();
        }

        Loaded += (_, _) =>
        {
            PositionBottomRight();
            _timer?.Start();
        };
    }

    private void PositionBottomRight()
    {
        var screen = SystemParameters.WorkArea;
        Left = screen.Right  - ActualWidth  - 16;
        Top  = screen.Bottom - ActualHeight - 16;
    }
}
