using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace DjLoudnessMeter.UI;

public sealed class TaskbarOverlayWindow : Window
{
    private bool _ownerClosing;

    public TaskbarOverlayWindow(FrameworkElement content)
    {
        AllowsTransparency = true;
        Background = new SolidColorBrush(Color.FromArgb(1, 0, 0, 0));
        content.VerticalAlignment = VerticalAlignment.Stretch;
        Content = content;
        ShowInTaskbar = false;
        Topmost = true;
        WindowStyle = WindowStyle.None;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.Manual;

        MenuItem closeItem = new() { Header = "Close" };
        closeItem.Click += (_, _) => CloseRequested?.Invoke(this, EventArgs.Empty);
        ContextMenu = new ContextMenu { Items = { closeItem } };
        MouseDoubleClick += OnMouseDoubleClick;
        Closing += OnClosing;
    }

    public event EventHandler? RestoreRequested;
    public event EventHandler? CloseRequested;

    public FrameworkElement DetachContent()
    {
        FrameworkElement content = (FrameworkElement)Content;
        Content = null;
        return content;
    }

    public void CloseFromOwner()
    {
        _ownerClosing = true;
        Close();
    }

    private void OnMouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton == MouseButton.Left)
        {
            RestoreRequested?.Invoke(this, EventArgs.Empty);
            e.Handled = true;
        }
    }

    private void OnClosing(object? sender, CancelEventArgs e)
    {
        if (!_ownerClosing)
        {
            e.Cancel = true;
            CloseRequested?.Invoke(this, EventArgs.Empty);
        }
    }
}
