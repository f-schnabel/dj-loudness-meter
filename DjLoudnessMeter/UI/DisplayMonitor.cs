using Microsoft.Win32;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Automation;

namespace DjLoudnessMeter.UI;

public sealed record DisplayMonitor(string DeviceName, string FriendlyName, Rect Bounds, Rect WorkArea, bool IsPrimary)
{
    private const uint MonitorInfoPrimary = 0x00000001;
    private const string ExplorerAdvancedKey = @"Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced";

    public static IReadOnlyList<DisplayMonitor> GetAll()
    {
        List<DisplayMonitor> monitors = [];
        MonitorEnumProc callback = (monitor, _, _, _) =>
        {
            MonitorInfoEx info = new()
            {
                Size = Marshal.SizeOf<MonitorInfoEx>()
            };

            if (GetMonitorInfo(monitor, ref info))
            {
                monitors.Add(new DisplayMonitor(
                    info.DeviceName,
                    string.Empty,
                    ToRect(info.Monitor),
                    ToRect(info.WorkArea),
                    (info.Flags & MonitorInfoPrimary) != 0));
            }

            return true;
        };

        EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, callback, IntPtr.Zero);
        return monitors
            .OrderBy(static monitor => monitor.DeviceName, StringComparer.OrdinalIgnoreCase)
            .Select((monitor, index) => monitor with
            {
                FriendlyName = $"Display {index + 1}" + (monitor.IsPrimary ? " (Main)" : string.Empty)
            })
            .ToArray();
    }

    public static double? GetTaskbarSafeRight(string monitorDeviceName)
    {
        double? result = null;
        WindowEnumProc callback = (window, _) =>
        {
            StringBuilder className = new(64);
            GetClassName(window, className, className.Capacity);
            if (className.ToString() is not ("Shell_TrayWnd" or "Shell_SecondaryTrayWnd"))
            {
                return true;
            }

            IntPtr monitor = MonitorFromWindow(window, 2);
            MonitorInfoEx monitorInfo = new() { Size = Marshal.SizeOf<MonitorInfoEx>() };
            if (!GetMonitorInfo(monitor, ref monitorInfo) ||
                !string.Equals(monitorInfo.DeviceName, monitorDeviceName, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            double? notificationLeft = null;
            WindowEnumProc childCallback = (child, _) =>
            {
                StringBuilder childClassName = new(64);
                GetClassName(child, childClassName, childClassName.Capacity);
                string childClass = childClassName.ToString();
                if (!IsWindowVisible(child) || !GetWindowRect(child, out NativeRect childRect) ||
                    childRect.Right <= childRect.Left)
                {
                    return true;
                }

                if (childClass is "TrayNotifyWnd" or "ClockButton" ||
                    childClass.Contains("SystemTray", StringComparison.OrdinalIgnoreCase))
                {
                    notificationLeft = notificationLeft is null
                        ? childRect.Left
                        : Math.Min(notificationLeft.Value, childRect.Left);
                }

                return true;
            };

            EnumChildWindows(window, childCallback, IntPtr.Zero);
            result = notificationLeft;
            return false;

        };

        EnumWindows(callback, IntPtr.Zero);
        return result;
    }

    public static double? GetTaskbarSafeLeft(string monitorDeviceName)
    {
        if (!AreWidgetsEnabled())
        {
            return null;
        }

        double? result = null;
        WindowEnumProc callback = (window, _) =>
        {
            if (!IsTaskbarOnMonitor(window, monitorDeviceName))
            {
                return true;
            }

            try
            {
                AutomationElement? widgetsButton = AutomationElement.FromHandle(window).FindFirst(
                    TreeScope.Descendants,
                    new PropertyCondition(AutomationElement.AutomationIdProperty, "WidgetsButton"));
                Rect bounds = widgetsButton?.Current.BoundingRectangle ?? Rect.Empty;
                if (!bounds.IsEmpty && bounds.Width > 0)
                {
                    result = bounds.Right;
                }
            }
            catch (ElementNotAvailableException)
            {
                // Explorer can rebuild the taskbar while it is being queried.
            }
            catch (COMException)
            {
                // The taskbar's XAML island can disappear during an Explorer refresh.
            }

            return result is null;
        };

        EnumWindows(callback, IntPtr.Zero);
        return result;
    }

    private static bool AreWidgetsEnabled()
    {
        using RegistryKey? key = Registry.CurrentUser.OpenSubKey(ExplorerAdvancedKey);
        object? value = key?.GetValue("TaskbarDa");
        return value is int enabled ? enabled != 0 : value?.ToString() == "1";
    }

    private static bool IsTaskbarOnMonitor(IntPtr window, string monitorDeviceName)
    {
        StringBuilder className = new(64);
        GetClassName(window, className, className.Capacity);
        if (className.ToString() is not ("Shell_TrayWnd" or "Shell_SecondaryTrayWnd"))
        {
            return false;
        }

        IntPtr monitor = MonitorFromWindow(window, 2);
        MonitorInfoEx monitorInfo = new() { Size = Marshal.SizeOf<MonitorInfoEx>() };
        return GetMonitorInfo(monitor, ref monitorInfo) &&
            string.Equals(monitorInfo.DeviceName, monitorDeviceName, StringComparison.OrdinalIgnoreCase);
    }

    private static Rect ToRect(NativeRect rect) =>
        new(rect.Left, rect.Top, rect.Right - rect.Left, rect.Bottom - rect.Top);

    private delegate bool MonitorEnumProc(IntPtr monitor, IntPtr deviceContext, IntPtr monitorRect, IntPtr data);
    private delegate bool WindowEnumProc(IntPtr window, IntPtr data);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct MonitorInfoEx
    {
        public int Size;
        public NativeRect Monitor;
        public NativeRect WorkArea;
        public uint Flags;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string DeviceName;
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumDisplayMonitors(
        IntPtr deviceContext,
        IntPtr clipRect,
        MonitorEnumProc callback,
        IntPtr data);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfoEx info);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(WindowEnumProc callback, IntPtr data);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr window, StringBuilder className, int maxCount);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumChildWindows(IntPtr parent, WindowEnumProc callback, IntPtr data);

    [DllImport("user32.dll")]
    private static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetWindowRect(IntPtr window, out NativeRect rect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr window);
}
