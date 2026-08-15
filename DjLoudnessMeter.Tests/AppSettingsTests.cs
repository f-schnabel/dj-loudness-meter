using DjLoudnessMeter.Settings;

namespace DjLoudnessMeter.Tests;

public sealed class AppSettingsTests
{
    [Fact]
    public void DisplayZeroDefaultsToMinusNineDbfs()
    {
        Assert.Equal(-9.0, new AppSettings().DisplayZeroDbfs);
    }
}
