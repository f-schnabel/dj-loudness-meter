# DJ Loudness Meter

A small Windows 10/11 x64 WPF meter for the playback device receiving Rekordbox **PC MASTER OUT**. It observes the selected render endpoint through WASAPI loopback; it does not route, record, retransmit, or modify audio and therefore does not sit in the playback path.

The UI contains stereo sample peak, peak hold, clip state, LUFS-M, LUFS-S, a low-overhead Windows ACPI thermal-zone temperature, total CPU/RAM usage, a large dB-scaled master meter, and playback-device selection. There is deliberately no FFT, spectrum, waveform, recording, Rekordbox integration, or raw audio history.

Compact mode is a borderless, draggable 390×42 numeric strip labeled P (500ms), P (5s), LUFS (0.4s), LUFS (3s), Temp, CPU, and RAM. The labels sit above their values so the complete names fit in the taskbar footprint. The complete display updates as one snapshot every 500 ms, with P (500ms) reporting the maximum captured during that interval and P (5s) holding the peak for five seconds. Double-click it to return to device configuration. Peak values turn amber at −6 dBFS and red at −1 dBFS; LUFS values turn amber at −12 LUFS and red at −9 LUFS. Five seconds of digital silence clears Peak, Hold, LUFS, and the clip state to −∞. Temperature shows N/A if Windows does not expose an ACPI thermal zone.

The normal window's **Zero at** field configures a display reference from −30 to 0 dBFS and defaults to −9 dBFS. At `-9`, the meter adds 9 dB to P/H/S/M and the large meter, so a raw −9 dB value reads 0 dB and a raw 0 dB value reads +9 dB. Scale labels, colored meter bands, and numeric warning thresholds shift by the same amount. A value of `0` restores the unadjusted readings.

Adjusted P/H/LUFS values display down to −99.0 dB. Values below −99.0 dB are rendered as −∞.

Taskbar mode places the same strip over the static bottom-left taskbar area without injecting into Windows Explorer. Double-click the strip to return to normal mode.

## Requirements

- Windows 10 2004 or newer, or Windows 11, x64
- .NET 9 SDK for development
- Visual Studio 2022 Build Tools with the C++ workload plus [vcpkg](https://github.com/microsoft/vcpkg) to build the native dependency
- NAudio 3 (`WasapiRecorder`) and libebur128 1.2.6

NAudio 3 is currently a prerelease package. The project pins `3.0.0-preview.19`, whose modern recorder provides the span-based, zero-copy callback used here.

## Build

Set `VCPKG_ROOT` to a bootstrapped vcpkg checkout, then run:

```powershell
./scripts/Build-Native.ps1
dotnet test DjLoudnessMeter.sln -c Release -p:Platform=x64
dotnet run --project DjLoudnessMeter/DjLoudnessMeter.csproj -c Release
```

`Build-Native.ps1` builds the official MIT-licensed libebur128 port and copies only `ebur128.dll` into the application's RID-specific native directory. The DLL is intentionally not committed to source control.

Create a self-contained Windows x64 distribution with:

```powershell
./scripts/Publish.ps1
```

The release workflow performs the same native build, DSP tests, and publish process on a Windows runner.

## Rekordbox setup

1. In Rekordbox, keep the controller (for example, DDJ-400) as the main audio device.
2. Enable **PC MASTER OUT** and choose a Windows playback endpoint.
3. Start DJ Loudness Meter and select that same Windows endpoint.
4. Play audio. The status changes from “waiting for audio” when loopback packets arrive.

WASAPI loopback observes everything rendered to the selected endpoint, not Rekordbox alone. Avoid routing unrelated system sounds to that endpoint during a set if they should not affect the meter.

## Design notes

- Shared-mode loopback uses `WasapiRecorderBuilder.WithDevice(...).WithLoopbackCapture()` with a 100 ms buffer. Low-latency mode is not enabled.
- IEEE float input is passed to metering without a conversion copy. PCM 16/24/32 uses one reusable growth-only float buffer.
- The capture callback calculates interval sample peaks and feeds libebur128. It never dispatches UI work, performs file I/O, uses LINQ, or retains raw samples.
- libebur128 uses only `EBUR128_MODE_S`, which includes Momentary support. Integrated loudness, LRA, sample-peak, and true-peak modes are not enabled.
- The WPF timer samples the latest state, Windows thermal-zone counter, total CPU utilization, and physical-memory utilization at 2 Hz. The temperature read does not require HWiNFO or another monitoring process. Visual release smoothing is display-only.
- If loopback stops producing packets, the visual meter decays immediately and LUFS/peak values become silence after five seconds. Capture remains armed and resumes automatically.
- Taskbar mode is a deliberately simple borderless topmost overlay at the bottom-left of the primary display; it does not inject into Windows Explorer.
- Settings, including the display-zero reference, are stored in a small JSON file under the application data DjLoudnessMeter folder.

## Accuracy and endurance validation

Automated tests cover silence, stereo channel separation, known 0/−3/−6/−12 dBFS sine peaks, PCM 16/24/32 conversion, 44.1/48 kHz loudness integration, and a known steady stereo sine loudness. Native loudness tests run after `Build-Native.ps1` has supplied `ebur128.dll`.

Before publishing a release, also run the Release build with music for at least 30 minutes and inspect CPU, working set, allocation rate, UI render activity, and capture-thread timing in Visual Studio Profiler or PerfView. The implementation has no growing queue or application-owned audio history, but an endurance profile on the target audio driver is still required.

For the manual Rekordbox check, verify that channel TRIM, EQ, track pausing/resuming, and mixing two tracks change the readings as expected. Changing the controller's physical analog master level should not alter the digital PC MASTER OUT copy being measured.
