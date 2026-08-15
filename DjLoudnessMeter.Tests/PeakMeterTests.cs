using System.Diagnostics;
using DjLoudnessMeter.Metering;

namespace DjLoudnessMeter.Tests;

public sealed class PeakMeterTests
{
    [Theory]
    [InlineData(0.0, 0.0)]
    [InlineData(-3.0, -3.0)]
    [InlineData(-6.0, -6.0)]
    [InlineData(-12.0, -12.0)]
    public void ReportsKnownSinePeak(double requestedDb, double expectedDb)
    {
        const int sampleRate = 48000;
        float amplitude = MathF.Pow(10, (float)requestedDb / 20.0f);
        float[] stereo = GenerateStereoSine(sampleRate, 1000, amplitude, TimeSpan.FromMilliseconds(100));
        var meter = new PeakMeter(TimeSpan.FromSeconds(2));
        long now = Stopwatch.GetTimestamp();

        meter.Update(stereo, 2, now);
        PeakReading reading = meter.ReadAndResetDisplayPeaks(now);

        Assert.InRange(PeakMeter.LinearToDb(reading.Left), (float)expectedDb - 0.0002f, (float)expectedDb + 0.0002f);
        Assert.InRange(PeakMeter.LinearToDb(reading.Right), (float)expectedDb - 0.0002f, (float)expectedDb + 0.0002f);
    }

    [Fact]
    public void SilenceIsNegativeInfinityDbfs()
    {
        var meter = new PeakMeter(TimeSpan.FromSeconds(2));
        long now = Stopwatch.GetTimestamp();

        meter.Update(new float[960], 2, now);
        PeakReading reading = meter.ReadAndResetDisplayPeaks(now);

        Assert.True(float.IsNegativeInfinity(PeakMeter.LinearToDb(reading.Left)));
        Assert.True(float.IsNegativeInfinity(PeakMeter.LinearToDb(reading.Right)));
    }

    [Fact]
    public void StereoChannelsAreMeasuredIndependently()
    {
        float[] stereo = [0.1f, -0.25f, -0.5f, 0.125f];
        var meter = new PeakMeter(TimeSpan.FromSeconds(2));
        long now = Stopwatch.GetTimestamp();

        meter.Update(stereo, 2, now);
        PeakReading reading = meter.ReadAndResetDisplayPeaks(now);

        Assert.Equal(0.5f, reading.Left);
        Assert.Equal(0.25f, reading.Right);
    }

    [Fact]
    public void FullScaleSetsClipAndResetClearsIt()
    {
        var meter = new PeakMeter(TimeSpan.FromSeconds(2));
        long now = Stopwatch.GetTimestamp();
        meter.Update([1.0f, 0.0f], 2, now);

        Assert.True(meter.ReadAndResetDisplayPeaks(now).IsClipping);
        meter.Reset();
        Assert.False(meter.ReadAndResetDisplayPeaks(now).IsClipping);
    }

    private static float[] GenerateStereoSine(int sampleRate, double frequency, float amplitude, TimeSpan duration)
    {
        int frames = (int)(sampleRate * duration.TotalSeconds);
        var result = new float[frames * 2];
        for (int frame = 0; frame < frames; frame++)
        {
            float sample = amplitude * MathF.Sin(2 * MathF.PI * (float)frequency * frame / sampleRate);
            result[frame * 2] = sample;
            result[(frame * 2) + 1] = sample;
        }

        return result;
    }
}
