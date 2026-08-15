using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using DjLoudnessMeter.Audio;
using DjLoudnessMeter.Hardware;
using DjLoudnessMeter.Infrastructure;
using DjLoudnessMeter.Metering;
using DjLoudnessMeter.Settings;

namespace DjLoudnessMeter.UI;

public partial class MainWindow : Window
{
    private const double PeakWarningDb = -6.0;
    private const double PeakCriticalDb = -1.0;
    private const double LufsWarning = -12.0;
    private const double LufsCritical = -9.0;
    private const int SystemMetricsIntervalMilliseconds = 500;
    private static readonly int[] RefreshIntervals = [10, 50, 125, 250, 500, 750, 1000, 1250, 1500, 1750, 2000];
    private const uint SwpNoSize = 0x0001;
    private const uint SwpNoMove = 0x0002;
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpShowWindow = 0x0040;
    private static readonly IntPtr HwndTopmost = new(-1);
    private static readonly Brush ConnectedBrush = new SolidColorBrush(Color.FromRgb(88, 214, 141));
    private static readonly Brush WaitingBrush = new SolidColorBrush(Color.FromRgb(143, 154, 163));
    private static readonly Brush ErrorBrush = new SolidColorBrush(Color.FromRgb(255, 123, 127));
    private static readonly Brush NormalValueBrush = new SolidColorBrush(Color.FromRgb(243, 245, 246));
    private static readonly Brush WarningValueBrush = new SolidColorBrush(Color.FromRgb(246, 195, 68));
    private static readonly Brush CriticalValueBrush = new SolidColorBrush(Color.FromRgb(255, 90, 95));
    private readonly SettingsService _settingsService = new();
    private readonly AudioDeviceService _deviceService = new();
    private readonly DispatcherTimer _uiTimer;
    private readonly AppSettings _settings;
    private readonly AudioCaptureService _captureService;
    private readonly CpuTemperatureService _cpuTemperatureService = new();
    private readonly SystemResourceService _systemResourceService = new();
    private bool _refreshingDevices;
    private bool _applicationClosing;
    private bool _initializingValueVisibility = true;
    private TaskbarOverlayWindow? _taskbarOverlay;
    private double _displayZeroDbfs;
    private double _normalWidth = 460;
    private double _normalHeight = 410;
    private double _normalLeft = double.NaN;
    private double _normalTop = double.NaN;
    private long _lastSystemMetricsUpdate;
    private bool _hasTemperature;

    public MainWindow()
    {
        _settings = _settingsService.Load();
        if (!_settings.ShowLoudnessValues && !_settings.ShowSystemValues)
        {
            _settings.ShowLoudnessValues = true;
        }

        _displayZeroDbfs = LoudnessDisplayScale.NormalizeReference(_settings.DisplayZeroDbfs);
        _captureService = new AudioCaptureService(TimeSpan.FromMilliseconds(Math.Clamp(_settings.PeakHoldMilliseconds, 0, 10000)));
        Devices = [];
        Monitors = [];
        InitializeComponent();
        DataContext = this;
        int refreshMilliseconds = NormalizeRefreshInterval(_settings.UiRefreshMilliseconds);
        _uiTimer = new DispatcherTimer(DispatcherPriority.Background)
        {
            Interval = TimeSpan.FromMilliseconds(refreshMilliseconds)
        };
        _uiTimer.Tick += OnUiTimerTick;
        DisplayZeroTextBox.Text = FormatReference(_displayZeroDbfs);
        TaskbarSideComboBox.SelectedIndex = _settings.TaskbarRightAligned ? 1 : 0;
        LoudnessValuesCheckBox.IsChecked = _settings.ShowLoudnessValues;
        SystemValuesCheckBox.IsChecked = _settings.ShowSystemValues;
        _initializingValueVisibility = false;
        ApplyOverlayValueVisibility();
        RefreshIntervalSlider.Value = refreshMilliseconds;
        RefreshIntervalValue.Text = $"{refreshMilliseconds} ms";
        _settings.UiRefreshMilliseconds = refreshMilliseconds;
        UpdateRefreshIntervalLabel(refreshMilliseconds);
        UpdateThresholdTooltips();

        _captureService.CaptureStopped += OnCaptureStopped;
    }

