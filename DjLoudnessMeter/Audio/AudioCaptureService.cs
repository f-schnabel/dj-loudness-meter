using System.Diagnostics;
using DjLoudnessMeter.Infrastructure;
using DjLoudnessMeter.Metering;
using NAudio.CoreAudioApi;
using NAudio.Wave;

namespace DjLoudnessMeter.Audio;

public sealed class AudioCaptureService : IDisposable
{
    private static readonly long RecentAudioTicks = (long)(0.75 * Stopwatch.Frequency);
    private static readonly long SilenceTicks = 5 * Stopwatch.Frequency;
    private const float SignalSilenceThreshold = 0.000001f;
    private readonly object _meterGate = new();
    private readonly TimeSpan _peakHoldDuration;
    private readonly AudioSampleConverter _converter = new();
    private WasapiRecorder? _recorder;
    private MMDevice? _device;
    private AudioFormatInfo _format;
    private PeakMeter? _peakMeter;
    private LoudnessMeter? _loudnessMeter;
    private long _lastBufferTimestamp;
    private long _lastSignalTimestamp;
    private long _captureStartedTimestamp;
    private bool _isConnected;
    private bool _loudnessResetForSilence = true;
    private bool _disposed;

    public AudioCaptureService(TimeSpan peakHoldDuration)
    {
        _peakHoldDuration = peakHoldDuration;
    }

    public event EventHandler<CaptureStoppedEventArgs>? CaptureStopped;

    public string? DeviceName { get; private set; }
    public string? FormatDescription { get; private set; }
    public bool IsCapturing => Volatile.Read(ref _isConnected);

    public void Start(string endpointId)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ArgumentException.ThrowIfNullOrWhiteSpace(endpointId);
        Stop();

