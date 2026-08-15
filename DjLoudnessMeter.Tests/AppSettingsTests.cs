using DjLoudnessMeter.Settings;

namespace DjLoudnessMeter.Tests;

public sealed class AppSettingsTests
{
    [Fact]
    public void DisplayZeroDefaultsToMinusNineDbfs()
    {
        Assert.Equal(-9.0, new AppSettings().DisplayZeroDbfs);
    }

    [Fact]
    public void TaskbarMonitorDefaultsToAutomaticSelection()
    {
        Assert.Null(new AppSettings().TaskbarMonitorDeviceName);
    }

    [Fact]
    public void TaskbarSideDefaultsToLeft()
    {
        Assert.False(new AppSettings().TaskbarRightAligned);
    }

    [Fact]
    public void UiRefreshDefaultsToFiveHundredMilliseconds()
    {
        Assert.Equal(500, new AppSettings().UiRefreshMilliseconds);
    }
}