    public ObservableCollection<AudioDeviceInfo> Devices { get; }
    public ObservableCollection<DisplayMonitor> Monitors { get; }

    internal void Start()
    {
        RestoreWindowSettings();
        RefreshMonitors();
        RefreshDevices();
        ConfigureSettingsWindow();
        CreateTaskbarOverlay();
        _normalWidth = Width;
        _normalHeight = Height;
        PositionTaskbarWindow();
        _taskbarOverlay?.Show();
        Dispatcher.BeginInvoke((Action)EnsureTaskbarZOrder, DispatcherPriority.Loaded);
        _uiTimer.Start();
    }

    private void RestoreWindowSettings()
    {
        if (double.IsFinite(_settings.WindowWidth) && _settings.WindowWidth >= MinWidth)
        {
            Width = _settings.WindowWidth;
        }

        if (double.IsFinite(_settings.WindowHeight) && _settings.WindowHeight >= MinHeight)
        {
            Height = _settings.WindowHeight;
        }

        if (double.IsFinite(_settings.WindowLeft) && double.IsFinite(_settings.WindowTop))
        {
            Left = _settings.WindowLeft;
            Top = _settings.WindowTop;
            _normalLeft = Left;
            _normalTop = Top;
        }

        Topmost = false;
    }

    private void RefreshMonitors()
    {
        IReadOnlyList<DisplayMonitor> monitors = DisplayMonitor.GetAll();
        Monitors.Clear();
        foreach (DisplayMonitor monitor in monitors)
        {
            Monitors.Add(monitor);
        }

        Visibility monitorVisibility = Monitors.Count > 1 ? Visibility.Visible : Visibility.Collapsed;
        MonitorLabel.Visibility = monitorVisibility;
        MonitorComboBox.Visibility = monitorVisibility;
        DisplayMonitor? selected = Monitors.FirstOrDefault(monitor =>
                string.Equals(monitor.DeviceName, _settings.TaskbarMonitorDeviceName, StringComparison.OrdinalIgnoreCase))
            ?? Monitors.FirstOrDefault(static monitor => monitor.IsPrimary)
            ?? Monitors.FirstOrDefault();
        MonitorComboBox.SelectedItem = selected;
        _settings.TaskbarMonitorDeviceName = selected?.DeviceName;
    }

    private void RefreshDevices()
    {
        _refreshingDevices = true;
        string? wantedId = (DeviceComboBox.SelectedItem as AudioDeviceInfo)?.Id ?? _settings.SelectedAudioEndpointId;
        try
        {
            IReadOnlyList<AudioDeviceInfo> devices = _deviceService.GetActiveRenderDevices();
            Devices.Clear();
            foreach (AudioDeviceInfo device in devices)
            {
                Devices.Add(device);
            }

            if (Devices.Count == 0)
            {
                DeviceComboBox.SelectedItem = null;
                _captureService.Stop();
                ShowStatus("No active Windows playback devices are available.", isError: true);
                return;
            }

            AudioDeviceInfo selected = Devices.FirstOrDefault(device => device.Id == wantedId)
                ?? Devices.FirstOrDefault(static device => device.IsDefault)
                ?? Devices[0];
            DeviceComboBox.SelectedItem = selected;
        }
        catch (Exception ex)
        {
            AppLog.Error("Audio device enumeration failed.", ex);
            ShowStatus("Windows playback devices could not be enumerated.", isError: true);
        }
        finally
        {
            _refreshingDevices = false;
        }

        if (DeviceComboBox.SelectedItem is AudioDeviceInfo selectedDevice)
        {
            StartCapture(selectedDevice);
        }
    }

