using System.Diagnostics;

namespace DjLoudnessMeter.Metering;

public sealed class PeakMeter
{
    private const double HoldDecayDbPerSecond = 18.0;
    private readonly long _holdDurationTicks;
    private readonly long _clipDurationTicks;
    private float _displayLeft;
    private float _displayRight;
    private float _latestOverall;
    private float _heldPeak;
    private long _holdUntil;
    private long _clipUntil;

    public PeakMeter(TimeSpan holdDuration, TimeSpan? clipDuration = null)
    {
        if (holdDuration < TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(holdDuration));
        }

        _holdDurationTicks = ToStopwatchTicks(holdDuration);
        _clipDurationTicks = ToStopwatchTicks(clipDuration ?? TimeSpan.FromSeconds(3));
    }

    public float Update(ReadOnlySpan<float> interleavedSamples, int channels, long timestamp)
    {
        if (channels is < 1 or > 2)
        {
            throw new ArgumentOutOfRangeException(nameof(channels));
        }

        float left = 0;
        float right = 0;
        if (channels == 1)
        {
            for (int i = 0; i < interleavedSamples.Length; i++)
            {
                float value = MathF.Abs(interleavedSamples[i]);
                if (value > left)
                {
                    left = value;
                }
            }

            right = left;
        }
        else
        {
            for (int i = 0; i + 1 < interleavedSamples.Length; i += 2)
            {
                float leftValue = MathF.Abs(interleavedSamples[i]);
                float rightValue = MathF.Abs(interleavedSamples[i + 1]);
                if (leftValue > left)
                {
                    left = leftValue;
                }

                if (rightValue > right)
                {
                    right = rightValue;
                }
            }
        }

        _displayLeft = MathF.Max(_displayLeft, left);
        _displayRight = MathF.Max(_displayRight, right);
        _latestOverall = MathF.Max(left, right);
        float currentHold = GetHoldLinear(timestamp);
        if (_latestOverall >= currentHold)
        {
            _heldPeak = _latestOverall;
            _holdUntil = timestamp + _holdDurationTicks;
        }

        if (_latestOverall >= 1.0f)
        {
            _clipUntil = timestamp + _clipDurationTicks;
        }

        return _latestOverall;
    }

    public PeakReading ReadAndResetDisplayPeaks(long timestamp)
    {
        float left = _displayLeft;
        float right = _displayRight;
        float hold = GetHoldLinear(timestamp);
        _displayLeft = 0;
        _displayRight = 0;
        _latestOverall = 0;
        return new PeakReading(left, right, hold, timestamp < _clipUntil);
    }

    public void Reset()
    {
        _displayLeft = 0;
        _displayRight = 0;
        _latestOverall = 0;
        _heldPeak = 0;
        _holdUntil = 0;
        _clipUntil = 0;
    }

    public static float LinearToDb(float amplitude) =>
        amplitude > 0 ? 20.0f * MathF.Log10(amplitude) : float.NegativeInfinity;

    private float GetHoldLinear(long timestamp)
    {
        if (_heldPeak <= 0 || timestamp <= _holdUntil)
        {
            return _heldPeak;
        }

        double seconds = (timestamp - _holdUntil) / (double)Stopwatch.Frequency;
        float decayedDb = LinearToDb(_heldPeak) - (float)(seconds * HoldDecayDbPerSecond);
        float decayedLinear = MathF.Pow(10.0f, decayedDb / 20.0f);
        return MathF.Max(_latestOverall, decayedLinear);
    }

    private static long ToStopwatchTicks(TimeSpan duration) =>
        (long)(duration.TotalSeconds * Stopwatch.Frequency);
}

public readonly record struct PeakReading(float Left, float Right, float Hold, bool IsClipping);
