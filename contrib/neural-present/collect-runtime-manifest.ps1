[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$XemuPath,

    [Parameter(Mandatory)]
    [string]$AdapterPath,

    [string]$StreamlineDirectory,

    [string]$OutputPath = (Join-Path $PWD 'xemu-neural-runtime-manifest.json')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-NormalizedFileRecord {
    param([Parameter(Mandatory)][string]$Path)

    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($item.PSIsContainer) {
        throw "Expected a file but received a directory: $Path"
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $item.FullName
    $version = $item.VersionInfo
    [ordered]@{
        path = $item.FullName
        length = $item.Length
        lastWriteTimeUtc = $item.LastWriteTimeUtc.ToString('o')
        sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        fileVersion = $version.FileVersion
        productVersion = $version.ProductVersion
        signatureStatus = [string]$signature.Status
        signerSubject = if ($signature.SignerCertificate) {
            $signature.SignerCertificate.Subject
        } else {
            $null
        }
        signerThumbprint = if ($signature.SignerCertificate) {
            $signature.SignerCertificate.Thumbprint
        } else {
            $null
        }
    }
}

$xemu = (Resolve-Path -LiteralPath $XemuPath).Path
$adapter = (Resolve-Path -LiteralPath $AdapterPath).Path
$files = [System.Collections.Generic.List[object]]::new()
$files.Add((Get-NormalizedFileRecord -Path $xemu))
$files.Add((Get-NormalizedFileRecord -Path $adapter))

if ($StreamlineDirectory) {
    $slDirectory = (Resolve-Path -LiteralPath $StreamlineDirectory).Path
    Get-ChildItem -LiteralPath $slDirectory -File -Recurse |
        Where-Object {
            $_.Name -like 'sl.*.dll' -or
            $_.Name -like 'nvngx*.dll' -or
            $_.Name -eq 'sl.interposer.dll'
        } |
        Sort-Object FullName -Unique |
        ForEach-Object {
            $files.Add((Get-NormalizedFileRecord -Path $_.FullName))
        }
} else {
    $slDirectory = $null
}

$nvidiaSmi = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
$gpuRecords = @()
if ($nvidiaSmi) {
    $query = & $nvidiaSmi.Source '--query-gpu=name,pci.bus_id,driver_version,uuid' '--format=csv,noheader,nounits' 2>$null
    if ($LASTEXITCODE -eq 0) {
        $gpuRecords = @($query | ForEach-Object {
            $parts = $_ -split ',\s*', 4
            [ordered]@{
                name = $parts[0]
                pciBusId = if ($parts.Count -gt 1) { $parts[1] } else { $null }
                driverVersion = if ($parts.Count -gt 2) { $parts[2] } else { $null }
                uuid = if ($parts.Count -gt 3) { $parts[3] } else { $null }
            }
        })
    }
}

$os = Get-CimInstance -ClassName Win32_OperatingSystem
$manifest = [ordered]@{
    schema = 'xemu-neural-runtime-manifest-v1'
    collectedAtUtc = [DateTime]::UtcNow.ToString('o')
    machine = [ordered]@{
        computerName = $env:COMPUTERNAME
        osCaption = $os.Caption
        osVersion = $os.Version
        osBuildNumber = $os.BuildNumber
        architecture = $env:PROCESSOR_ARCHITECTURE
    }
    gpu = $gpuRecords
    configuration = [ordered]@{
        streamlineDirectory = $slDirectory
        workWidth = $env:XEMU_NEURAL_PRESENT_WORK_WIDTH
        workHeight = $env:XEMU_NEURAL_PRESENT_WORK_HEIGHT
        streamlineProjectId = $env:XEMU_STREAMLINE_PROJECT_ID
        streamlineLogDirectory = $env:XEMU_STREAMLINE_LOG_DIRECTORY
    }
    files = $files
}

$output = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $output
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $output -Encoding utf8
Write-Host "Runtime manifest written to: $output"
