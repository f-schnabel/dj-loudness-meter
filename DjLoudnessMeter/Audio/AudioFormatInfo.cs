using NAudio.Wave;

namespace DjLoudnessMeter.Audio;

public enum AudioSampleEncoding
{
    IeeeFloat,
    Pcm
}

public readonly record struct AudioFormatInfo(
    int SampleRate,
    int Channels,
    int BitsPerSample,
    int ValidBitsPerSample,
    int BlockAlign,
    AudioSampleEncoding Encoding)
{
    private static readonly Guid PcmSubFormat = new("00000001-0000-0010-8000-00AA00389B71");
    private static readonly Guid IeeeFloatSubFormat = new("00000003-0000-0010-8000-00AA00389B71");

    public static AudioFormatInfo FromWaveFormat(WaveFormat format)
    {
        ArgumentNullException.ThrowIfNull(format);

        AudioSampleEncoding encoding;
        int validBits = format.BitsPerSample;
        if (format is WaveFormatExtensible extensible)
        {
            validBits = extensible.ValidBitsPerSample;
            if (extensible.SubFormat == IeeeFloatSubFormat)
            {
                encoding = AudioSampleEncoding.IeeeFloat;
            }
            else if (extensible.SubFormat == PcmSubFormat)
            {
                encoding = AudioSampleEncoding.Pcm;
            }
            else
            {
                throw new NotSupportedException($"Unsupported WASAPI sub-format: {extensible.SubFormat}.");
            }
        }
        else if (format.Encoding == WaveFormatEncoding.IeeeFloat)
        {
            encoding = AudioSampleEncoding.IeeeFloat;
        }
        else if (format.Encoding == WaveFormatEncoding.Pcm)
        {
            encoding = AudioSampleEncoding.Pcm;
        }
        else
        {
            throw new NotSupportedException($"Unsupported WASAPI sample encoding: {format.Encoding}.");
        }

        var result = new AudioFormatInfo(
            format.SampleRate,
            format.Channels,
            format.BitsPerSample,
            validBits,
            format.BlockAlign,
            encoding);
        result.Validate();
        return result;
    }

    public void Validate()
    {
        if (Channels is < 1 or > 2)
        {
            throw new NotSupportedException($"Unsupported channel configuration: {Channels} channels. Select a mono or stereo Windows output device.");
        }

        bool supported = Encoding switch
        {
            AudioSampleEncoding.IeeeFloat => BitsPerSample == 32,
            AudioSampleEncoding.Pcm => BitsPerSample is 16 or 24 or 32,
            _ => false
        };
        if (!supported)
        {
            throw new NotSupportedException($"Unsupported audio format: {Encoding} {BitsPerSample}-bit.");
        }

        if (BlockAlign != Channels * (BitsPerSample / 8))
        {
            throw new NotSupportedException("The audio format has an unsupported block alignment.");
        }
    }
}
