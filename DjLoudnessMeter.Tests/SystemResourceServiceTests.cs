using DjLoudnessMeter.Hardware;

namespace DjLoudnessMeter.Tests;

public sealed class SystemResourceServiceTests
{
    [Fact]
    public void ReadsPlausibleSystemResourcePercentages()
    {
        var service = new SystemResourceService();

        SystemResourceSnapshot first = service.Read();
        Thread.Sleep(25);
        SystemResourceSnapshot second = service.Read();

        Assert.Null(first.CpuUsagePercent);
        Assert.NotNull(second.CpuUsagePercent);
        Assert.InRange(second.CpuUsagePercent.Value, 0.0, 100.0);
        Assert.NotNull(second.MemoryUsagePercent);
        Assert.InRange(second.MemoryUsagePercent.Value, 0.0, 100.0);
    }
}
