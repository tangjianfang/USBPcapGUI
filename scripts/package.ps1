<#
.SYNOPSIS
    Package USBPcapGUI into a self-contained distributable folder.

.DESCRIPTION
    1. Locates Visual Studio via vswhere
    2. Re-runs CMake and builds all C++ projects (Release x64)
    3. Installs Node.js dependencies and bundles gui-server with @yao-pkg/pkg
    4. Copies everything into dist\USBPcapGUI\
    5. Creates dist\USBPcapGUI-<version>.zip

.PARAMETER BuildDir
    CMake build directory (default: build_fresh relative to repo root)

.PARAMETER SkipCppBuild
    Skip C++ rebuild (use existing binaries in BuildDir)

.PARAMETER SkipNodeBuild
    Skip Node.js bundle step

.EXAMPLE
    .\scripts\package.ps1
    .\scripts\package.ps1 -SkipCppBuild
#>
param(
    [string]$BuildDir    = "",
    [switch]$SkipCppBuild,
    [switch]$SkipNodeBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root    = Split-Path -Parent $PSScriptRoot
$version = "0.1.0"

if (-not $BuildDir) {
    $BuildDir = Join-Path $root "build_fresh"
}

$dist       = Join-Path $root "dist"
$distApp    = Join-Path $dist "USBPcapGUI"
$binSrc     = Join-Path $BuildDir "bin\Release"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  USBPcapGUI Packager  v$version"        -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Root    : $root"
Write-Host "  BuildDir: $BuildDir"
Write-Host "  Output  : $distApp"
Write-Host ""

# ---------------------------------------------------------------------------
# Helper: find a tool, throw if missing
# ---------------------------------------------------------------------------
function Require-Tool([string]$Name, [string]$Path) {
    if (-not (Test-Path $Path)) {
        throw "Required tool not found: $Name`n  Expected: $Path"
    }
    return $Path
}

# ---------------------------------------------------------------------------
# Step 1 – Locate Visual Studio via vswhere
# ---------------------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    # Try Program Files (arm/other layouts)
    $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
}

if (-not (Test-Path $vswhere)) {
    # Last-ditch: search D:\
    $found = Get-ChildItem "D:\Program Files\Microsoft Visual Studio\Installer" -Filter "vswhere.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $vswhere = $found.FullName }
}

if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Please install Visual Studio 2022."
}

$vsRoot  = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsRoot) { throw "Visual Studio installation not found." }

$msbuild = Join-Path $vsRoot "MSBuild\Current\Bin\amd64\MSBuild.exe"
$cmake   = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

Require-Tool "MSBuild" $msbuild | Out-Null
Require-Tool "cmake"   $cmake   | Out-Null

