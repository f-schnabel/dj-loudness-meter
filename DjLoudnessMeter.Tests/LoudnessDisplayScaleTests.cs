using DjLoudnessMeter.Metering;

namespace DjLoudnessMeter.Tests;

public sealed class LoudnessDisplayScaleTests
{
    [Theory]
    [InlineData(-9.0, 0.0)]
    [InlineData(-6.0, 3.0)]
    [InlineData(0.0, 9.0)]
    public void MinusNineReferenceShiftsReadingsByNineDb(double input, double expected)
    {
        Assert.Equal(expected, LoudnessDisplayScale.Adjust(input, -9.0));
    }

    [Theory]
    [InlineData(-50.0, -30.0)]
    [InlineData(5.0, 0.0)]
    [InlineData(double.NaN, 0.0)]
    public void ReferenceIsKeptWithinSupportedRange(double input, double expected)
    {
        Assert.Equal(expected, LoudnessDisplayScale.NormalizeReference(input));
    }

    [Fact]
    public void SilenceRemainsNegativeInfinity()
    {
        Assert.True(double.IsNegativeInfinity(
            LoudnessDisplayScale.Adjust(double.NegativeInfinity, -9.0)));
    }

    [Theory]
    [InlineData(-99.0, true)]
    [InlineData(-99.01, false)]
    [InlineData(double.NegativeInfinity, false)]
    [InlineData(9.0, true)]
    public void DisplayFloorTreatsValuesBelowMinusNinetyNineAsSilence(double value, bool expected)
    {
        Assert.Equal(expected, LoudnessDisplayScale.ShouldDisplay(value));
    }
}
