[CmdletBinding()]
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot 'publish\win-x64'
}

& (Join-Path $PSScriptRoot 'Build-Native.ps1') -VcpkgRoot $VcpkgRoot
if ($LASTEXITCODE -ne 0) {
    throw 'Native dependency build failed.'
}

dotnet test (Join-Path $projectRoot 'DjLoudnessMeter.sln') -c Release -p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw 'Tests failed.'
}

dotnet publish (Join-Path $projectRoot 'DjLoudnessMeter\DjLoudnessMeter.csproj') `
    -c Release `
    -r win-x64 `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -o $OutputDirectory
if ($LASTEXITCODE -ne 0) {
    throw 'Publish failed.'
}

Write-Host "Published DJ Loudness Meter to $OutputDirectory"
