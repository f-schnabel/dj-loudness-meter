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

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $vcpkgCommand = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($null -eq $vcpkgCommand) { throw 'vcpkg not found. Set VCPKG_ROOT or place vcpkg on PATH.' }
    $vcpkgExecutable = $vcpkgCommand.Source
}
else {
    $vcpkgExecutable = Join-Path $VcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkgExecutable)) { throw "vcpkg.exe not found: $vcpkgExecutable" }
}

& $vcpkgExecutable install --triplet x64-windows --x-manifest-root $projectRoot --x-install-root $installRoot
if ($LASTEXITCODE -ne 0) { throw "vcpkg failed: $LASTEXITCODE" }
cmake -S $projectRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
ctest --test-dir $buildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed: $LASTEXITCODE" }

$dll = Join-Path $installRoot 'x64-windows\bin\ebur128.dll'
$output = Join-Path $buildDirectory $Configuration
if (-not (Test-Path -LiteralPath $dll)) { throw "libebur128 not found: $dll" }
Copy-Item -LiteralPath $dll -Destination (Join-Path $output 'ebur128.dll') -Force
Write-Host "Built native application: $output"
