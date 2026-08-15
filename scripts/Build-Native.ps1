[CmdletBinding()]
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$installRoot = Join-Path $projectRoot 'artifacts\vcpkg_installed'
$destinationDirectory = Join-Path $projectRoot 'DjLoudnessMeter\Native\runtimes\win-x64\native'

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $vcpkgCommand = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($null -eq $vcpkgCommand) {
        throw 'vcpkg was not found. Set VCPKG_ROOT or place vcpkg on PATH.'
    }

    $vcpkgExecutable = $vcpkgCommand.Source
}
else {
    $vcpkgExecutable = Join-Path $VcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkgExecutable)) {
        throw "vcpkg.exe was not found at $vcpkgExecutable."
    }
}

& $vcpkgExecutable install --triplet x64-windows --x-manifest-root $projectRoot --x-install-root $installRoot
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg failed with exit code $LASTEXITCODE."
}

$sourceDll = Join-Path $installRoot 'x64-windows\bin\ebur128.dll'
if (-not (Test-Path -LiteralPath $sourceDll)) {
    throw "The expected native library was not produced at $sourceDll."
}

New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
Copy-Item -LiteralPath $sourceDll -Destination (Join-Path $destinationDirectory 'ebur128.dll') -Force
Write-Host "Bundled x64 libebur128: $destinationDirectory\ebur128.dll"
