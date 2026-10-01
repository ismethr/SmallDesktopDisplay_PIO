[CmdletBinding()]
param(
    [string]$OutputDirectory = '',
    [switch]$SkipPng
)

# Windows counterpart of tools/preview_status_screen.sh: builds the USB status
# screen protocol tests and the real-code layout preview with MSVC, runs both
# and converts the 240 x 240 previews to PNG files and a contact sheet.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$firmware = Join-Path $repositoryRoot 'mac_status_display'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot 'build\status_preview'
}
$objectDirectory = Join-Path $repositoryRoot 'build\status_display_checks'
$python = Join-Path $repositoryRoot '.venv\Scripts\python.exe'
$pio = Join-Path $repositoryRoot '.venv\Scripts\pio.exe'
$fonts = Join-Path $firmware '.pio\libdeps\esp12e\TFT_eSPI\Fonts'
$unity = Join-Path $firmware '.pio\libdeps\native_test\Unity\src'

function Invoke-Checked {
    param([Parameter(Mandatory)] [string]$Executable, [string[]]$Arguments = @())
    & $Executable @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$([IO.Path]::GetFileName($Executable)) failed with exit code $LASTEXITCODE"
    }
}

function Import-VisualStudioEnvironment {
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe was not found; install Visual Studio Build Tools with the C++ workload.'
    }
    $installation = & $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($installation)) {
        throw 'The MSVC x64 toolchain was not found.'
    }
    $developerCommand = Join-Path $installation.Trim() 'Common7\Tools\VsDevCmd.bat'
    # VsDevCmd reports optional-component probes on stderr; only its exit code matters.
    $lines = & $env:ComSpec /d /s /c "`"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 >nul 2>nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to initialize the Visual Studio x64 build environment.'
    }
    foreach ($line in $lines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
        }
    }
}

# The pinned TFT_eSPI fonts and Unity come from the PlatformIO environments.
if (-not (Test-Path -LiteralPath (Join-Path $fonts 'Font32rle.c') -PathType Leaf)) {
    Invoke-Checked $pio @('pkg', 'install', '-d', $firmware, '-e', 'esp12e')
}
if (-not (Test-Path -LiteralPath (Join-Path $unity 'unity.c') -PathType Leaf)) {
    Invoke-Checked $pio @('pkg', 'install', '-d', $firmware, '-e', 'native_test')
}

Import-VisualStudioEnvironment
New-Item -ItemType Directory -Force -Path $objectDirectory, $OutputDirectory | Out-Null
$common = @('/nologo', '/EHsc', '/std:c++14', '/W4', '/WX', '/D_CRT_SECURE_NO_WARNINGS',
    '/Dstrtok_r=strtok_s', "/Fo$objectDirectory\")

$protocolTest = Join-Path $objectDirectory 'status_protocol_tests.exe'
Invoke-Checked cl.exe ($common + @(
    '/I', (Join-Path $firmware 'include'), '/I', $unity,
    (Join-Path $firmware 'test\test_protocol\test_main.cpp'), (Join-Path $unity 'unity.c'),
    "/Fe$protocolTest"))
Invoke-Checked $protocolTest

# Bundled TFT_eSPI font sources are third-party: compile the preview at /W3.
$preview = Join-Path $objectDirectory 'status_preview.exe'
$previewFlags = $common | Where-Object { $_ -notin @('/W4', '/WX') }
Invoke-Checked cl.exe ($previewFlags + @('/W3',
    '/I', (Join-Path $repositoryRoot 'tools\status_preview'), '/I', $fonts,
    '/I', (Join-Path $firmware 'include'),
    (Join-Path $repositoryRoot 'tools\status_preview\preview.cpp'),
    (Join-Path $firmware 'src\minidisplay_app.cpp'),
    (Join-Path $firmware 'src\status_screen.cpp'),
    (Join-Path $firmware 'src\country_flags.cpp'),
    (Join-Path $firmware 'src\offline_screen.cpp'),
    "/Fe$preview"))
Invoke-Checked $preview @($OutputDirectory)

if (-not $SkipPng) {
    Invoke-Checked $python @((Join-Path $repositoryRoot 'tools\status_preview\render_png.py'), $OutputDirectory)
}
