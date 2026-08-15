namespace DjLoudnessMeter.Settings;

public sealed class AppSettings
{
    public string? SelectedAudioEndpointId { get; set; }
    public double WindowLeft { get; set; } = double.NaN;
    public double WindowTop { get; set; } = double.NaN;
    public double WindowWidth { get; set; } = 440;
    public double WindowHeight { get; set; } = 520;
    public bool AlwaysOnTop { get; set; }
    public bool CompactMode { get; set; } = true;
    public bool TaskbarMode { get; set; }
    public string? TaskbarMonitorDeviceName { get; set; }
    public bool TaskbarRightAligned { get; set; }
    public int UiRefreshMilliseconds { get; set; } = 500;
    public int PeakHoldMilliseconds { get; set; } = 5000;
    public double DisplayZeroDbfs { get; set; } = -9.0;
}
