# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

USBPcapGUI is a Windows USB protocol analyzer built on top of [USBPcap](https://github.com/desowin/usbpcap). It captures USB traffic via USBPcap's kernel driver and displays it in a web-based GUI. The internal codename is "BHPlus" (referenced throughout as `bhplus`).

## Architecture

```
Browser (HTML/JS) <--WebSocket--> Node.js Server <--Named Pipe JSON-RPC--> C++ Core <--IOCTL/ReadFile--> USBPcap.sys
```

**Four layers:**
- **C++ Core** (`core/`): `bhplus-core.exe` — static library `bhplus_core` + service executable. Reads pcap streams from `\\.\USBPcap[N]` devices, parses USBPCAP packet headers, runs protocol decoders, and serves JSON-RPC 2.0 over Named Pipe (`\\.\pipe\bhplus-core`). Requires admin elevation.
- **Node.js Backend** (`gui/server.js` + `core-bridge.js`): Express HTTP + WebSocket (ws) server on port 17580. Bridges JSON-RPC from C++ core to browser via WebSocket. Bundled into standalone `gui-server.exe` via `@yao-pkg/pkg` for distribution.
- **Web Frontend** (`gui/public/`): Vanilla HTML5/CSS3/JS with zero framework dependencies. Virtual-scrolling capture table, hex viewer, device tree, filter system.
- **Launcher** (`launcher/`): `USBPcapGUI.exe` — spawns `bhplus-core.exe` and `gui-server.exe`, then opens the browser.

**Other components:** `cli/` (bhplus-cli.exe), `sdk/` (bhplus_sdk.dll), `driver/` (optional NVMe/Serial kernel driver, Phase 3+).

## Build Commands

### C++ (CMake + MSVC)

```bash
# Configure (use local vcpkg — typical locations: D:\vcpkg or C:\vcpkg)
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release --parallel

# Binaries land in build/bin/Release/
```

CMake options: `BHPLUS_BUILD_CLI`, `BHPLUS_BUILD_SDK`, `BHPLUS_BUILD_TESTS`, `BHPLUS_BUILD_LAUNCHER` (all ON by default). `BHPLUS_BUILD_DRIVER` is OFF (requires WDK).

### Node.js GUI

```bash
cd gui
npm install
npm start          # serves on http://localhost:17580
```

### Tests (Google Test)

```bash
cmake --build build --config Release --target bhplus_tests
cd build && ctest -C Release --output-on-failure
```

Tests are in `tests/core_tests/`. Run a single test:
```bash
build/bin/Release/bhplus_tests.exe --gtest_filter="TestSuiteName.TestName"
```

Tests link `bhplus_core` with `WHOLE_ARCHIVE` to ensure parser self-registration works.

### Package for distribution

```powershell
.\scripts\package.ps1                    # full build + bundle + zip
.\scripts\package.ps1 -SkipCppBuild      # skip C++ rebuild
.\scripts\package.ps1 -BumpPatch         # increment patch version
```

Outputs to `dist/USBPcapGUI/` and `dist/USBPcapGUI-<version>-win-x64.zip`. Auto-increments build number in `version.json`.

## Key Technical Details

- **C++ standard**: C++20, C17. MSVC only (Windows-specific APIs throughout).
- **vcpkg dependencies**: gtest, spdlog, fmt, nlohmann-json.
- **MSVC workaround**: `FMT_USE_CONSTEVAL=0` is defined globally for fmt 12+ compatibility.
- **IPC protocol**: Newline-delimited JSON-RPC 2.0 over Named Pipe. Methods: `capture.start`, `capture.stop`, `capture.status`, `devices.list`, `hubs.list`, `usbpcap.status`, `usbpcap.install`, `usbpcap.rescan`, `device.reset`, `command.send`.
- **Parser plugin system**: Protocol parsers (`core/src/parsers/`) self-register via `ParserRegistry` at static init time. When adding a new parser, the `WHOLE_ARCHIVE` link flag ensures registration runs.
- **Shared types**: `shared/include/bhplus_types.h` defines `BHPLUS_CAPTURE_EVENT`, `BHPLUS_USB_DEVICE_INFO`, event type enums, and transfer type constants used across all C++ components.
- **Version management**: `version.json` is the single source of truth (major.minor.patch.build). `package.ps1` auto-increments and syncs to `gui/package.json`.

## Commit Message Convention

Follow existing style: `v{major}.{minor}.{patch}.{build}: brief description of changes`
