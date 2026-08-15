using DjLoudnessMeter.Native;

namespace DjLoudnessMeter.Metering;

public sealed class LoudnessMeter : IDisposable
{
    private IntPtr _state;
    private bool _disposed;

    public LoudnessMeter(int channels, int sampleRate)
    {
        if (channels is < 1 or > 2)
        {
            throw new NotSupportedException("Only mono and stereo loudness measurement are supported.");
        }

        if (sampleRate <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(sampleRate));
        }

        _state = Ebur128Native.ebur128_init((uint)channels, (uint)sampleRate, Ebur128Native.ModeShortTerm);
        if (_state == IntPtr.Zero)
        {
            throw new InvalidOperationException("libebur128 could not initialize its loudness state.");
        }

        Channels = channels;
        SampleRate = sampleRate;
    }

    public int Channels { get; }
    public int SampleRate { get; }

    public unsafe void AddFrames(ReadOnlySpan<float> interleavedSamples)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (interleavedSamples.Length % Channels != 0)
        {
            throw new ArgumentException("The sample count must contain complete interleaved frames.", nameof(interleavedSamples));
        }

        if (interleavedSamples.IsEmpty)
        {
            return;
        }

        fixed (float* samples = interleavedSamples)
        {
            int result = Ebur128Native.ebur128_add_frames_float(
                _state,
                samples,
                (nuint)(interleavedSamples.Length / Channels));
            ThrowIfError(result, "add audio frames");
        }
    }

    public (double Momentary, double ShortTerm) ReadLoudness()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        int momentaryResult = Ebur128Native.ebur128_loudness_momentary(_state, out double momentary);
        ThrowIfError(momentaryResult, "read momentary loudness");
        int shortTermResult = Ebur128Native.ebur128_loudness_shortterm(_state, out double shortTerm);
        ThrowIfError(shortTermResult, "read short-term loudness");
        return (NormalizeSilence(momentary), NormalizeSilence(shortTerm));
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        if (_state != IntPtr.Zero)
        {
            Ebur128Native.ebur128_destroy(ref _state);
        }

        GC.SuppressFinalize(this);
    }

    private static double NormalizeSilence(double loudness) =>
        double.IsFinite(loudness) ? loudness : double.NegativeInfinity;

    private static void ThrowIfError(int result, string operation)
    {
        if (result != Ebur128Native.Success)
        {
            throw new InvalidOperationException($"libebur128 failed to {operation} (error {result}).");
        }
    }
}
