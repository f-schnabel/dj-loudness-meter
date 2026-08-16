[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$formatter = Get-Command clang-format -ErrorAction SilentlyContinue

if ($null -eq $formatter) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'clang-format not found.' }
    $visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $formatterPath = Join-Path $visualStudio 'VC\Tools\Llvm\x64\bin\clang-format.exe'
    if (-not (Test-Path -LiteralPath $formatterPath)) { throw 'Install the Visual Studio C++ Clang tools.' }
} else {
    $formatterPath = $formatter.Source
}

$files = Get-ChildItem (Join-Path $projectRoot 'src'), (Join-Path $projectRoot 'tests') -Recurse -File |
    Where-Object Extension -In '.c', '.cpp', '.h', '.hpp' |
    Sort-Object FullName

foreach ($file in $files) {
    $arguments = @('--style=file')
    if ($Check) { $arguments += '--dry-run', '--Werror' } else { $arguments += '-i' }
    & $formatterPath @arguments $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "clang-format failed: $($file.FullName)" }
}

if ($Check) { Write-Host 'Formatting valid.' } else { Write-Host 'Sources formatted.' }
