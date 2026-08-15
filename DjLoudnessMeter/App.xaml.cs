using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Threading;
using DjLoudnessMeter.Infrastructure;

namespace DjLoudnessMeter;

public partial class App : Application
{
    private const uint AttachParentProcess = 0xFFFFFFFF;
    private ConsoleControlHandler? _consoleControlHandler;

    protected override void OnStartup(StartupEventArgs e)
    {
        AppLog.Info("Application starting.");
        DispatcherUnhandledException += OnDispatcherUnhandledException;
        AttachConsole(AttachParentProcess);
        _consoleControlHandler = OnConsoleControl;
        SetConsoleCtrlHandler(_consoleControlHandler, add: true);
        base.OnStartup(e);

        UI.MainWindow window = new();
        MainWindow = window;
        window.Start();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_consoleControlHandler is not null)
        {
            SetConsoleCtrlHandler(_consoleControlHandler, add: false);
            _consoleControlHandler = null;
        }

        AppLog.Info("Application exiting.");
        base.OnExit(e);
    }

    private bool OnConsoleControl(ConsoleControlType controlType)
    {
        if (controlType is not (ConsoleControlType.CtrlC or ConsoleControlType.CtrlBreak))
        {
            return false;
        }

        Dispatcher.BeginInvoke(() =>
        {
            if (MainWindow is UI.MainWindow window)
            {
                window.CloseApplication();
            }
            else
            {
                Shutdown();
            }
        });
        return true;
    }

    private static void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        AppLog.Error("Unexpected UI exception.", e.Exception);
        MessageBox.Show(
            "The loudness meter encountered an unexpected error. Technical details were written to the log.",
            "DJ Loudness Meter",
            MessageBoxButton.OK,
            MessageBoxImage.Error);
        e.Handled = true;
    }

    private delegate bool ConsoleControlHandler(ConsoleControlType controlType);

    private enum ConsoleControlType : uint
    {
        CtrlC = 0,
        CtrlBreak = 1
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AttachConsole(uint processId);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetConsoleCtrlHandler(ConsoleControlHandler? handler, [MarshalAs(UnmanagedType.Bool)] bool add);
}
