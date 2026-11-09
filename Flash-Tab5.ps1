<#
.SYNOPSIS
Build and flash the standalone Tab5 Nightscout project.
.EXAMPLE
.\Flash-Tab5.ps1 -Port COM5
.EXAMPLE
.\Flash-Tab5.ps1 -UsePortal -Port COM5
.EXAMPLE
.\Flash-Tab5.ps1 -BuildOnly -PythonPath C:\Python313\python.exe
#>
[CmdletBinding()]
param(
    [string]$Port,
    [string]$PythonPath,
    [switch]$BuildOnly,
    [switch]$ListPorts,
    [switch]$Monitor,
    [switch]$InitConfig,
    [switch]$UsePortal,
    [switch]$Revision3,
    [switch]$DryRun
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Espressif upload progress uses Unicode; avoid Windows code-page failures.
$previousPythonUtf8 = $env:PYTHONUTF8
$previousPythonIoEncoding = $env:PYTHONIOENCODING
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$projectRoot = $PSScriptRoot
$environmentName = 'tab5'
if ($Revision3) { $environmentName = 'tab5-rev3' }

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "The command failed (exit $LASTEXITCODE). Stopped before any further steps."
    }
}

function Find-CompatiblePython {
    $candidates = @()
    if ($PythonPath) {
        $candidates += ,@($PythonPath)
    } else {
        $launcher = Get-Command py -ErrorAction SilentlyContinue
        if ($launcher) {
            foreach ($version in @('3.13', '3.12', '3.11', '3.10')) {
                $candidates += ,@($launcher.Source, "-$version")
            }
        }
        foreach ($name in @('python', 'python3')) {
            $command = Get-Command $name -ErrorAction SilentlyContinue
            if ($command) { $candidates += ,@($command.Source) }
        }
        $pioPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
        if (Test-Path -LiteralPath $pioPython -PathType Leaf) { $candidates += ,@($pioPython) }
    }
    foreach ($candidate in $candidates) {
        $program = $candidate[0]
        $prefix = @($candidate | Select-Object -Skip 1)
        try {
            $probe = @(& $program @prefix -c 'import sys; print(sys.executable if sys.version_info.major == 3 and 10 <= sys.version_info.minor <= 13 else ''UNSUPPORTED'')' 2>$null)
            if ($LASTEXITCODE -eq 0 -and $probe.Count -gt 0) {
                $resolved = [string]$probe[-1]
                if ($resolved -ne 'UNSUPPORTED' -and (Test-Path -LiteralPath $resolved -PathType Leaf)) { return $resolved }
            }
        } catch { }
    }
    throw 'Python 3.10-3.13 was not found. Install Python 3.13 with pip/venv from python.org, then rerun; or pass -PythonPath C:\path\to\python.exe. This pinned Tab5 platform does not support Python 3.14.'
}

try {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'platformio.ini') -PathType Leaf)) {
        throw 'Keep Flash-Tab5.ps1 in the extracted project folder beside platformio.ini.'
    }
    $actionCount = 0
    foreach ($flag in @($BuildOnly, $ListPorts, $Monitor, $InitConfig)) { if ($flag) { $actionCount++ } }
    if ($actionCount -gt 1) { throw 'Choose only one of -BuildOnly, -ListPorts, -Monitor or -InitConfig.' }
    $pioArguments = @('-m', 'platformio')
    if ($ListPorts) {
        $pioArguments += @('device', 'list')
    } elseif ($Monitor) {
        $pioArguments += @('device', 'monitor', '--baud', '115200')
        if ($Port) { $pioArguments += @('--port', $Port) }
    } else {
        $pioArguments += @('run', '--project-dir', $projectRoot, '--environment', $environmentName)
        if (-not $BuildOnly) {
            $pioArguments += @('--target', 'upload')
            if ($Port) { $pioArguments += @('--upload-port', $Port) }
        }
    }
    if ($DryRun) {
        Write-Host "Project: $projectRoot"
        Write-Host 'Dry run: no files changed, downloads started or device contacted.'
        if ($InitConfig) { Write-Host 'Would create include\config.h only if missing.' }
        else { Write-Host ('Would run: <local Python 3.10-3.13> ' + (($pioArguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')) }
        exit 0
    }
    if ($InitConfig -or (-not $ListPorts -and -not $Monitor)) {
        $configPath = Join-Path $projectRoot 'include\config.h'
        if (-not (Test-Path -LiteralPath $configPath)) {
            # Copy without overwrite, even if another process creates it concurrently.
            [System.IO.File]::Copy((Join-Path $projectRoot 'include\config.example.h'), $configPath, $false)
            Write-Host "Created $configPath"
            if (-not $UsePortal -and -not $InitConfig) {
                Write-Host 'Edit Wi-Fi, Nightscout URL and read-only token there, then run this script again.'
                Write-Host 'Or rerun with -UsePortal to configure on the device. Existing config.h is never overwritten.'
                exit 0
            }
        } else { Write-Host 'Existing include\config.h kept.' }
        if ($InitConfig) { exit 0 }
    }
    $python = Find-CompatiblePython
    $toolsRoot = Join-Path $projectRoot '.build-tools'
    $toolsPython = Join-Path $toolsRoot 'Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $toolsPython -PathType Leaf)) {
        Write-Host 'Preparing local build tools (first run only)...'
        Invoke-Checked -Program $python -Arguments @('-m', 'venv', $toolsRoot)
    }
    # Check the existing environment too: it may have been created with another Python.
    Invoke-Checked -Program $toolsPython -Arguments @('-c', 'import sys; sys.exit(0 if sys.version_info.major == 3 and 10 <= sys.version_info.minor <= 13 else 1)')
    $installed = @(& $toolsPython -c 'import importlib.util as u, importlib.metadata as m; print(m.version(''platformio'') if u.find_spec(''platformio'') else '''')' 2>$null)
    if ($LASTEXITCODE -ne 0 -or $installed.Count -eq 0 -or $installed[-1] -ne '6.2.0') {
        Write-Host 'Installing PlatformIO 6.2.0 locally; internet access is required.'
        Invoke-Checked -Program $toolsPython -Arguments @('-m', 'pip', 'install', 'platformio==6.2.0')
    }
    Push-Location -LiteralPath $projectRoot
    try {
        if (-not $ListPorts -and -not $Monitor) {
            Write-Host "Building $environmentName. The first build downloads the pinned hardware tools and libraries."
            if (-not $BuildOnly) { Write-Host 'Uploading after a successful build. Connect the Tab5 with a USB-C data cable.' }
        }
        Invoke-Checked -Program $toolsPython -Arguments $pioArguments
    } finally { Pop-Location }
    Write-Host 'Finished successfully.'
    exit 0
} catch {
    Write-Host ("Stopped: " + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'For upload problems, try -ListPorts, pass -Port COM5, or enter download mode by holding power until the green LED blinks.'
    exit 1
}

finally {
    if ($null -eq $previousPythonUtf8) { Remove-Item Env:PYTHONUTF8 -ErrorAction SilentlyContinue }
    else { $env:PYTHONUTF8 = $previousPythonUtf8 }
    if ($null -eq $previousPythonIoEncoding) { Remove-Item Env:PYTHONIOENCODING -ErrorAction SilentlyContinue }
    else { $env:PYTHONIOENCODING = $previousPythonIoEncoding }
}
