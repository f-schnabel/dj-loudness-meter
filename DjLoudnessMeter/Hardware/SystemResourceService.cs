using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

namespace DjLoudnessMeter.Hardware;

public sealed class SystemResourceService
{
    private ulong? _previousIdleTime;
    private ulong? _previousKernelTime;
    private ulong? _previousUserTime;

    public SystemResourceSnapshot Read()
    {
        double? cpuUsage = ReadCpuUsage();
        double? memoryUsage = ReadMemoryUsage();
        return new SystemResourceSnapshot(cpuUsage, memoryUsage);
    }

    private double? ReadCpuUsage()
    {
        if (!GetSystemTimes(out FILETIME idleTime, out FILETIME kernelTime, out FILETIME userTime))
        {
            return null;
        }

        ulong idle = ToUInt64(idleTime);
        ulong kernel = ToUInt64(kernelTime);
        ulong user = ToUInt64(userTime);
        double? usage = null;
        if (_previousIdleTime is ulong previousIdle &&
            _previousKernelTime is ulong previousKernel &&
            _previousUserTime is ulong previousUser &&
            idle >= previousIdle &&
            kernel >= previousKernel &&
            user >= previousUser)
        {
            ulong idleDelta = idle - previousIdle;
            ulong kernelDelta = kernel - previousKernel;
            ulong userDelta = user - previousUser;
            ulong totalDelta = kernelDelta + userDelta;
            if (totalDelta > 0 && idleDelta <= totalDelta)
            {
                usage = Math.Clamp(
                    100.0 * (totalDelta - idleDelta) / totalDelta,
                    0.0,
                    100.0);
            }
        }

        _previousIdleTime = idle;
        _previousKernelTime = kernel;
        _previousUserTime = user;
        return usage;
    }

    private static double? ReadMemoryUsage()
    {
        var status = new MemoryStatus
        {
            Length = (uint)Marshal.SizeOf<MemoryStatus>()
        };
        if (!GlobalMemoryStatusEx(ref status) || status.TotalPhysical == 0)
        {
            return null;
        }

        return Math.Clamp(
            100.0 * (status.TotalPhysical - status.AvailablePhysical) / status.TotalPhysical,
            0.0,
            100.0);
    }

    private static ulong ToUInt64(FILETIME value) =>
        ((ulong)(uint)value.dwHighDateTime << 32) | (uint)value.dwLowDateTime;

    [StructLayout(LayoutKind.Sequential)]
    private struct MemoryStatus
    {
        public uint Length;
        public uint MemoryLoad;
        public ulong TotalPhysical;
        public ulong AvailablePhysical;
        public ulong TotalPageFile;
        public ulong AvailablePageFile;
        public ulong TotalVirtual;
        public ulong AvailableVirtual;
        public ulong AvailableExtendedVirtual;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetSystemTimes(
        out FILETIME idleTime,
        out FILETIME kernelTime,
        out FILETIME userTime);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GlobalMemoryStatusEx(ref MemoryStatus buffer);
}

public readonly record struct SystemResourceSnapshot(
    double? CpuUsagePercent,
    double? MemoryUsagePercent);
