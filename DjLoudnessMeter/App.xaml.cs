using System.Windows;
using System.Windows.Threading;
using DjLoudnessMeter.Infrastructure;

namespace DjLoudnessMeter;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        AppLog.Info("Application starting.");
        DispatcherUnhandledException += OnDispatcherUnhandledException;
        base.OnStartup(e);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        AppLog.Info("Application exiting.");
        base.OnExit(e);
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
}
