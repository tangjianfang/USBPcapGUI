# USBPcapGUI - Kernel Driver

## Overview
This directory contains the KMDF filter driver that captures I/O from device stacks.

## Building
The driver must be built with the Windows Driver Kit (WDK) using MSBuild.

### Prerequisites
- Visual Studio 2022
- Windows Driver Kit (WDK) 10.0.26100+
- Windows SDK 10.0.26100+

### Build
```cmd
msbuild bhplus.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Test Signing
During development, enable test signing:
```cmd
bcdedit /set testsigning on
```

Then sign the driver with a test certificate:
```cmd
makecert -r -pe -ss PrivateCertStore -n "CN=BHPlusTestCert" BHPlusTest.cer
signtool sign /s PrivateCertStore /n "BHPlusTestCert" /t http://timestamp.digicert.com bhplus.sys
```

### Installation
```cmd
pnputil /add-driver bhplus.inf /install
```

Or using devcon:
```cmd
devcon install bhplus.inf Root\BHPlus
```

## Architecture
The driver operates as an upper filter driver in the device stack:

```
Application  →  bhplus.sys (filter)  →  Function Driver  →  Hardware
```

It intercepts IRPs flowing through the device stack and copies relevant
information to a ring buffer accessible from user mode via IOCTL.
