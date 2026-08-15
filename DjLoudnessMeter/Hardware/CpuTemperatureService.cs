using System.Runtime.InteropServices;
using DjLoudnessMeter.Infrastructure;

namespace DjLoudnessMeter.Hardware;

public sealed class CpuTemperatureService : IDisposable
{
    private const string TemperatureCounterPath = @"\Thermal Zone Information(*)\Temperature";
    private const uint ErrorSuccess = 0;
    private const uint PdhMoreData = 0x800007D2;
    private const uint PdhFormatDouble = 0x00000200;
    private const uint PdhStatusValidData = 0x00000000;
    private const uint PdhStatusNewData = 0x00000001;
    private const double KelvinOffset = 273.15;
    private IntPtr _query;
    private IntPtr _counter;
    private IntPtr _valueBuffer;
    private uint _valueBufferSize;
    private bool _disposed;

    public CpuTemperatureService()
    {
        try
        {
            uint result = PdhOpenQuery(null, IntPtr.Zero, out _query);
            ThrowIfError(result, "open the thermal counter query");
            result = PdhAddEnglishCounter(_query, TemperatureCounterPath, IntPtr.Zero, out _counter);
            ThrowIfError(result, "add the thermal-zone temperature counter");
        }
        catch (Exception ex)
        {
            AppLog.Error("Windows thermal-zone monitoring is unavailable.", ex);
            ReleaseResources();
        }
    }

    public double? ReadCelsius()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_query == IntPtr.Zero || _counter == IntPtr.Zero)
        {
            return null;
        }

        try
        {
            uint result = PdhCollectQueryData(_query);
            if (result != ErrorSuccess)
            {
                DisableAfterReadFailure(result, "collect thermal-zone data");
                return null;
            }

            uint itemCount = 0;
            uint bufferSize = _valueBufferSize;
            result = PdhGetFormattedCounterArray(
                _counter,
                PdhFormatDouble,
                ref bufferSize,
                ref itemCount,
                _valueBuffer);
            if (result == PdhMoreData)
            {
                ResizeValueBuffer(bufferSize);
                bufferSize = _valueBufferSize;
                result = PdhGetFormattedCounterArray(
                    _counter,
                    PdhFormatDouble,
                    ref bufferSize,
                    ref itemCount,
                    _valueBuffer);
            }

            if (result != ErrorSuccess)
            {
                DisableAfterReadFailure(result, "read thermal-zone data");
                return null;
            }

            double? hottestKelvin = null;
            int itemSize = Marshal.SizeOf<PdhFormattedCounterValueItem>();
            for (uint itemIndex = 0; itemIndex < itemCount; itemIndex++)
            {
                IntPtr itemAddress = IntPtr.Add(_valueBuffer, checked((int)itemIndex * itemSize));
                PdhFormattedCounterValueItem item =
                    Marshal.PtrToStructure<PdhFormattedCounterValueItem>(itemAddress);
                if (item.Value.Status is not (PdhStatusValidData or PdhStatusNewData) ||
                    !double.IsFinite(item.Value.DoubleValue))
                {
                    continue;
                }

                hottestKelvin = hottestKelvin is null
                    ? item.Value.DoubleValue
                    : Math.Max(hottestKelvin.Value, item.Value.DoubleValue);
            }

            return hottestKelvin - KelvinOffset;
        }
        catch (Exception ex)
        {
            AppLog.Error("Windows thermal-zone temperature monitoring was disabled after a read failure.", ex);
            ReleaseResources();
            return null;
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        ReleaseResources();
        GC.SuppressFinalize(this);
    }

    private void ResizeValueBuffer(uint requiredSize)
    {
        if (_valueBuffer != IntPtr.Zero)
        {
            Marshal.FreeHGlobal(_valueBuffer);
        }

        _valueBuffer = Marshal.AllocHGlobal(checked((int)requiredSize));
        _valueBufferSize = requiredSize;
    }

    private void DisableAfterReadFailure(uint result, string operation)
    {
        AppLog.Error($"Windows thermal-zone monitoring was disabled: PDH could not {operation} (error 0x{result:X8}).");
        ReleaseResources();
    }

    private void ReleaseResources()
    {
        if (_valueBuffer != IntPtr.Zero)
        {
            Marshal.FreeHGlobal(_valueBuffer);
            _valueBuffer = IntPtr.Zero;
            _valueBufferSize = 0;
        }

        if (_query != IntPtr.Zero)
        {
            PdhCloseQuery(_query);
            _query = IntPtr.Zero;
            _counter = IntPtr.Zero;
        }
    }

    private static void ThrowIfError(uint result, string operation)
    {
        if (result != ErrorSuccess)
        {
            throw new InvalidOperationException($"PDH could not {operation} (error 0x{result:X8}).");
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly struct PdhFormattedCounterValueItem
    {
        public readonly IntPtr Name;
        public readonly PdhFormattedCounterValue Value;
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly struct PdhFormattedCounterValue
    {
        public readonly uint Status;
        public readonly double DoubleValue;
    }

    [DllImport("pdh.dll", CharSet = CharSet.Unicode, EntryPoint = "PdhOpenQueryW")]
    private static extern uint PdhOpenQuery(string? dataSource, IntPtr userData, out IntPtr query);

    [DllImport("pdh.dll", CharSet = CharSet.Unicode, EntryPoint = "PdhAddEnglishCounterW")]
    private static extern uint PdhAddEnglishCounter(
        IntPtr query,
        string fullCounterPath,
        IntPtr userData,
        out IntPtr counter);

    [DllImport("pdh.dll")]
    private static extern uint PdhCollectQueryData(IntPtr query);

    [DllImport("pdh.dll", EntryPoint = "PdhGetFormattedCounterArrayW")]
    private static extern uint PdhGetFormattedCounterArray(
        IntPtr counter,
        uint format,
        ref uint bufferSize,
        ref uint itemCount,
        IntPtr itemBuffer);

    [DllImport("pdh.dll")]
    private static extern uint PdhCloseQuery(IntPtr query);
}
