using System.Globalization;
using System.IO;

namespace DjLoudnessMeter.Infrastructure;

internal static class AppLog
{
    private static readonly object Gate = new();
    private static readonly string LogDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "DjLoudnessMeter");
    private static readonly string LogPath = Path.Combine(LogDirectory, "meter.log");

    public static void Info(string message) => Write("INFO", message, null);

    public static void Error(string message, Exception? exception = null) => Write("ERROR", message, exception);

    private static void Write(string level, string message, Exception? exception)
    {
        try
        {
            lock (Gate)
            {
                Directory.CreateDirectory(LogDirectory);
                using var writer = new StreamWriter(LogPath, append: true);
                writer.Write(DateTimeOffset.Now.ToString("O", CultureInfo.InvariantCulture));
                writer.Write(" [");
                writer.Write(level);
                writer.Write("] ");
                writer.WriteLine(message);
                if (exception is not null)
                {
                    writer.WriteLine(exception);
                }
            }
        }
        catch
        {
            // Logging must never affect capture or application shutdown.
        }
    }
}
