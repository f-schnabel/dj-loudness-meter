using System.Runtime.InteropServices;
using DjLoudnessMeter.Metering;

namespace DjLoudnessMeter.Tests;

public sealed class LoudnessMeterIntegrationTests
{
    [Theory]
    [InlineData(44100)]
    [InlineData(48000)]
    public void StereoSineMatchesKnownEbuR128Level(int sampleRate)
    {
        if (!NativeLibrary.TryLoad("ebur128", out IntPtr library))
        {
            Assert.Fail("ebur128.dll is required for native integration tests. Run scripts/Build-Native.ps1 first.");
        }

        NativeLibrary.Free(library);
        const float amplitude = 0.1f; // -20 dBFS peak per channel.
        using var meter = new LoudnessMeter(2, sampleRate);
        float[] block = GenerateStereoSine(sampleRate, 1000, amplitude, TimeSpan.FromSeconds(4));

        meter.AddFrames(block);
        (double momentary, double shortTerm) = meter.ReadLoudness();

        Assert.InRange(momentary, -20.2, -19.8);
        Assert.InRange(shortTerm, -20.2, -19.8);
    }

    [Fact]
    public void SilenceReturnsNegativeInfinity()
    {
        if (!NativeLibrary.TryLoad("ebur128", out IntPtr library))
        {
            Assert.Fail("ebur128.dll is required for native integration tests. Run scripts/Build-Native.ps1 first.");
        }

        NativeLibrary.Free(library);
        using var meter = new LoudnessMeter(2, 48000);
        meter.AddFrames(new float[48000 * 2 * 3]);

        (double momentary, double shortTerm) = meter.ReadLoudness();

        Assert.True(double.IsNegativeInfinity(momentary));
        Assert.True(double.IsNegativeInfinity(shortTerm));
    }

    private static float[] GenerateStereoSine(int sampleRate, double frequency, float amplitude, TimeSpan duration)
    {
        int frames = (int)(sampleRate * duration.TotalSeconds);
        var samples = new float[frames * 2];
        for (int frame = 0; frame < frames; frame++)
        {
            float value = amplitude * MathF.Sin(2 * MathF.PI * (float)frequency * frame / sampleRate);
            samples[frame * 2] = value;
            samples[(frame * 2) + 1] = value;
        }

        return samples;
    }
}
