namespace DjLoudnessMeter.Metering;

public readonly record struct MeterSnapshot(
    float LeftPeakDb,
    float RightPeakDb,
    float PeakDb,
    float PeakHoldDb,
    double MomentaryLufs,
    double ShortTermLufs,
    bool IsClipping,
    bool IsConnected,
    bool HasRecentAudio)
{
    public static MeterSnapshot Disconnected => new(
        float.NegativeInfinity,
        float.NegativeInfinity,
        float.NegativeInfinity,
        float.NegativeInfinity,
        double.NegativeInfinity,
        double.NegativeInfinity,
        false,
        false,
        false);
}
