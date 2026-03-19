using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Input;

namespace LocalCallPro;

public class FriendSelectItem(FriendInfo f)
{
    public string Name       => f.Name;
    public bool   IsOnline   => f.IsOnline;
    public bool   IsSelected { get; set; }
    public FriendInfo Friend  => f;
}

public partial class GroupCreateDialog : Window
{
    private readonly List<FriendSelectItem> _items;

    public string        GroupName    { get; private set; } = "";
    public List<FriendInfo> Selected  { get; private set; } = [];

    public GroupCreateDialog(IEnumerable<FriendInfo> onlineFriends, Window owner)
    {
        InitializeComponent();
        Owner = owner;
        _items = onlineFriends.Select(f => new FriendSelectItem(f)).ToList();
        FriendCheckList.ItemsSource = _items;
        Loaded += (_, _) => TxtName.Focus();
    }

    private void Create_Click(object sender, RoutedEventArgs e) => TryCreate();
    private void Cancel_Click(object sender, RoutedEventArgs e) => DialogResult = false;
    private void TxtName_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) TryCreate();
        if (e.Key == Key.Escape) DialogResult = false;
    }

    private void TryCreate()
    {
        var name     = TxtName.Text.Trim();
        var selected = _items.Where(i => i.IsSelected).Select(i => i.Friend).ToList();

        if (string.IsNullOrWhiteSpace(name))
        { MessageBox.Show("Please enter a group name.", "Create Group"); return; }
        if (selected.Count == 0)
        { MessageBox.Show("Please select at least one member.", "Create Group"); return; }

        GroupName    = name;
        Selected     = selected;
        DialogResult = true;
    }
}
