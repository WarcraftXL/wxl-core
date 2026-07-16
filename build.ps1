#requires -Version 5.1
<#
.SYNOPSIS
    Build + deploy WarcraftXL: WarcraftXL.dll (injected), d3d9.dll (proxy) and wxl-patcher.exe (32-bit).

.PARAMETER Config
    Build configuration. Default: Release.

.PARAMETER ClientPath
    Client directory to deploy into. Optional once it has been cached by a first run.

.PARAMETER Clean
    Delete the build directory before configuring (forces a from-scratch build).

.PARAMETER AutoPatch
    After the build, run wxl-patcher on the client's Wow.exe. The patcher is idempotent
    (it skips an already-patched exe and backs the original up to Wow.exe.orig on first run).

.EXAMPLE
    .\build.ps1
.EXAMPLE
    .\build.ps1 -ClientPath "D:\Path\To\Client" -Clean
.EXAMPLE
    .\build.ps1 -AutoPatch
#>
param(
    [string]$Config = "Release",
    [string]$ClientPath,
    [switch]$Clean,
    [switch]$AutoPatch,
    [switch]$BuildHost
)

$ErrorActionPreference = "Stop"

$root         = $PSScriptRoot
$buildDir     = Join-Path $root "build\dll"
$buildHostDir = Join-Path $root "build\host"

if (-not (Test-Path (Join-Path $root "CMakeLists.txt"))) {
    throw "CMakeLists.txt is missing from '$root'. Place build.ps1 in the project root directory (next to CMakeLists.txt)."
}

function Get-CachedClientPath([string]$cacheDir) {
    $cache = Join-Path $cacheDir "CMakeCache.txt"
    if (Test-Path $cache) {
        $hit = Select-String -Path $cache -Pattern '^CLIENT_PATH:PATH=(.+)$' | Select-Object -First 1
        if ($hit) { return $hit.Matches[0].Groups[1].Value.Trim() }
    }
    return $null
}

if (-not $ClientPath) {
    $ClientPath = Get-CachedClientPath $buildDir
}
if (-not $ClientPath) {
    throw "No client path is known. Run the command once with -ClientPath '<client folder>' (it will be stored in the CMake cache)."
}
if (-not (Test-Path $ClientPath)) {
    throw "Client path not found: $ClientPath"
}

Write-Host "Project : $root"
Write-Host "Client : $ClientPath"
Write-Host "Config : $Config"
Write-Host ""

function Invoke-Native([string]$exe, [string[]]$cmdArgs) {
    Write-Host ">> $exe $($cmdArgs -join ' ')" -ForegroundColor Cyan
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { & $exe @cmdArgs } finally { $ErrorActionPreference = $prev }
    if ($LASTEXITCODE -ne 0) { throw "Failure ($LASTEXITCODE): $exe $($cmdArgs -join ' ')" }
}

function Build-Dll {
    Write-Host "=== WarcraftXL.dll (32-bit) ===" -ForegroundColor Green

    if ($Clean -and (Test-Path $buildDir)) {
        Write-Host "Clean $buildDir" -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildDir
    }

    $needConfigure = $Clean `
        -or $PSBoundParameters.ContainsKey('ClientPath') `
        -or (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt")))
    if ($needConfigure) {
        Invoke-Native "cmake" @("-S", $root, "-B", $buildDir, "-A", "Win32", "-DCLIENT_PATH=$ClientPath")
    }

    Invoke-Native "cmake" @("--build", $buildDir, "--config", $Config, "--parallel")
    Write-Host ""
}

function Build-Host {
    Write-Host "=== WarcraftXLHost.exe (64-bit) ===" -ForegroundColor Green

    if ($Clean -and (Test-Path $buildHostDir)) {
        Write-Host "Clean $buildHostDir" -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildHostDir
    }

    $needConfigure = $Clean `
        -or $PSBoundParameters.ContainsKey('ClientPath') `
        -or (-not (Test-Path (Join-Path $buildHostDir "CMakeCache.txt")))
    if ($needConfigure) {
        Invoke-Native "cmake" @("-S", $root, "-B", $buildHostDir, "-A", "x64", "-DWXL_BUILD_HOST=ON", "-DCLIENT_PATH=$ClientPath")
    }

    Invoke-Native "cmake" @("--build", $buildHostDir, "--config", $Config, "--parallel")
    Write-Host ""
}

function Invoke-AutoPatch {
    $patcher = Join-Path $buildDir "$Config\wxl-patcher.exe"
    if (-not (Test-Path $patcher)) { $patcher = Join-Path $ClientPath "wxl-patcher.exe" }
    if (-not (Test-Path $patcher)) {
        throw "wxl-patcher.exe not found. Build first (.\build.ps1)."
    }
    $wow = Join-Path $ClientPath "Wow.exe"
    if (-not (Test-Path $wow)) { throw "Wow.exe not found: $wow" }

    Write-Host "=== AutoPatch ===" -ForegroundColor Green
    Invoke-Native $patcher @($wow)
    Write-Host ""
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

Build-Dll
if ($BuildHost) { Build-Host }
if ($AutoPatch) { Invoke-AutoPatch }

$sw.Stop()
Write-Host "OK - build + deploy in $([int]$sw.Elapsed.TotalSeconds)s -> $ClientPath" -ForegroundColor Green
