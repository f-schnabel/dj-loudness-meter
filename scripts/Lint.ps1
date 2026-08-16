[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio locator not found.' }

$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($visualStudio)) { throw 'Visual Studio C++ tools not found.' }

$tidy = Join-Path $visualStudio 'VC\Tools\Llvm\x64\bin\clang-tidy.exe'
if (-not (Test-Path -LiteralPath $tidy)) { throw 'Install the Visual Studio C++ Clang tools.' }

$devShell = Join-Path $visualStudio 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShell
Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64' | Out-Null

$include = Join-Path $projectRoot 'artifacts\vcpkg_installed\x64-windows-static\include'
$sources = Get-ChildItem (Join-Path $projectRoot 'src'), (Join-Path $projectRoot 'tests') -Recurse -File |
    Where-Object Extension -In '.c', '.cpp' |
    Sort-Object FullName

Push-Location $projectRoot
try {
    foreach ($source in $sources) {
        if ($source.Extension -eq '.cpp') { $standard = '/std:c++latest' } else { $standard = '/std:c17' }
        $arguments = @(
            $source.FullName, '--quiet', '--warnings-as-errors=*', '--', '--driver-mode=cl', $standard,
            '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/D_CRT_SECURE_NO_WARNINGS',
            '/Isrc', "/I$include"
        )
        $diagnostics = @(& $tidy @arguments 2>&1)
        $exitCode = $LASTEXITCODE
        $diagnostics |
            Where-Object { $_.ToString() -notmatch '^\d+ warnings generated\.$' } |
            ForEach-Object { Write-Host $_ }
        if ($exitCode -ne 0) { throw "clang-tidy failed: $($source.FullName)" }
    }
} finally {
    Pop-Location
}

Write-Host 'Lint passed.'
