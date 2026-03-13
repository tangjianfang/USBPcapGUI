# USBPcapGUI

A modern, open-source USBPcap-based protocol analyzer GUI for Windows.

## Features (Planned)

- **USB 1.0 - 4.0** protocol capture and analysis
- **NVMe / SATA / SCSI** storage protocol capture
- **Serial port** monitoring
- Real-time I/O data display
- Protocol decoding with hex view
- Custom command builder & sender
- Conditional triggering
- Export to CSV, TXT, PCAP formats
- Command-line capture tool
- Automation SDK

## Architecture

```
Browser (HTML/JS) ←WebSocket→ Node.js Server ←JSON-RPC→ C++ Core ←IOCTL→ Kernel Driver
                                                         ↑
                                                    ETW Consumer
```

See [docs/DESIGN.md](docs/DESIGN.md) for the full design document.

## Building

### Prerequisites

- Visual Studio 2022 with C++ workload
- Windows SDK 10.0.26100+
- Windows Driver Kit (WDK) 10.0.26100+ (for driver)
- Node.js 20+ LTS (for Web GUI)
- CMake 3.28+
- vcpkg

### Build Steps

```powershell
# Clone
git clone https://github.com/user/USBPcapGUI.git
cd USBPcapGUI

# Build C++ core & CLI
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Install & start Web GUI
cd gui
npm install
npm start       # Opens browser at http://localhost:17580

# Build driver (separate, requires WDK)
cd ../driver
msbuild bhplus.sln /p:Configuration=Release /p:Platform=x64
```

## Development

Enable test signing mode for driver development:
```cmd
bcdedit /set testsigning on
```

Use a VM for kernel driver testing to avoid BSOD on your main machine.

## License

TBD
