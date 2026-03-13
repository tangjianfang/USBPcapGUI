<#
.SYNOPSIS
    Package USBPcapGUI into a self-contained distributable folder.

.DESCRIPTION
    1. Locates Visual Studio via vswhere
    2. Re-runs CMake and builds all C++ projects (Release x64)
    3. Installs Node.js dependencies and bundles gui-server with @yao-pkg/pkg
    4. Copies everything into dist\USBPcapGUI\
    5. Creates dist\USBPcapGUI-<version>.zip
    6. Auto-increments build number in version.json

.PARAMETER BuildDir
    CMake build directory (default: build_fresh relative to repo root)

.PARAMETER SkipCppBuild
    Skip C++ rebuild (use existing binaries in BuildDir)

.PARAMETER SkipNodeBuild
    Skip Node.js bundle step

.PARAMETER BumpBuild
    (default) Increment the 4th segment: 1.0.0.1 -> 1.0.0.2

.PARAMETER BumpPatch
    Increment patch and reset build: 1.0.0.x -> 1.0.1.0

.PARAMETER BumpMinor
    Increment minor and reset patch+build: 1.0.x.y -> 1.1.0.0

.PARAMETER BumpMajor
    Increment major and reset all lower segments: 1.x.y.z -> 2.0.0.0

.EXAMPLE
    .\scripts\package.ps1                          # auto-increments build
    .\scripts\package.ps1 -SkipCppBuild
    .\scripts\package.ps1 -BumpPatch               # bigger fix release
    .\scripts\package.ps1 -BumpMinor               # feature release
    .\scripts\package.ps1 -BumpMajor               # breaking/major release