    private void StartCapture(AudioDeviceInfo device)
    {
        try
        {
            _captureService.Start(device.Id);
            _settings.SelectedAudioEndpointId = device.Id;
            FormatText.Text = _captureService.FormatDescription ?? string.Empty;
            ShowStatus($"Monitoring {device.FriendlyName}", isError: false);
        }
        catch (Exception ex)
        {
            AppLog.Error($"Capture failed for {device.FriendlyName}.", ex);
            ShowStatus(ToUserMessage(ex), isError: true);
            FormatText.Text = string.Empty;
        }
    }

    private void OnUiTimerTick(object? sender, EventArgs e)
    {
        MeterSnapshot snapshot;
        try
        {
            snapshot = _captureService.GetSnapshot();
        }
        catch (Exception ex)
        {
            AppLog.Error("Meter state update failed.", ex);
            _captureService.Stop();
            ShowStatus(ToUserMessage(ex), isError: true);
            snapshot = MeterSnapshot.Disconnected;
        }

        CompactPeakValue.Text = FormatDb(Adjust(snapshot.PeakDb), string.Empty);
        CompactHoldValue.Text = FormatDb(Adjust(snapshot.PeakHoldDb), string.Empty);
        CompactMomentaryValue.Text = FormatLufs(Adjust(snapshot.MomentaryLufs));
        CompactShortTermValue.Text = FormatLufs(Adjust(snapshot.ShortTermLufs));
        long now = Environment.TickCount64;
        if (_lastSystemMetricsUpdate == 0 || now - _lastSystemMetricsUpdate >= SystemMetricsIntervalMilliseconds)
        {
            _lastSystemMetricsUpdate = now;
            if (_settings.ShowSystemValues)
            {
                double? cpuTemperature = _cpuTemperatureService.ReadCelsius();
                CompactCpuTemperatureValue.Text = FormatCompactTemperature(cpuTemperature);
                _hasTemperature = cpuTemperature is double temperature && double.IsFinite(temperature);
                SystemResourceSnapshot systemResources = _systemResourceService.Read();
                CompactCpuUsageValue.Text = FormatPercentage(systemResources.CpuUsagePercent, compact: true);
                CompactMemoryUsageValue.Text = FormatPercentage(systemResources.MemoryUsagePercent, compact: true);
                ApplyOverlayValueVisibility();
            }

            if (_taskbarOverlay is not null)
            {
                PositionTaskbarWindow();
            }
        }
        SetReadingColors(snapshot);
        if (snapshot.IsConnected && !snapshot.HasRecentAudio && StatusText.Foreground != ErrorBrush)
        {
            StatusText.Text = $"Monitoring {_captureService.DeviceName} · waiting for audio";
            StatusText.Foreground = WaitingBrush;
        }
        else if (snapshot.IsConnected && snapshot.HasRecentAudio && StatusText.Foreground != ErrorBrush)
        {
            StatusText.Text = $"Monitoring {_captureService.DeviceName}";
            StatusText.Foreground = ConnectedBrush;
        }

        EnsureTaskbarZOrder();
    }

