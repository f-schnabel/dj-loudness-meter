[CmdletBinding()]
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$installRoot = Join-Path $projectRoot 'artifacts\vcpkg_installed'
$buildDirectory = Join-Path $projectRoot 'build'
$library = Join-Path $installRoot 'x64-windows-static\lib\ebur128.lib'
$wilHeader = Join-Path $installRoot 'x64-windows-static\include\wil\resource.h'

if (-not (Test-Path -LiteralPath $library) -or -not (Test-Path -LiteralPath $wilHeader)) {
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -and -not [string]::IsNullOrWhiteSpace($env:VCPKG_INSTALLATION_ROOT)) {
        $VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
    }
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        $vcpkgCommand = Get-Command vcpkg -ErrorAction SilentlyContinue
        if ($null -eq $vcpkgCommand) { throw 'vcpkg not found. Set VCPKG_ROOT or place vcpkg on PATH.' }
        $vcpkgExecutable = $vcpkgCommand.Source
    }
    else {
        $vcpkgExecutable = Join-Path $VcpkgRoot 'vcpkg.exe'
        if (-not (Test-Path -LiteralPath $vcpkgExecutable)) { throw "vcpkg.exe not found: $vcpkgExecutable" }
    }
    & $vcpkgExecutable install --triplet x64-windows-static --x-manifest-root $projectRoot --x-install-root $installRoot
    if ($LASTEXITCODE -ne 0) { throw "vcpkg failed: $LASTEXITCODE" }
}
cmake -S $projectRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
$targetExecutable = Join-Path $buildDirectory "$Configuration\DjLoudnessMeter.exe"
$runningTarget = Get-Process -Name DjLoudnessMeter -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $targetExecutable }
if ($null -ne $runningTarget) { throw "Close the running app before building: $targetExecutable (PID $($runningTarget.Id))" }
cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
ctest --test-dir $buildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed: $LASTEXITCODE" }

$output = Join-Path $buildDirectory $Configuration
if (-not (Test-Path -LiteralPath $library)) { throw "Static libebur128 not found: $library" }
$obsoleteDll = Join-Path $output 'ebur128.dll'
if (Test-Path -LiteralPath $obsoleteDll) { Remove-Item -LiteralPath $obsoleteDll -Force }
Write-Host "Built native application: $output"
