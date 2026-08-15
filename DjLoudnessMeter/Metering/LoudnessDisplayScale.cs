namespace DjLoudnessMeter.Metering;

public static class LoudnessDisplayScale
{
    public const double MinimumReferenceDbfs = -30.0;
    public const double MaximumReferenceDbfs = 0.0;
    public const double MinimumDisplayedDb = -99.0;

    public static double NormalizeReference(double referenceDbfs) =>
        double.IsFinite(referenceDbfs)
            ? Math.Clamp(referenceDbfs, MinimumReferenceDbfs, MaximumReferenceDbfs)
            : 0.0;

    public static double GetOffset(double referenceDbfs) => -NormalizeReference(referenceDbfs);

    public static double Adjust(double value, double referenceDbfs) =>
        double.IsFinite(value) ? value + GetOffset(referenceDbfs) : value;

    public static bool ShouldDisplay(double adjustedValue) =>
        double.IsFinite(adjustedValue) && adjustedValue >= MinimumDisplayedDb;
}
