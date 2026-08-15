using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace DjLoudnessMeter.Audio;

public sealed class AudioSampleConverter
{
    private float[] _conversionBuffer = Array.Empty<float>();

    public void Prepare(int sampleCapacity)
    {
        if (sampleCapacity < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(sampleCapacity));
        }

        EnsureCapacity(sampleCapacity);
    }

    public ReadOnlySpan<float> Convert(ReadOnlySpan<byte> source, AudioFormatInfo format)
    {
        if (source.Length % format.BlockAlign != 0)
        {
            throw new ArgumentException("The audio buffer does not contain complete frames.", nameof(source));
        }

        if (format.Encoding == AudioSampleEncoding.IeeeFloat)
        {
            return MemoryMarshal.Cast<byte, float>(source);
        }

        int sampleCount = source.Length / (format.BitsPerSample / 8);
        EnsureCapacity(sampleCount);
        Span<float> destination = _conversionBuffer.AsSpan(0, sampleCount);
        switch (format.BitsPerSample)
        {
            case 16:
                ConvertPcm16(source, destination);
                break;
            case 24:
                ConvertPcm24(source, destination);
                break;
            case 32:
                ConvertPcm32(source, destination);
                break;
            default:
                throw new NotSupportedException($"Unsupported PCM bit depth: {format.BitsPerSample}.");
        }

        return destination;
    }

    private static void ConvertPcm16(ReadOnlySpan<byte> source, Span<float> destination)
    {
        const float scale = 1.0f / 32768.0f;
        for (int sample = 0, offset = 0; sample < destination.Length; sample++, offset += 2)
        {
            destination[sample] = BinaryPrimitives.ReadInt16LittleEndian(source.Slice(offset, 2)) * scale;
        }
    }

    private static void ConvertPcm24(ReadOnlySpan<byte> source, Span<float> destination)
    {
        const float scale = 1.0f / 8388608.0f;
        for (int sample = 0, offset = 0; sample < destination.Length; sample++, offset += 3)
        {
            int value = source[offset] | (source[offset + 1] << 8) | (source[offset + 2] << 16);
            if ((value & 0x00800000) != 0)
            {
                value |= unchecked((int)0xFF000000);
            }

            destination[sample] = value * scale;
        }
    }

    private static void ConvertPcm32(ReadOnlySpan<byte> source, Span<float> destination)
    {
        const double scale = 1.0 / 2147483648.0;
        for (int sample = 0, offset = 0; sample < destination.Length; sample++, offset += 4)
        {
            destination[sample] = (float)(BinaryPrimitives.ReadInt32LittleEndian(source.Slice(offset, 4)) * scale);
        }
    }

    private void EnsureCapacity(int sampleCount)
    {
        if (_conversionBuffer.Length >= sampleCount)
        {
            return;
        }

        int newLength = Math.Max(sampleCount, Math.Max(4096, _conversionBuffer.Length * 2));
        _conversionBuffer = new float[newLength];
    }
}
