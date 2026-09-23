[CmdletBinding()]
param(
    [string]$Python = '',
    [string]$Iscc = '',
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Python) { $Python = Join-Path $repositoryRoot '.venv\Scripts\python.exe' }
if (-not $SkipBuild) { & "$PSScriptRoot\build_windows_bridge_exe.ps1" -Python $Python }
if (-not $Iscc) {
    $candidates = @(
        (Join-Path $repositoryRoot 'build\toolchain\inno\ISCC.exe'),
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    $Iscc = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
$staging = Join-Path $repositoryRoot 'build\windows_bridge_installer'
$cache = Join-Path $repositoryRoot 'build\toolchain'
$sensors = Join-Path $repositoryRoot 'build\windows_sensors\sensors'
$licenses = Join-Path $staging 'licenses'
New-Item -ItemType Directory -Force -Path $staging, $cache, $sensors, $licenses | Out-Null

# GitHub-hosted Windows runners do not always include Inno Setup. Pin the
# official compiler installer so a clean runner can reproduce the package.
if (-not $Iscc) {
    $innoSetup = Join-Path $cache 'innosetup-6.7.3.exe'
    if (-not (Test-Path -LiteralPath $innoSetup)) {
        Invoke-WebRequest 'https://files.jrsoftware.org/is/6/innosetup-6.7.3.exe' -OutFile $innoSetup
    }
    if ((Get-FileHash -LiteralPath $innoSetup -Algorithm SHA256).Hash -ne '9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732') {
        throw 'Inno Setup compiler SHA-256 mismatch.'
    }
    $innoDirectory = Join-Path $cache 'inno'
    $installer = Start-Process -FilePath $innoSetup -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOICONS',
        ('/DIR="{0}"' -f $innoDirectory)
    ) -WindowStyle Hidden -Wait -PassThru
    if ($installer.ExitCode -ne 0) { throw "Inno Setup installation failed: $($installer.ExitCode)" }
    $Iscc = Join-Path $innoDirectory 'ISCC.exe'
}

$archive = Join-Path $cache 'LibreHardwareMonitor-0.9.6.zip'
$expectedHash = '086d9f1b5a99e643edc2cfaaac16051685b551e4c5ac0b32a57c58c0e529c001'
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest 'https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v0.9.6/LibreHardwareMonitor.zip' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'LibreHardwareMonitor archive SHA-256 mismatch. Remove the cached archive and retry.'
}
Expand-Archive -LiteralPath $archive -DestinationPath $sensors -Force
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:winexe /optimize+ "/out:$sensors\MiniDisplayTemperature.exe" "/reference:$sensors\LibreHardwareMonitorLib.dll" /reference:System.Web.Extensions.dll "/win32manifest:$PSScriptRoot\desktop_display_bridge\TemperatureCollector.manifest" "$PSScriptRoot\desktop_display_bridge\TemperatureCollector.cs"
if ($LASTEXITCODE -ne 0) { throw 'Temperature collector compilation failed' }
Copy-Item -LiteralPath "$sensors\LibreHardwareMonitor.exe.config" -Destination "$sensors\MiniDisplayTemperature.exe.config" -Force
foreach ($name in @('LICENSE', 'THIRD-PARTY-NOTICES.txt')) {
    $destination = Join-Path $licenses "LibreHardwareMonitor-$name.txt"
    if (-not (Test-Path -LiteralPath $destination)) {
        Invoke-WebRequest "https://raw.githubusercontent.com/LibreHardwareMonitor/LibreHardwareMonitor/v0.9.6/$name" -OutFile $destination
    }
}
$serialLicense = Join-Path $licenses 'pyserial-LICENSE.txt'
if (-not (Test-Path -LiteralPath $serialLicense)) {
    Invoke-WebRequest 'https://raw.githubusercontent.com/pyserial/pyserial/v3.5/LICENSE.txt' -OutFile $serialLicense
}
& $Python (Join-Path $PSScriptRoot 'desktop_display_bridge\prepare_windows_package.py') $repositoryRoot $staging
if ($LASTEXITCODE -ne 0) { throw 'Package asset preparation failed' }
& $Iscc "/DRepositoryRoot=$repositoryRoot" (Join-Path $PSScriptRoot 'desktop_display_bridge\windows_installer.iss')
if ($LASTEXITCODE -ne 0) { throw "Installer compiler failed: $LASTEXITCODE" }
$version = (Get-Item "$repositoryRoot\build\windows_bridge_exe\dist\SmallDesktopDisplayBridge.exe").VersionInfo.FileVersion
$setup = Join-Path $repositoryRoot "build\packages\MiniDisplayBridge-$version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $setup)) { throw "Installer missing: $setup" }
$checksum = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
"$checksum  $([IO.Path]::GetFileName($setup))" | Set-Content -LiteralPath "$setup.sha256" -Encoding ascii
Write-Host "Installer: $setup"
Write-Host "SHA256: $checksum"
