using NAudio.CoreAudioApi;

namespace DjLoudnessMeter.Audio;

public sealed record AudioDeviceInfo(string Id, string FriendlyName, bool IsDefault)
{
    public override string ToString() => FriendlyName;
}

public sealed class AudioDeviceService
{
    public IReadOnlyList<AudioDeviceInfo> GetActiveRenderDevices()
    {
        using var enumerator = new MMDeviceEnumerator();
        string? defaultId = TryGetDefaultDeviceId(enumerator);
        MMDeviceCollection devices = enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active);
        var result = new List<AudioDeviceInfo>(devices.Count);
        foreach (MMDevice device in devices)
        {
            using (device)
            {
                result.Add(new AudioDeviceInfo(device.ID, device.FriendlyName, device.ID == defaultId));
            }
        }

        result.Sort(static (left, right) =>
        {
            if (left.IsDefault != right.IsDefault)
            {
                return left.IsDefault ? -1 : 1;
            }

            return StringComparer.CurrentCultureIgnoreCase.Compare(left.FriendlyName, right.FriendlyName);
        });
        return result;
    }

    public string? GetDefaultRenderDeviceId()
    {
        using var enumerator = new MMDeviceEnumerator();
        return TryGetDefaultDeviceId(enumerator);
    }

    private static string? TryGetDefaultDeviceId(MMDeviceEnumerator enumerator)
    {
        try
        {
            using MMDevice device = enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
            return device.ID;
        }
        catch
        {
            return null;
        }
    }
}
