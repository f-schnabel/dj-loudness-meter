[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path (Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) 'DjLoudnessMeter') 'Assets') 'DjLoudnessMeter.ico'),
    [string]$PreviewPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

function New-Brush([string]$color)
{
    return [Windows.Media.SolidColorBrush]::new(
        [Windows.Media.ColorConverter]::ConvertFromString($color))
}

function New-IconPng([int]$size)
{
    $visual = [Windows.Media.DrawingVisual]::new()
    $drawing = $visual.RenderOpen()
    try
    {
        $scale = $size / 128.0
        $drawing.PushTransform([Windows.Media.ScaleTransform]::new($scale, $scale))

        $background = New-Brush '#111417'
        $border = [Windows.Media.Pen]::new((New-Brush '#3C464E'), 4)
        $drawing.DrawRoundedRectangle(
            $background,
            $border,
            [Windows.Rect]::new(4, 4, 120, 120),
            22,
            22)

        $level = New-Brush '#58D68D'
        $bars = @(
            [Windows.Rect]::new(22, 60, 14, 42),
            [Windows.Rect]::new(44, 42, 14, 60),
            [Windows.Rect]::new(66, 24, 14, 78),
            [Windows.Rect]::new(88, 50, 14, 52)
        )
        foreach ($bar in $bars)
        {
            $drawing.DrawRoundedRectangle($level, $null, $bar, 7, 7)
        }

        $drawing.DrawEllipse(
            (New-Brush '#F6C344'),
            $null,
            [Windows.Point]::new(96, 27),
            8,
            8)
        $drawing.Pop()
    }
    finally
    {
        $drawing.Close()
    }

    $bitmap = [Windows.Media.Imaging.RenderTargetBitmap]::new(
        $size,
        $size,
        96,
        96,
        [Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $encoder = [Windows.Media.Imaging.PngBitmapEncoder]::new()
    $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = [IO.MemoryStream]::new()
    try
    {
        $encoder.Save($stream)
        return $stream.ToArray()
    }
    finally
    {
        $stream.Dispose()
    }
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = foreach ($size in $sizes)
{
    [pscustomobject]@{
        Size = $size
        Bytes = [byte[]](New-IconPng $size)
    }
}

$directory = Split-Path -Parent $OutputPath
[IO.Directory]::CreateDirectory($directory) | Out-Null
$temporaryPath = $OutputPath + '.tmp'
$stream = [IO.File]::Create($temporaryPath)
$writer = [IO.BinaryWriter]::new($stream)
try
{
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$images.Count)

    [uint32]$offset = 6 + (16 * $images.Count)
    foreach ($image in $images)
    {
        $dimension = if ($image.Size -ge 256) { 0 } else { $image.Size }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$image.Bytes.Length)
        $writer.Write($offset)
        $offset += [uint32]$image.Bytes.Length
    }

    foreach ($image in $images)
    {
        $writer.Write([byte[]]$image.Bytes)
    }
}
finally
{
    $writer.Dispose()
    $stream.Dispose()
}

[IO.File]::Move($temporaryPath, $OutputPath, $true)
if (-not [string]::IsNullOrWhiteSpace($PreviewPath))
{
    [IO.File]::WriteAllBytes($PreviewPath, [byte[]]$images[-1].Bytes)
}

Write-Host "Generated application icon: $OutputPath"