Write-Host "[1/4] Found VS at: $vsRoot" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Step 2 – Build C++ (cmake + MSBuild)
# ---------------------------------------------------------------------------
if (-not $SkipCppBuild) {
    Write-Host ""
    Write-Host "[2/4] Building C++ (Release x64)..." -ForegroundColor Cyan

    # Locate vcpkg toolchain
    $vcpkgChain = ""
    $vcpkgRoots = @(
        "D:\vcpkg\scripts\buildsystems\vcpkg.cmake",
        "C:\vcpkg\scripts\buildsystems\vcpkg.cmake",
        "${env:VCPKG_ROOT}\scripts\buildsystems\vcpkg.cmake"
    )
    foreach ($p in $vcpkgRoots) {
        if (Test-Path $p) { $vcpkgChain = $p; break }
    }
    if (-not $vcpkgChain) {
        Write-Warning "vcpkg toolchain not found – CMake will use cached settings."
    }

    # Create/refresh build dir
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }

    Push-Location $BuildDir
    try {
        $cmakeArgs = @(
            "-S", $root,
            "-B", $BuildDir,
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-DBHPLUS_BUILD_LAUNCHER=ON",
            "-DBHPLUS_BUILD_CLI=ON",
            "-DBHPLUS_BUILD_SDK=ON",
            "-DBHPLUS_BUILD_TESTS=OFF"   # skip test binary in release package
        )
        if ($vcpkgChain) {
            $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgChain"
        }

        Write-Host "  cmake configure..." -ForegroundColor Gray
        & $cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

        Write-Host "  cmake build..." -ForegroundColor Gray
        & $cmake --build $BuildDir --config Release --parallel
        if ($LASTEXITCODE -ne 0) { throw "CMake build failed (exit $LASTEXITCODE)" }
    }
    finally {
        Pop-Location
    }

    Write-Host "  C++ build complete." -ForegroundColor Green
} else {
    Write-Host "[2/4] Skipped C++ build (--SkipCppBuild)." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Step 3 – Bundle Node.js server with pkg
# ---------------------------------------------------------------------------
if (-not $SkipNodeBuild) {
    Write-Host ""
    Write-Host "[3/4] Bundling Node.js GUI server..." -ForegroundColor Cyan

    $guiDir = Join-Path $root "gui"

    # Need node in PATH
    $nodePath = Get-Command node -ErrorAction SilentlyContinue
    if (-not $nodePath) {
        throw "node.exe not found in PATH. Install Node.js 20+ LTS."
    }
    Write-Host "  node: $($nodePath.Source)"

    Push-Location $guiDir
    try {
        Write-Host "  npm install..." -ForegroundColor Gray
        npm install --prefer-offline 2>&1
        if ($LASTEXITCODE -ne 0) { throw "npm install failed." }

        # Ensure dist folder exists for pkg output
        if (-not (Test-Path $dist)) { New-Item -ItemType Directory -Path $dist | Out-Null }

        Write-Host "  pkg bundle -> dist\gui-server.exe ..." -ForegroundColor Gray
        npx @yao-pkg/pkg . --targets node20-win-x64 --output (Join-Path $dist "gui-server.exe") 2>&1
        if ($LASTEXITCODE -ne 0) { throw "pkg bundle failed." }
    }
    finally {
        Pop-Location
    }

    Write-Host "  Node.js bundle complete." -ForegroundColor Green
} else {
    Write-Host "[3/4] Skipped Node bundle (--SkipNodeBuild)." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Step 4 – Assemble dist folder
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "[4/4] Assembling $distApp ..." -ForegroundColor Cyan

if (Test-Path $distApp) {
    Remove-Item -Recurse -Force $distApp
}
New-Item -ItemType Directory -Path $distApp | Out-Null

# C++ executables and DLLs
$cppFiles = @(
    "USBPcapGUI.exe",     # launcher
    "bhplus-core.exe",    # capture engine
    "bhplus-cli.exe",     # CLI tool
    "bhplus_sdk.dll",     # automation SDK
    "fmt.dll",
    "spdlog.dll"
)

foreach ($f in $cppFiles) {
    $src = Join-Path $binSrc $f
    if (Test-Path $src) {
        Copy-Item $src $distApp
        Write-Host "  + $f" -ForegroundColor Gray
    } else {
        Write-Warning "  Missing expected binary: $f"
    }
}

# Bundled Node.js server
$guiServerSrc = Join-Path $dist "gui-server.exe"
if (Test-Path $guiServerSrc) {
    Copy-Item $guiServerSrc $distApp
    Write-Host "  + gui-server.exe" -ForegroundColor Gray
} else {
    Write-Warning "  gui-server.exe not found – run without -SkipNodeBuild."
}

# README
$readmeSrc = Join-Path $root "README.md"
if (Test-Path $readmeSrc) {
    Copy-Item $readmeSrc $distApp
}

# ---------------------------------------------------------------------------
# Create ZIP archive
# ---------------------------------------------------------------------------
$zipPath = Join-Path $dist "USBPcapGUI-$version-win-x64.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath }

Compress-Archive -Path "$distApp\*" -DestinationPath $zipPath
Write-Host "  ZIP: $zipPath" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Package complete!"                      -ForegroundColor Green
Write-Host "  Folder : $distApp"
Write-Host "  Archive: $zipPath"
Write-Host ""
Write-Host "  To run: double-click USBPcapGUI.exe"   -ForegroundColor White
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
