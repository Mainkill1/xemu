[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$XemuPath,

    [Parameter(Mandatory)]
    [string]$AdapterPath,

    [int]$WorkWidth = 0,

    [int]$WorkHeight = 0,

    [string]$StreamlineDirectory,

    [string]$StreamlineProjectId,

    [string]$StreamlineLogDirectory,

    [string[]]$XemuArgument = @(),

    [string]$LogPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$xemu = (Resolve-Path -LiteralPath $XemuPath).Path
$adapter = (Resolve-Path -LiteralPath $AdapterPath).Path
if (-not [System.IO.Path]::IsPathFullyQualified($adapter)) {
    throw 'The adapter path must be absolute.'
}
if (($WorkWidth -eq 0) -xor ($WorkHeight -eq 0)) {
    throw 'WorkWidth and WorkHeight must both be zero or both be positive.'
}
if ($WorkWidth -lt 0 -or $WorkHeight -lt 0) {
    throw 'Work dimensions cannot be negative.'
}

$names = @(
    'XEMU_EXPERIMENTAL_NEURAL_PRESENT',
    'XEMU_NEURAL_PRESENT_PLUGIN',
    'XEMU_NEURAL_PRESENT_WORK_WIDTH',
    'XEMU_NEURAL_PRESENT_WORK_HEIGHT',
    'XEMU_STREAMLINE_DIRECTORY',
    'XEMU_STREAMLINE_PROJECT_ID',
    'XEMU_STREAMLINE_LOG_DIRECTORY'
)
$previous = @{}
foreach ($name in $names) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

$processExitCode = 0
try {
    $env:XEMU_EXPERIMENTAL_NEURAL_PRESENT = '1'
    $env:XEMU_NEURAL_PRESENT_PLUGIN = $adapter
    if ($WorkWidth -gt 0) {
        $env:XEMU_NEURAL_PRESENT_WORK_WIDTH = [string]$WorkWidth
        $env:XEMU_NEURAL_PRESENT_WORK_HEIGHT = [string]$WorkHeight
    } else {
        Remove-Item Env:XEMU_NEURAL_PRESENT_WORK_WIDTH -ErrorAction SilentlyContinue
        Remove-Item Env:XEMU_NEURAL_PRESENT_WORK_HEIGHT -ErrorAction SilentlyContinue
    }

    if ($StreamlineDirectory) {
        $env:XEMU_STREAMLINE_DIRECTORY =
            (Resolve-Path -LiteralPath $StreamlineDirectory).Path
    } else {
        Remove-Item Env:XEMU_STREAMLINE_DIRECTORY -ErrorAction SilentlyContinue
    }
    if ($StreamlineProjectId) {
        if ($StreamlineProjectId -notmatch
            '^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$') {
            throw 'StreamlineProjectId must be a GUID without braces.'
        }
        $env:XEMU_STREAMLINE_PROJECT_ID = $StreamlineProjectId
    } else {
        Remove-Item Env:XEMU_STREAMLINE_PROJECT_ID -ErrorAction SilentlyContinue
    }
    if ($StreamlineLogDirectory) {
        $streamlineLog = [System.IO.Path]::GetFullPath($StreamlineLogDirectory)
        if (-not (Test-Path -LiteralPath $streamlineLog)) {
            New-Item -ItemType Directory -Path $streamlineLog -Force | Out-Null
        }
        $env:XEMU_STREAMLINE_LOG_DIRECTORY = $streamlineLog
    } else {
        Remove-Item Env:XEMU_STREAMLINE_LOG_DIRECTORY -ErrorAction SilentlyContinue
    }

    Write-Host "Launching: $xemu"
    Write-Host "Adapter:  $adapter"
    if ($WorkWidth -gt 0) {
        Write-Host "Work size: ${WorkWidth}x${WorkHeight}"
    }

    if ($LogPath) {
        $log = [System.IO.Path]::GetFullPath($LogPath)
        $logDirectory = Split-Path -Parent $log
        if ($logDirectory -and -not (Test-Path -LiteralPath $logDirectory)) {
            New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
        }
        & $xemu @XemuArgument 2>&1 | Tee-Object -FilePath $log
    } else {
        & $xemu @XemuArgument
    }
    $processExitCode = $LASTEXITCODE
} finally {
    foreach ($name in $names) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}

exit $processExitCode
