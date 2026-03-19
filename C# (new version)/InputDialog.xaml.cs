using System.Windows;
using System.Windows.Input;

namespace LocalCallPro;

public partial class InputDialog : Window
{
    public string Result { get; private set; } = "";

    public InputDialog(string title, string label, string defaultText = "")
    {
        InitializeComponent();
        Title         = title;
        TxtLabel.Text = label;
        TxtInput.Text = defaultText;
        Loaded += (_, _) => { TxtInput.SelectAll(); TxtInput.Focus(); };
    }

    private void OK_Click(object sender, RoutedEventArgs e)
    {
        Result = TxtInput.Text.Trim();
        DialogResult = true;
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) => DialogResult = false;

    private void TxtInput_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)  { Result = TxtInput.Text.Trim(); DialogResult = true; }
        if (e.Key == Key.Escape) { DialogResult = false; }
    }
}