    private void OnCaptureStopped(object? sender, CaptureStoppedEventArgs e)
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(() => OnCaptureStopped(sender, e));
            return;
        }

        ShowStatus(e.Message + " Select or refresh a playback device.", isError: true);
    }

    private void OnDeviceSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_refreshingDevices && DeviceComboBox.SelectedItem is AudioDeviceInfo device)
        {
            StartCapture(device);
        }
    }

    private void OnRefreshDevicesClick(object sender, RoutedEventArgs e) => RefreshDevices();

    private void OnMonitorSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (MonitorComboBox.SelectedItem is DisplayMonitor monitor)
        {
            _settings.TaskbarMonitorDeviceName = monitor.DeviceName;
            if (_taskbarOverlay is not null)
            {
                PositionTaskbarWindow();
            }
        }
    }

    private void OnTaskbarSideSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        _settings.TaskbarRightAligned = TaskbarSideComboBox.SelectedIndex == 1;
        if (_taskbarOverlay is not null)
        {
            PositionTaskbarWindow();
        }
    }

    private void OnValueVisibilityChanged(object sender, RoutedEventArgs e)
    {
        if (_initializingValueVisibility)
        {
            return;
        }

        bool showLoudness = LoudnessValuesCheckBox.IsChecked == true;
        bool showSystem = SystemValuesCheckBox.IsChecked == true;
        if (!showLoudness && !showSystem)
        {
            ((CheckBox)sender).IsChecked = true;
            return;
        }

        _settings.ShowLoudnessValues = showLoudness;
        _settings.ShowSystemValues = showSystem;
        ApplyOverlayValueVisibility();
        if (_taskbarOverlay is not null)
        {
            PositionTaskbarWindow();
        }
    }

    private void ApplyOverlayValueVisibility()
    {
        bool showLoudness = _settings.ShowLoudnessValues;
        bool showSystem = _settings.ShowSystemValues;
        GridLength loudnessWidth = showLoudness ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
        PeakColumn.Width = loudnessWidth;
        HoldColumn.Width = loudnessWidth;
        MomentaryColumn.Width = loudnessWidth;
        ShortTermColumn.Width = loudnessWidth;
        CompactPeakPanel.Visibility = showLoudness ? Visibility.Visible : Visibility.Collapsed;
        CompactHoldPanel.Visibility = showLoudness ? Visibility.Visible : Visibility.Collapsed;
        CompactMomentaryPanel.Visibility = showLoudness ? Visibility.Visible : Visibility.Collapsed;
        CompactShortTermPanel.Visibility = showLoudness ? Visibility.Visible : Visibility.Collapsed;

        bool showSeparator = showLoudness && showSystem;
        SeparatorColumn.Width = showSeparator ? new GridLength(13) : new GridLength(0);
        CompactSeparator.Visibility = showSeparator ? Visibility.Visible : Visibility.Collapsed;
        bool showTemperature = showSystem && _hasTemperature;
        TemperatureColumn.Width = showTemperature ? new GridLength(46) : new GridLength(0);
        CompactTemperaturePanel.Visibility = showTemperature ? Visibility.Visible : Visibility.Collapsed;
        CpuColumn.Width = showSystem ? new GridLength(46) : new GridLength(0);
        MemoryColumn.Width = showSystem ? new GridLength(46) : new GridLength(0);
        CompactCpuPanel.Visibility = showSystem ? Visibility.Visible : Visibility.Collapsed;
        CompactMemoryPanel.Visibility = showSystem ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnRefreshIntervalChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_uiTimer is null)
        {
            return;
        }

        int refreshMilliseconds = NormalizeRefreshInterval((int)e.NewValue);
        _settings.UiRefreshMilliseconds = refreshMilliseconds;
        _uiTimer.Interval = TimeSpan.FromMilliseconds(refreshMilliseconds);
        RefreshIntervalValue.Text = $"{refreshMilliseconds} ms";
        UpdateRefreshIntervalLabel(refreshMilliseconds);
        UpdateThresholdTooltips();
    }

    private void OnResetClick(object sender, RoutedEventArgs e)
    {
        try
        {
            _captureService.Reset();
        }
        catch (Exception ex)
        {
            AppLog.Error("Meter reset failed.", ex);
            ShowStatus(ToUserMessage(ex), isError: true);
        }
    }

    private void OnDisplayZeroLostFocus(object sender, RoutedEventArgs e) => ApplyDisplayZeroInput();

    private void OnDisplayZeroKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            ApplyDisplayZeroInput();
            Keyboard.ClearFocus();
            e.Handled = true;
        }
    }

    private void ApplyDisplayZeroInput()
    {
        string input = DisplayZeroTextBox.Text
            .Replace("dBFS", string.Empty, StringComparison.OrdinalIgnoreCase)
            .Replace("dB", string.Empty, StringComparison.OrdinalIgnoreCase)
            .Replace('−', '-')
            .Trim();
        bool parsed = double.TryParse(input, NumberStyles.Float, CultureInfo.CurrentCulture, out double reference) ||
            double.TryParse(input, NumberStyles.Float, CultureInfo.InvariantCulture, out reference);

        if (!parsed || !double.IsFinite(reference) ||
            reference < LoudnessDisplayScale.MinimumReferenceDbfs ||
            reference > LoudnessDisplayScale.MaximumReferenceDbfs)
        {
            DisplayZeroTextBox.Text = FormatReference(_displayZeroDbfs);
            return;
        }

        _displayZeroDbfs = Math.Round(reference, 1, MidpointRounding.AwayFromZero);
        _settings.DisplayZeroDbfs = _displayZeroDbfs;
        DisplayZeroTextBox.Text = FormatReference(_displayZeroDbfs);
        UpdateThresholdTooltips();
    }

    private void ConfigureSettingsWindow()
    {
        Topmost = false;
        WindowStyle = WindowStyle.SingleBorderWindow;
        ResizeMode = ResizeMode.CanResize;
        MinWidth = 400;
        MinHeight = 360;
        MainRoot.Margin = new Thickness(16);
    }

    private void ShowSettingsWindow()
    {
        ShowInTaskbar = true;
        if (!IsVisible)
        {
            bool fitToContent = SizeToContent != System.Windows.SizeToContent.Manual;
            if (!fitToContent)
            {
                Width = Math.Max(_normalWidth, MinWidth);
                Height = Math.Max(_normalHeight, MinHeight);
                RestoreNormalPosition();
            }

            Show();
            if (fitToContent)
            {
                double contentHeight = ActualHeight;
                SizeToContent = System.Windows.SizeToContent.Manual;
                Height = Math.Max(MinHeight, contentHeight);
                _normalWidth = ActualWidth;
                _normalHeight = Height;
                _normalLeft = Left;
                _normalTop = Top;
            }
        }

        if (WindowState == WindowState.Minimized)
        {
            WindowState = WindowState.Normal;
        }

        Activate();
    }

    private void CreateTaskbarOverlay()
    {
        if (_taskbarOverlay is not null)
        {
            return;
        }

        CompactMeasurements.Visibility = Visibility.Visible;
        MainRoot.Children.Remove(CompactMeasurements);
        _taskbarOverlay = new TaskbarOverlayWindow(CompactMeasurements);
        _taskbarOverlay.RestoreRequested += OnTaskbarRestoreRequested;
        _taskbarOverlay.CloseRequested += OnTaskbarCloseRequested;
    }

    private void CloseTaskbarOverlay(bool restoreContent)
    {
        if (_taskbarOverlay is null)
        {
            return;
        }

        _taskbarOverlay.RestoreRequested -= OnTaskbarRestoreRequested;
        _taskbarOverlay.CloseRequested -= OnTaskbarCloseRequested;
        FrameworkElement content = _taskbarOverlay.DetachContent();
        _taskbarOverlay.CloseFromOwner();
        _taskbarOverlay = null;
        if (restoreContent)
        {
            MainRoot.Children.Add(content);
        }
    }

    private void OnTaskbarRestoreRequested(object? sender, EventArgs e) => ShowSettingsWindow();

    private void OnTaskbarCloseRequested(object? sender, EventArgs e) => CloseApplication();

    internal void CloseApplication()
    {
        _applicationClosing = true;
        Close();
    }

    private void RestoreNormalPosition()
    {
        DisplayMonitor monitor = FindMonitorForNormalPosition();
        Rect workArea = ToWpfRect(monitor.WorkArea);
        double wantedLeft = double.IsFinite(_normalLeft) ? _normalLeft : workArea.Left + ((workArea.Width - Width) / 2);
        double wantedTop = double.IsFinite(_normalTop) ? _normalTop : workArea.Top + ((workArea.Height - Height) / 2);
        Left = Math.Clamp(wantedLeft, workArea.Left, Math.Max(workArea.Left, workArea.Right - Width));
        Top = Math.Clamp(wantedTop, workArea.Top, Math.Max(workArea.Top, workArea.Bottom - Height));
    }

    private DisplayMonitor FindMonitorForNormalPosition()
    {
        if (double.IsFinite(_normalLeft) && double.IsFinite(_normalTop))
        {
            DisplayMonitor? matching = Monitors.FirstOrDefault(monitor =>
            {
                Rect bounds = ToWpfRect(monitor.Bounds);
                return bounds.Contains(new Point(_normalLeft, _normalTop));
            });
            if (matching is not null)
            {
                return matching;
            }
        }

        return Monitors.FirstOrDefault(static monitor => monitor.IsPrimary)
            ?? Monitors.First();
    }

    private void PositionTaskbarWindow()
    {
        DisplayMonitor? monitor = MonitorComboBox.SelectedItem as DisplayMonitor
            ?? Monitors.FirstOrDefault(static candidate => candidate.IsPrimary)
            ?? Monitors.FirstOrDefault();
        if (monitor is null)
        {
            return;
        }

        Window target = (Window?)_taskbarOverlay ?? this;

        Rect bounds = ToWpfRect(monitor.Bounds);
        Rect workArea = ToWpfRect(monitor.WorkArea);
        double bottomTaskbarHeight = bounds.Bottom - workArea.Bottom;
        double topTaskbarHeight = workArea.Top - bounds.Top;
        double taskbarHeight;
        if (bottomTaskbarHeight > 0)
        {
            target.Top = workArea.Bottom;
            taskbarHeight = bottomTaskbarHeight;
        }
        else if (topTaskbarHeight > 0)
        {
            target.Top = bounds.Top;
            taskbarHeight = topTaskbarHeight;
        }
        else
        {
            taskbarHeight = 42;
            target.Top = bounds.Bottom - taskbarHeight;
        }

        double overlayWidth = 0;
        if (_settings.ShowLoudnessValues)
        {
            overlayWidth += 255;
        }
        if (_settings.ShowSystemValues)
        {
            overlayWidth += 92;
            if (_hasTemperature)
            {
                overlayWidth += 46;
            }
        }
        if (_settings.ShowLoudnessValues && _settings.ShowSystemValues)
        {
            overlayWidth += 13;
        }

        target.Width = Math.Min(overlayWidth, bounds.Width);
        target.Height = Math.Max(32, taskbarHeight);
        if (_settings.TaskbarRightAligned)
        {
            double? taskbarSafeRight = DisplayMonitor.GetTaskbarSafeRight(monitor.DeviceName);
            double fallbackReserve = monitor.IsPrimary ? 240 : 120;
            double safeRight = taskbarSafeRight is double physicalRight
                ? ToWpfX(physicalRight) - 4
                : bounds.Right - Math.Min(fallbackReserve, bounds.Width / 4);
            target.Left = Math.Max(bounds.Left, safeRight - target.Width);
        }
        else
        {
            double? taskbarSafeLeft = DisplayMonitor.GetTaskbarSafeLeft(monitor.DeviceName);
            bool followsWidgets = taskbarSafeLeft is not null;
            HorizontalAlignment metricAlignment = followsWidgets
                ? HorizontalAlignment.Left
                : HorizontalAlignment.Stretch;
            Thickness metricMargin = followsWidgets ? new Thickness(2, 0, 0, 0) : default;
            CompactPeakPanel.HorizontalAlignment = metricAlignment;
            CompactHoldPanel.HorizontalAlignment = metricAlignment;
            CompactMomentaryPanel.HorizontalAlignment = metricAlignment;
            CompactShortTermPanel.HorizontalAlignment = metricAlignment;
            CompactPeakPanel.Margin = metricMargin;
            CompactHoldPanel.Margin = metricMargin;
            CompactMomentaryPanel.Margin = metricMargin;
            CompactShortTermPanel.Margin = metricMargin;
            target.Left = taskbarSafeLeft is double physicalLeft
                ? Math.Max(bounds.Left, ToWpfX(physicalLeft) + 2)
                : bounds.Left;
        }
    }

    private Rect ToWpfRect(Rect physicalRect)
    {
        double scale = GetCoordinateScale();
        return new Rect(
            physicalRect.Left / scale,
            physicalRect.Top / scale,
            physicalRect.Width / scale,
            physicalRect.Height / scale);
    }

    private double ToWpfX(double physicalX) => physicalX / GetCoordinateScale();

    private double GetCoordinateScale()
    {
        DisplayMonitor? primary = Monitors.FirstOrDefault(static monitor => monitor.IsPrimary);
        return primary is not null && SystemParameters.PrimaryScreenWidth > 0
            ? primary.Bounds.Width / SystemParameters.PrimaryScreenWidth
            : 1;
    }

    private void EnsureTaskbarZOrder()
    {
        if (_taskbarOverlay is null)
        {
            return;
        }

        Window target = (Window?)_taskbarOverlay ?? this;
        IntPtr handle = new WindowInteropHelper(target).Handle;
        if (handle != IntPtr.Zero)
        {
            SetWindowPos(
                handle,
                HwndTopmost,
                0,
                0,
                0,
                0,
                SwpNoMove | SwpNoSize | SwpNoActivate | SwpShowWindow);
        }
    }
    private void SetReadingColors(MeterSnapshot snapshot)
    {
        Brush peakBrush = GetThresholdBrush(Adjust(snapshot.PeakDb), Adjust(PeakWarningDb), Adjust(PeakCriticalDb));
        Brush holdBrush = GetThresholdBrush(Adjust(snapshot.PeakHoldDb), Adjust(PeakWarningDb), Adjust(PeakCriticalDb));
        Brush momentaryBrush = GetThresholdBrush(Adjust(snapshot.MomentaryLufs), Adjust(LufsWarning), Adjust(LufsCritical));
        Brush shortTermBrush = GetThresholdBrush(Adjust(snapshot.ShortTermLufs), Adjust(LufsWarning), Adjust(LufsCritical));

        CompactPeakValue.Foreground = peakBrush;
        CompactHoldValue.Foreground = holdBrush;
        CompactMomentaryValue.Foreground = momentaryBrush;
        CompactShortTermValue.Foreground = shortTermBrush;
    }

    private void UpdateThresholdTooltips()
    {
        string peakTooltip = $"Adjusted peak: amber at {FormatThreshold(Adjust(PeakWarningDb))} dB; red at {FormatThreshold(Adjust(PeakCriticalDb))} dB";
        string shortTermTooltip = $"Adjusted short-term loudness: amber at {FormatThreshold(Adjust(LufsWarning))}; red at {FormatThreshold(Adjust(LufsCritical))}";
        string momentaryTooltip = $"Adjusted momentary loudness: amber at {FormatThreshold(Adjust(LufsWarning))}; red at {FormatThreshold(Adjust(LufsCritical))}";
        CompactPeakPanel.ToolTip = $"{_settings.UiRefreshMilliseconds} ms maximum. " + peakTooltip;
        CompactHoldPanel.ToolTip = peakTooltip;
        CompactShortTermPanel.ToolTip = shortTermTooltip;
        CompactMomentaryPanel.ToolTip = momentaryTooltip;
    }

    private static Brush GetThresholdBrush(double value, double warning, double critical)
    {
        if (!double.IsFinite(value))
        {
            return NormalValueBrush;
        }

        return value >= critical ? CriticalValueBrush : value >= warning ? WarningValueBrush : NormalValueBrush;
    }

    private void ShowStatus(string message, bool isError)
    {
        StatusText.Text = message;
        StatusText.Foreground = isError ? ErrorBrush : ConnectedBrush;
        if (isError)
        {
            StatusPanel.Visibility = Visibility.Visible;
        }
        else
        {
            StatusPanel.Visibility = Visibility.Visible;
        }
    }

    private void OnClosing(object? sender, CancelEventArgs e)
    {
        if (!_applicationClosing)
        {
            e.Cancel = true;
            if (WindowState == WindowState.Normal)
            {
                _normalWidth = ActualWidth;
                _normalHeight = ActualHeight;
                _normalLeft = Left;
                _normalTop = Top;
            }

            Hide();
            return;
        }

        CloseTaskbarOverlay(restoreContent: false);
        _uiTimer.Stop();
        _captureService.CaptureStopped -= OnCaptureStopped;
        _captureService.Dispose();
        _cpuTemperatureService.Dispose();

        _settings.WindowLeft = Left;
        _settings.WindowTop = Top;
        _settings.WindowWidth = ActualWidth;
        _settings.WindowHeight = ActualHeight;

        _settings.AlwaysOnTop = false;
        _settings.TaskbarMonitorDeviceName = (MonitorComboBox.SelectedItem as DisplayMonitor)?.DeviceName;
        _settings.TaskbarRightAligned = TaskbarSideComboBox.SelectedIndex == 1;
        _settings.UiRefreshMilliseconds = (int)_uiTimer.Interval.TotalMilliseconds;
        _settings.DisplayZeroDbfs = _displayZeroDbfs;
        _settingsService.Save(_settings);
    }

    private double Adjust(double value) => LoudnessDisplayScale.Adjust(value, _displayZeroDbfs);

    private static string FormatDb(double value, string suffix) =>
        LoudnessDisplayScale.ShouldDisplay(value)
            ? value.ToString("+0.0;-0.0;0.0", CultureInfo.InvariantCulture) + suffix
            : "−∞";

    private static string FormatLufs(double value) =>
        LoudnessDisplayScale.ShouldDisplay(value)
            ? value.ToString("+0.0;-0.0;0.0", CultureInfo.InvariantCulture)
            : "−∞";

    private static string FormatReference(double value) =>
        value.ToString("0.#", CultureInfo.InvariantCulture);

    private static string FormatThreshold(double value) =>
        value.ToString("+0.#;-0.#;0", CultureInfo.InvariantCulture);

    private static string FormatCompactTemperature(double? value) =>
        value is double temperature && double.IsFinite(temperature)
            ? temperature.ToString("0", CultureInfo.InvariantCulture) + "°"
            : "N/A";

    private static string FormatPercentage(double? value, bool compact) =>
        value is double percentage && double.IsFinite(percentage)
            ? percentage.ToString("0", CultureInfo.InvariantCulture) + (compact ? "%" : " %")
            : "N/A";

    private static int NormalizeRefreshInterval(int value)
    {
        return RefreshIntervals.MinBy(interval => Math.Abs(interval - value));
    }

    private void UpdateRefreshIntervalLabel(int milliseconds)
    {
        string interval = milliseconds < 1000
            ? $"{milliseconds}ms"
            : $"{milliseconds / 1000.0:0.##}s";
        CompactPeakLabel.Text = $"P ({interval})";
    }
    private static string ToUserMessage(Exception exception) => exception switch
    {
        DllNotFoundException => "libebur128.dll is missing. Rebuild the native dependency or use a packaged x64 release.",
        BadImageFormatException => "libebur128.dll is not a compatible x64 library.",
        NotSupportedException => exception.Message,
        InvalidOperationException when exception.Message.StartsWith("libebur128", StringComparison.OrdinalIgnoreCase) => exception.Message,
        _ => "WASAPI loopback capture could not start for this playback device."
    };
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowPos(
        IntPtr window,
        IntPtr insertAfter,
        int x,
        int y,
        int width,
        int height,
        uint flags);
}
