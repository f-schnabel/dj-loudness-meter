[CmdletBinding()]
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $projectRoot 'publish\win-x64' }

& (Join-Path $PSScriptRoot 'Build-Native.ps1') -VcpkgRoot $VcpkgRoot -Configuration Release
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$obsoleteDll = Join-Path $OutputDirectory 'ebur128.dll'
if (Test-Path -LiteralPath $obsoleteDll) { Remove-Item -LiteralPath $obsoleteDll -Force }
$buildOutput = Join-Path $projectRoot 'build\Release'
Copy-Item -LiteralPath (Join-Path $buildOutput 'DjLoudnessMeter.exe') -Destination $OutputDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $OutputDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD-PARTY-NOTICES.md') -Destination $OutputDirectory -Force
Write-Host "Published: $OutputDirectory"
