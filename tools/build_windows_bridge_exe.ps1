[CmdletBinding()]
param(
    [string]$Python = '',
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bridgeDirectory = Join-Path $repositoryRoot 'tools\desktop_display_bridge'
$launcher = Join-Path $bridgeDirectory 'windows_bridge_launcher.py'
$versionInfo = Join-Path $bridgeDirectory 'windows_version_info.txt'
$buildRoot = Join-Path $repositoryRoot 'build\windows_bridge_exe'
$distDirectory = Join-Path $buildRoot 'dist'
$workDirectory = Join-Path $buildRoot 'work'
$specDirectory = Join-Path $buildRoot 'spec'

if ([string]::IsNullOrWhiteSpace($Python)) {
    $Python = Join-Path $repositoryRoot '.venv\Scripts\python.exe'
}
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    $pythonCommand = Get-Command $Python -ErrorAction SilentlyContinue
    if ($null -eq $pythonCommand) {
        throw "Python was not found: $Python"
    }
    $Python = $pythonCommand.Source
}
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "Windows bridge launcher was not found: $launcher"
}

$allowedBuildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build'))
$resolvedBuildRoot = [IO.Path]::GetFullPath($buildRoot)
$allowedPrefix = $allowedBuildRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $resolvedBuildRoot.StartsWith(
        $allowedPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use a build directory outside the repository build tree: $resolvedBuildRoot"
}

if ($Clean -and (Test-Path -LiteralPath $resolvedBuildRoot)) {
    Remove-Item -LiteralPath $resolvedBuildRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $distDirectory, $workDirectory, $specDirectory |
    Out-Null

& $Python -c 'import PyInstaller' 2>$null
if ($LASTEXITCODE -ne 0) {
    throw @"
PyInstaller is not installed. Run:
  $Python -m pip install -r tools\desktop_display_bridge\requirements-build.txt
"@
}

$arguments = @(
    '-m', 'PyInstaller',
    '--noconfirm',
    '--onefile',
    '--windowed',
    '--name', 'SmallDesktopDisplayBridge',
    '--distpath', $distDirectory,
    '--workpath', $workDirectory,
    '--specpath', $specDirectory,
    '--paths', $bridgeDirectory,
    '--paths', (Join-Path $repositoryRoot 'tools\codex_usage_bridge'),
    '--hidden-import', 'codex_usage_bridge',
    '--version-file', $versionInfo,
    $launcher
)

& $Python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed with exit code $LASTEXITCODE"
}

$executable = Join-Path $distDirectory 'SmallDesktopDisplayBridge.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Expected executable was not produced: $executable"
}

$item = Get-Item -LiteralPath $executable
$hash = Get-FileHash -LiteralPath $executable -Algorithm SHA256
Write-Host "Windows bridge executable: $($item.FullName)"
Write-Host "Size: $($item.Length) bytes"
Write-Host "SHA256: $($hash.Hash)"
