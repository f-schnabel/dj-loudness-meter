using System.Runtime.InteropServices;

namespace DjLoudnessMeter.Native;

internal static unsafe class Ebur128Native
{
    internal const int Success = 0;
    internal const int ModeShortTerm = (1 << 1) | (1 << 0);

    [DllImport("ebur128", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern IntPtr ebur128_init(uint channels, uint sampleRate, int mode);

    [DllImport("ebur128", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern void ebur128_destroy(ref IntPtr state);

    [DllImport("ebur128", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int ebur128_add_frames_float(IntPtr state, float* source, nuint frames);

    [DllImport("ebur128", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int ebur128_loudness_momentary(IntPtr state, out double loudness);

    [DllImport("ebur128", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int ebur128_loudness_shortterm(IntPtr state, out double loudness);
}
