[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [string]$BuildDirectory = "$PSScriptRoot/build",

    [string]$StreamlineSdkRoot,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($Clean -and (Test-Path -LiteralPath $BuildDirectory)) {
    Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
}

$configure = @(
    '-S', $PSScriptRoot,
    '-B', $BuildDirectory,
    '-DBUILD_TESTING=ON'
)
if ($StreamlineSdkRoot) {
    $resolvedStreamline = (Resolve-Path -LiteralPath $StreamlineSdkRoot).Path
    $configure += "-DXEMU_STREAMLINE_SDK_ROOT=$resolvedStreamline"
}

cmake @configure
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Reference adapter tests failed with exit code $LASTEXITCODE"
}

Write-Host "Reference adapters built in: $BuildDirectory"
