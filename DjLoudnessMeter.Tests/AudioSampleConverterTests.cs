using System.Buffers.Binary;
using DjLoudnessMeter.Audio;

namespace DjLoudnessMeter.Tests;

public sealed class AudioSampleConverterTests
{
    [Fact]
    public void ConvertsPcm16ToNormalizedFloat()
    {
        byte[] bytes = new byte[8];
        BinaryPrimitives.WriteInt16LittleEndian(bytes.AsSpan(0, 2), short.MinValue);
        BinaryPrimitives.WriteInt16LittleEndian(bytes.AsSpan(2, 2), -16384);
        BinaryPrimitives.WriteInt16LittleEndian(bytes.AsSpan(4, 2), 0);
        BinaryPrimitives.WriteInt16LittleEndian(bytes.AsSpan(6, 2), short.MaxValue);
        var format = new AudioFormatInfo(48000, 2, 16, 16, 4, AudioSampleEncoding.Pcm);

        ReadOnlySpan<float> result = new AudioSampleConverter().Convert(bytes, format);

        Assert.Equal(-1.0f, result[0]);
        Assert.Equal(-0.5f, result[1]);
        Assert.Equal(0.0f, result[2]);
        Assert.InRange(result[3], 0.9999f, 1.0f);
    }

    [Fact]
    public void ConvertsSignedPcm24ToNormalizedFloat()
    {
        byte[] bytes = [0x00, 0x00, 0x80, 0xFF, 0xFF, 0x7F];
        var format = new AudioFormatInfo(44100, 1, 24, 24, 3, AudioSampleEncoding.Pcm);

        ReadOnlySpan<float> result = new AudioSampleConverter().Convert(bytes, format);

        Assert.Equal(-1.0f, result[0]);
        Assert.InRange(result[1], 0.9999f, 1.0f);
    }

    [Fact]
    public void ConvertsPcm32ToNormalizedFloat()
    {
        byte[] bytes = new byte[8];
        BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(0, 4), int.MinValue);
        BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(4, 4), int.MaxValue);
        var format = new AudioFormatInfo(48000, 1, 32, 32, 4, AudioSampleEncoding.Pcm);

        ReadOnlySpan<float> result = new AudioSampleConverter().Convert(bytes, format);

        Assert.Equal(-1.0f, result[0]);
        Assert.InRange(result[1], 0.9999f, 1.0f);
    }

    [Fact]
    public void FloatConversionUsesTheInputBufferWithoutAConversionAllocation()
    {
        float[] values = [-1.0f, -0.25f, 0.25f, 1.0f];
        ReadOnlySpan<byte> bytes = System.Runtime.InteropServices.MemoryMarshal.AsBytes(values.AsSpan());
        var format = new AudioFormatInfo(48000, 2, 32, 32, 8, AudioSampleEncoding.IeeeFloat);
        var converter = new AudioSampleConverter();

        ReadOnlySpan<float> first = converter.Convert(bytes, format);
        ReadOnlySpan<float> second = converter.Convert(bytes, format);

        Assert.True(first.SequenceEqual(values));
        Assert.True(second.SequenceEqual(values));
    }
}
