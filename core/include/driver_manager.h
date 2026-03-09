#pragma once

/*
 * USBPcapGUI - Driver Manager
 * Installs, uninstalls, and manages the kernel filter driver.
 */

#include <string>

namespace bhplus {

class DriverManager {
public:
    // Install the filter driver from the given .inf file
    static bool InstallDriver(const std::wstring& infPath);
    
    // Uninstall the filter driver
    static bool UninstallDriver();
    
    // Check if the driver is currently loaded
    static bool IsDriverLoaded();
    
    // Start the driver service
    static bool StartDriver();
    
    // Stop the driver service
    static bool StopDriver();

    // Enable test signing mode (requires admin + reboot)
    static bool EnableTestSigning();

    // Get driver version string
    static std::string GetDriverVersion();

    // ── USBPcap integration ──────────────────────────────────────────────

    /// Check if USBPcap is installed by probing \\.\ USBPcap1..
    static bool IsUSBPcapInstalled();

    /// Return how many USBPcap control interfaces are currently visible.
    static uint32_t GetUSBPcapInterfaceCount();

    /// Return true if the USBPcap service is installed.
    static bool IsUSBPcapServiceInstalled();

    /// Return true if the USBPcap service is currently running.
    static bool IsUSBPcapDriverRunning();

    /// Return true if the USB class UpperFilters contains USBPcap.
    static bool HasUSBPcapUpperFilter();

    /// Start the USBPcap kernel driver if it is installed but not running.
    static bool StartUSBPcapDriver();

    /// Find the USBPcap installer bundled next to our exe.
    /// Returns empty wstring if not found.
    static std::wstring GetUSBPcapInstallerPath();

    /// Launch the bundled USBPcap installer (ShellExecuteW with "runas").
    static bool LaunchUSBPcapInstaller();
};

} // namespace bhplus