        MMDevice? device = null;
        WasapiRecorder? recorder = null;
        LoudnessMeter? loudnessMeter = null;
        try
        {
            using var enumerator = new MMDeviceEnumerator();
            device = enumerator.GetDevice(endpointId);
            recorder = new WasapiRecorderBuilder()
                .WithDevice(device)
                .WithLoopbackCapture()
                .WithBufferLength(100)
                .WithMmcssThreadPriority("Audio")
                .Build();

            AudioFormatInfo format = AudioFormatInfo.FromWaveFormat(recorder.WaveFormat);
            if (format.Encoding == AudioSampleEncoding.Pcm)
            {
                // Reserve for a generous 200 ms packet before the capture thread starts.
                _converter.Prepare((format.SampleRate * format.Channels) / 5);
            }

            loudnessMeter = new LoudnessMeter(format.Channels, format.SampleRate);
            var peakMeter = new PeakMeter(_peakHoldDuration);

            lock (_meterGate)
            {
                _format = format;
                _peakMeter = peakMeter;
                _loudnessMeter = loudnessMeter;
                _lastBufferTimestamp = 0;
                _lastSignalTimestamp = 0;
                _captureStartedTimestamp = Stopwatch.GetTimestamp();
                _loudnessResetForSilence = true;
                _device = device;
                _recorder = recorder;
                DeviceName = device.FriendlyName;
                FormatDescription = $"{format.SampleRate / 1000.0:0.0} kHz · {format.BitsPerSample}-bit {format.Encoding} · {format.Channels} ch";
                _isConnected = true;
            }

            loudnessMeter = null;
            recorder.DataAvailable += OnDataAvailable;
            recorder.RecordingStopped += OnRecordingStopped;
            recorder.StartRecording();
            AppLog.Info($"Capture started: {DeviceName}; {FormatDescription}.");
        }
        catch
        {
            lock (_meterGate)
            {
                _isConnected = false;
                _recorder = null;
                _device = null;
                _loudnessMeter?.Dispose();
                _loudnessMeter = null;
                _peakMeter = null;
            }

            if (recorder is not null)
            {
                recorder.DataAvailable -= OnDataAvailable;
                recorder.RecordingStopped -= OnRecordingStopped;
                recorder.Dispose();
            }

            device?.Dispose();
            loudnessMeter?.Dispose();
            throw;
        }
    }

    public MeterSnapshot GetSnapshot()
    {
        long now = Stopwatch.GetTimestamp();
        lock (_meterGate)
        {
            if (!_isConnected || _peakMeter is null || _loudnessMeter is null)
            {
                return MeterSnapshot.Disconnected;
            }

            long bufferAge = _lastBufferTimestamp == 0 ? long.MaxValue : now - _lastBufferTimestamp;
            long signalReference = _lastSignalTimestamp == 0 ? _captureStartedTimestamp : _lastSignalTimestamp;
            long signalAge = now - signalReference;
            bool silent = signalAge >= SilenceTicks;
            bool hasRecentAudio = bufferAge <= RecentAudioTicks && !silent;
            PeakReading peak = _peakMeter.ReadAndResetDisplayPeaks(now);
            if (silent)
            {
                if (!_loudnessResetForSilence)
                {
                    _loudnessMeter.Dispose();
                    _loudnessMeter = new LoudnessMeter(_format.Channels, _format.SampleRate);
                    _peakMeter.Reset();
                    _loudnessResetForSilence = true;
                }

                return new MeterSnapshot(
                    float.NegativeInfinity,
                    float.NegativeInfinity,
                    float.NegativeInfinity,
                    float.NegativeInfinity,
                    double.NegativeInfinity,
                    double.NegativeInfinity,
                    false,
                    true,
                    false);
            }

            (double momentary, double shortTerm) = _loudnessMeter.ReadLoudness();
            float leftDb = PeakMeter.LinearToDb(peak.Left);
            float rightDb = PeakMeter.LinearToDb(peak.Right);
            return new MeterSnapshot(
                leftDb,
                rightDb,
                MathF.Max(leftDb, rightDb),
                PeakMeter.LinearToDb(peak.Hold),
                momentary,
                shortTerm,
                peak.IsClipping,
                true,
                hasRecentAudio);
        }
    }

    public void Reset()
    {
        lock (_meterGate)
        {
            _peakMeter?.Reset();
            if (_loudnessMeter is not null)
            {
                _loudnessMeter.Dispose();
                _loudnessMeter = new LoudnessMeter(_format.Channels, _format.SampleRate);
            }

            _lastBufferTimestamp = 0;
            _lastSignalTimestamp = 0;
            _captureStartedTimestamp = Stopwatch.GetTimestamp();
            _loudnessResetForSilence = true;
        }
    }

    public void Stop()
    {
        WasapiRecorder? recorder;
        MMDevice? device;
        lock (_meterGate)
        {
            recorder = _recorder;
            device = _device;
            _recorder = null;
            _device = null;
            _isConnected = false;
            _loudnessMeter?.Dispose();
            _loudnessMeter = null;
            _peakMeter = null;
            _lastBufferTimestamp = 0;
            _lastSignalTimestamp = 0;
            _captureStartedTimestamp = 0;
        }

        if (recorder is not null)
        {
            recorder.DataAvailable -= OnDataAvailable;
            recorder.RecordingStopped -= OnRecordingStopped;
            recorder.Dispose();
            AppLog.Info("Capture stopped.");
        }

        device?.Dispose();
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        Stop();
        _disposed = true;
        GC.SuppressFinalize(this);
    }

    private void OnDataAvailable(ReadOnlySpan<byte> buffer, AudioClientBufferFlags flags, long devicePosition, long qpcPosition)
    {
        lock (_meterGate)
        {
            if (!_isConnected || _peakMeter is null || _loudnessMeter is null)
            {
                return;
            }

            ReadOnlySpan<float> samples = _converter.Convert(buffer, _format);
            long now = Stopwatch.GetTimestamp();
            float bufferPeak = _peakMeter.Update(samples, _format.Channels, now);
            _loudnessMeter.AddFrames(samples);
            _lastBufferTimestamp = now;
            if (bufferPeak > SignalSilenceThreshold)
            {
                _lastSignalTimestamp = now;
                _loudnessResetForSilence = false;
            }
        }
    }

    private void OnRecordingStopped(object? sender, StoppedEventArgs e)
    {
        if (!ReferenceEquals(sender, _recorder))
        {
            return;
        }

        string message = e.Exception is null
            ? "Audio capture stopped."
            : "The selected audio device disconnected or WASAPI capture failed.";
        if (e.Exception is not null)
        {
            AppLog.Error(message, e.Exception);
        }

        lock (_meterGate)
        {
            _isConnected = false;
        }

        CaptureStopped?.Invoke(this, new CaptureStoppedEventArgs(message, e.Exception));
    }
}

public sealed class CaptureStoppedEventArgs(string message, Exception? exception) : EventArgs
{
    public string Message { get; } = message;
    public Exception? Exception { get; } = exception;
}
