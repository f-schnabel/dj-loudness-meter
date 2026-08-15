using System.Text.Json;
using System.IO;
using DjLoudnessMeter.Infrastructure;

namespace DjLoudnessMeter.Settings;

public sealed class SettingsService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    private readonly string _settingsPath;

    public SettingsService()
    {
        _settingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "DjLoudnessMeter",
            "settings.json");
    }

    public AppSettings Load()
    {
        try
        {
            if (!File.Exists(_settingsPath))
            {
                return new AppSettings();
            }

            string json = File.ReadAllText(_settingsPath);
            return JsonSerializer.Deserialize<AppSettings>(json, JsonOptions) ?? new AppSettings();
        }
        catch (Exception ex)
        {
            AppLog.Error("Could not load settings; defaults will be used.", ex);
            return new AppSettings();
        }
    }

    public void Save(AppSettings settings)
    {
        try
        {
            string? directory = Path.GetDirectoryName(_settingsPath);
            if (directory is not null)
            {
                Directory.CreateDirectory(directory);
            }

            string temporaryPath = _settingsPath + ".tmp";
            File.WriteAllText(temporaryPath, JsonSerializer.Serialize(settings, JsonOptions));
            File.Move(temporaryPath, _settingsPath, overwrite: true);
        }
        catch (Exception ex)
        {
            AppLog.Error("Could not save settings.", ex);
        }
    }
}
