using System.Runtime.InteropServices;
using DjLoudnessMeter.Metering;

namespace DjLoudnessMeter.Tests;

public sealed class LoudnessDynamicsIntegrationTests
{
    [Theory]
    [InlineData(44100)]
    [InlineData(48000)]
    public void DeterministicPinkNoiseTracksARequestedSixDbLevelChange(int sampleRate)
    {
        EnsureNativeAvailable();
        float[] quietNoise = GenerateStereoPinkNoise(sampleRate, 4, 0.04f, 0x6D2B79F5u);
        float[] loudNoise = new float[quietNoise.Length];
        for (int i = 0; i < quietNoise.Length; i++)
        {
            loudNoise[i] = quietNoise[i] * 2.0f;
        }

        using var quietMeter = new LoudnessMeter(2, sampleRate);
        using var loudMeter = new LoudnessMeter(2, sampleRate);
        quietMeter.AddFrames(quietNoise);
        loudMeter.AddFrames(loudNoise);

        double difference = loudMeter.ReadLoudness().ShortTerm - quietMeter.ReadLoudness().ShortTerm;

        Assert.InRange(difference, 5.99, 6.05);
    }

    [Fact]
    public void MomentaryAndShortTermFollowARecentLevelIncrease()
    {
        EnsureNativeAvailable();
        const int sampleRate = 48000;
        using var meter = new LoudnessMeter(2, sampleRate);
        meter.AddFrames(GenerateStereoSine(sampleRate, 1000, DbToAmplitude(-30), 3));
        (double quietMomentary, double quietShortTerm) = meter.ReadLoudness();

        meter.AddFrames(GenerateStereoSine(sampleRate, 1000, DbToAmplitude(-12), 3));
        (double loudMomentary, double loudShortTerm) = meter.ReadLoudness();

        Assert.InRange(loudMomentary - quietMomentary, 17.8, 18.2);
        Assert.InRange(loudShortTerm - quietShortTerm, 17.8, 18.2);
    }

    private static void EnsureNativeAvailable()
    {
        if (!NativeLibrary.TryLoad("ebur128", out IntPtr library))
        {
            Assert.Fail("ebur128.dll is required for native integration tests. Run scripts/Build-Native.ps1 first.");
        }

        NativeLibrary.Free(library);
    }

    private static float[] GenerateStereoSine(int sampleRate, double frequency, float amplitude, int seconds)
    {
        int frames = sampleRate * seconds;
        var samples = new float[frames * 2];
        for (int frame = 0; frame < frames; frame++)
        {
            float value = amplitude * MathF.Sin(2 * MathF.PI * (float)frequency * frame / sampleRate);
            samples[frame * 2] = value;
            samples[(frame * 2) + 1] = value;
        }

        return samples;
    }

    private static float[] GenerateStereoPinkNoise(int sampleRate, int seconds, float amplitude, uint seed)
    {
        int frames = sampleRate * seconds;
        var samples = new float[frames * 2];
        double b0 = 0;
        double b1 = 0;
        double b2 = 0;
        double b3 = 0;
        double b4 = 0;
        double b5 = 0;
        double b6 = 0;
        uint state = seed;
        for (int frame = 0; frame < frames; frame++)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            double white = ((state / (double)uint.MaxValue) * 2.0) - 1.0;
            b0 = (0.99886 * b0) + (white * 0.0555179);
            b1 = (0.99332 * b1) + (white * 0.0750759);
            b2 = (0.96900 * b2) + (white * 0.1538520);
            b3 = (0.86650 * b3) + (white * 0.3104856);
            b4 = (0.55000 * b4) + (white * 0.5329522);
            b5 = (-0.7616 * b5) - (white * 0.0168980);
            double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + (white * 0.5362);
            b6 = white * 0.115926;
            float value = (float)(pink * 0.11) * amplitude;
            samples[frame * 2] = value;
            samples[(frame * 2) + 1] = value;
        }

        return samples;
    }

    private static float DbToAmplitude(double db) => MathF.Pow(10.0f, (float)db / 20.0f);
}