#>
param(
    [string]$BuildDir    = "",
    [switch]$SkipCppBuild,
    [switch]$SkipNodeBuild,
    [switch]$BumpBuild,    # default behaviour
    [switch]$BumpPatch,
    [switch]$BumpMinor,
    [switch]$BumpMajor
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot

# ---------------------------------------------------------------------------
# Version management — read from version.json, compute bumped version
# ---------------------------------------------------------------------------
$versionFile = Join-Path $root "version.json"
if (-not (Test-Path $versionFile)) {
    throw "version.json not found at $versionFile"
}
$versionData = Get-Content $versionFile -Raw | ConvertFrom-Json

$verMajor = [int]$versionData.major
$verMinor = [int]$versionData.minor
$verPatch = [int]$versionData.patch
$verBuild = [int]$versionData.build

# Determine which segment to bump (default: build)
if ($BumpMajor) {
    $verMajor++; $verMinor = 0; $verPatch = 0; $verBuild = 0
} elseif ($BumpMinor) {
    $verMinor++; $verPatch = 0; $verBuild = 0
} elseif ($BumpPatch) {
    $verPatch++; $verBuild = 0
} else {
    # Default: increment build number
    $verBuild++
}

$version = "$verMajor.$verMinor.$verPatch.$verBuild"

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

# Kill any running output binaries so the linker can overwrite them
$killedAny = $false
@("USBPcapGUI", "bhplus-core", "bhplus-cli", "gui-server", "node") | ForEach-Object {
    $procs = Get-Process $_ -ErrorAction SilentlyContinue
    if ($procs) {
        $procs | Stop-Process -Force
        $killedAny = $true
        Write-Host "  Stopped running process: $_" -ForegroundColor Yellow
    }
}
if ($killedAny) { Start-Sleep -Milliseconds 500 }

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
        # Use SilentlyContinue for CMake stderr -- warnings are non-fatal; only exit code matters
        $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        & $cmake @cmakeArgs
        $cmakeExit = $LASTEXITCODE
        $ErrorActionPreference = $prev
        if ($cmakeExit -ne 0) { throw "CMake configure failed (exit $cmakeExit)" }

        Write-Host "  cmake build..." -ForegroundColor Gray
        $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        & $cmake --build $BuildDir --config Release --parallel
        $cmakeExit = $LASTEXITCODE
        $ErrorActionPreference = $prev
        if ($cmakeExit -ne 0) { throw "CMake build failed (exit $cmakeExit)" }
    }
    finally {
        Pop-Location
    }

    Write-Host "  C++ build complete." -ForegroundColor Green
} else {
    Write-Host "[2/4] Skipped C++ build (--SkipCppBuild)." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Step 3 – Bundle Node.js GUI server into a standalone gui-server.exe
# ---------------------------------------------------------------------------
$guiServerExe = Join-Path $dist "gui-server.exe"

if (-not $SkipNodeBuild) {
    Write-Host ""
    Write-Host "[3/4] Bundling GUI server with @yao-pkg/pkg..." -ForegroundColor Cyan

    # Find node/npm
    $nodeCmd = Get-Command node -ErrorAction SilentlyContinue
    if (-not $nodeCmd) {
        throw "node.exe not found in PATH. Install Node.js 20+ LTS."
    }
    Write-Host "  node: $($nodeCmd.Source)"

    $guiDir = Join-Path $root "gui"
    Push-Location $guiDir
    try {
        # Install ALL deps (including devDependencies for @yao-pkg/pkg bundler)
        Write-Host "  npm install ..." -ForegroundColor Gray
        npm install --prefer-offline 2>&1
        if ($LASTEXITCODE -ne 0) { throw "npm install failed." }

        # Bundle: server.js + dependencies + public/ assets -> single exe
        # PKG_CACHE_PATH: keep the downloaded Node.js binary inside the repo
        # so it is only fetched once across all builds on this machine.
        Write-Host "  pkg bundle -> gui-server.exe ..." -ForegroundColor Gray
        $pkgCache = Join-Path $root ".pkg-cache"
        if (-not (Test-Path $pkgCache)) { New-Item -ItemType Directory -Path $pkgCache | Out-Null }
        $env:PKG_CACHE_PATH = $pkgCache
        if (-not (Test-Path $dist)) { New-Item -ItemType Directory -Path $dist | Out-Null }
        npx @yao-pkg/pkg . --targets node20-win-x64 --output $guiServerExe 2>&1
        if ($LASTEXITCODE -ne 0) { throw "pkg bundle failed." }
    }
    finally {
        Pop-Location
    }

    if (-not (Test-Path $guiServerExe)) {
        throw "gui-server.exe was not created at $guiServerExe"
    }
    Write-Host "  GUI server bundled: $guiServerExe" -ForegroundColor Green
} else {
    Write-Host "[3/4] Skipped Node build (--SkipNodeBuild)." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Step 4 – Assemble dist folder
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "[4/4] Assembling $distApp ..." -ForegroundColor Cyan

if (Test-Path $distApp) {
    # Release any file locks on the old dist folder before deleting
    Get-Process -ErrorAction SilentlyContinue | Where-Object {
        try { $_.MainModule.FileName -like "$distApp\*" } catch { $false }
    } | ForEach-Object {
        Write-Host "  Stopping process holding dist lock: $($_.Name)" -ForegroundColor Yellow
        $_ | Stop-Process -Force
    }
    Start-Sleep -Milliseconds 300
    Remove-Item -Recurse -Force $distApp
}
New-Item -ItemType Directory -Path $distApp | Out-Null

# C++ executables and DLLs
$cppFiles = @(
    "USBPcapGUI.exe",     # launcher
    "bhplus-core.exe",    # capture engine
    "bhplus-cli.exe",     # CLI tool
    "bhplus_sdk.dll"      # automation SDK
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

# vcpkg runtime DLLs (fmt, spdlog) — search both build-local and repo-root vcpkg_installed
$vcpkgDlls = @("fmt.dll", "spdlog.dll")
$vcpkgBinDirs = @(
    (Join-Path $BuildDir "vcpkg_installed\x64-windows\bin"),
    (Join-Path $root     "vcpkg_installed\x64-windows\bin"),
    $binSrc
)
foreach ($dll in $vcpkgDlls) {
    $copied = $false
    foreach ($dir in $vcpkgBinDirs) {
        $src = Join-Path $dir $dll
        if (Test-Path $src) {
            Copy-Item $src $distApp
            Write-Host "  + $dll  (from $dir)" -ForegroundColor Gray
            $copied = $true
            break
        }
    }
    if (-not $copied) {
        Write-Warning "  Missing vcpkg DLL: $dll — searched: $($vcpkgBinDirs -join ', ')"
    }
}

# Bundled GUI server (single self-contained exe, no node.exe or node_modules needed)
if (Test-Path $guiServerExe) {
    Copy-Item $guiServerExe $distApp
    Write-Host "  + gui-server.exe  (bundled, self-contained)" -ForegroundColor Gray
} else {
    Write-Warning "  gui-server.exe not found - GUI won't work. Run without -SkipNodeBuild."
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
# Persist incremented version back to version.json and gui/package.json
# ---------------------------------------------------------------------------
$newVersionData = [ordered]@{
    major    = $verMajor
    minor    = $verMinor
    patch    = $verPatch
    build    = $verBuild
    _comment = $versionData._comment
}
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($versionFile, ($newVersionData | ConvertTo-Json), $utf8NoBom)
Write-Host "  Version bumped -> $version (saved to version.json)" -ForegroundColor Cyan

# Keep gui/package.json in sync (uses 3-part semver: major.minor.patch)
$guiPkgFile = Join-Path $root "gui\package.json"
if (Test-Path $guiPkgFile) {
    $guiPkg = Get-Content $guiPkgFile -Raw | ConvertFrom-Json
    $guiPkg.version = "$verMajor.$verMinor.$verPatch"
    [System.IO.File]::WriteAllText($guiPkgFile, ($guiPkg | ConvertTo-Json -Depth 10), $utf8NoBom)
    Write-Host "  gui/package.json version -> $verMajor.$verMinor.$verPatch" -ForegroundColor Cyan
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Package complete!"                      -ForegroundColor Green
Write-Host "  Version: $version"                     -ForegroundColor Yellow
Write-Host "  Folder : $distApp"
Write-Host "  Archive: $zipPath"
Write-Host ""
Write-Host "  To run: double-click USBPcapGUI.exe"   -ForegroundColor White
Write-Host "  Next build will be: $verMajor.$verMinor.$verPatch.$([int]$verBuild+1)" -ForegroundColor DarkGray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
