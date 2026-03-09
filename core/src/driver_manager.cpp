/*
 * USBPcapGUI - Driver Manager Implementation
 */

#include "driver_manager.h"
#include <spdlog/spdlog.h>
#include <windows.h>
#include <shellapi.h>
#include <newdev.h>
#include <cfgmgr32.h>

#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "shell32.lib")

namespace bhplus {

bool DriverManager::IsDriverLoaded() {
    SC_HANDLE scManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager)
        return false;

    SC_HANDLE service = OpenServiceW(scManager, L"BHPlus", SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    SERVICE_STATUS status = {};
    BOOL result = QueryServiceStatus(service, &status);
    
    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return result && status.dwCurrentState == SERVICE_RUNNING;
}

bool DriverManager::InstallDriver(const std::wstring& infPath) {
    spdlog::info("Installing driver from INF...");
    
    BOOL rebootRequired = FALSE;
    // Use DiInstallDriver for PnP filter driver installation
    // For non-PnP control device, use SCM CreateService
    
    SC_HANDLE scManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scManager) {
        spdlog::error("Failed to open SCM: error {}", GetLastError());
        return false;
    }

    SC_HANDLE service = CreateServiceW(
        scManager,
        L"BHPlus",
        L"USBPcapGUI Filter Driver",
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        infPath.c_str(),  // Should be full path to .sys file
        nullptr, nullptr, nullptr, nullptr, nullptr
    );

    if (!service) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            spdlog::info("Driver service already exists");
            CloseServiceHandle(scManager);
            return true;
        }
        spdlog::error("Failed to create service: error {}", err);
        CloseServiceHandle(scManager);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    spdlog::info("Driver installed successfully");
    return true;
}

bool DriverManager::UninstallDriver() {
    StopDriver();

    SC_HANDLE scManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = OpenServiceW(scManager, L"BHPlus", DELETE);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    BOOL result = DeleteService(service);
    
    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return result != FALSE;
}

bool DriverManager::StartDriver() {
    SC_HANDLE scManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = OpenServiceW(scManager, L"BHPlus", SERVICE_START);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    BOOL result = StartServiceW(service, 0, nullptr);
    
    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return result != FALSE;
}

bool DriverManager::StopDriver() {
    SC_HANDLE scManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = OpenServiceW(scManager, L"BHPlus", SERVICE_STOP);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    SERVICE_STATUS status = {};
    BOOL result = ControlService(service, SERVICE_CONTROL_STOP, &status);
    
    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return result != FALSE;
}

bool DriverManager::EnableTestSigning() {
    // Must run as admin
    int result = system("bcdedit /set testsigning on");
    return result == 0;
}

std::string DriverManager::GetDriverVersion() {
    // TODO: Query driver version via IOCTL
    return "0.1.0";
}

// ── USBPcap ──────────────────────────────────────────────────────────────

bool DriverManager::IsUSBPcapInstalled() {
    for (int n = 1; n <= 16; ++n) {
        std::wstring path = L"\\\\.\\USBPcap" + std::to_wstring(n);
        HANDLE h = CreateFileW(path.c_str(),
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return true;
        }
        DWORD err = GetLastError();
        // ERROR_ACCESS_DENIED still means the device exists
        if (err == ERROR_ACCESS_DENIED) return true;
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
            return true;
    }
    // Also check registry key as fallback
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\USBPcap",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

std::wstring DriverManager::GetUSBPcapInstallerPath() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    auto slash = dir.rfind(L'\\');
    if (slash != std::wstring::npos) dir.resize(slash + 1);

    for (const wchar_t* name : {
            L"USBPcap-installer.exe",
            L"USBPcapSetup.exe",
            L"USBPcap-setup.exe" }) {
        std::wstring candidate = dir + name;
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
    }
    return {};
}

bool DriverManager::LaunchUSBPcapInstaller() {
    std::wstring path = GetUSBPcapInstallerPath();
    if (path.empty()) {
        spdlog::error("USBPcap installer not found next to exe");
        return false;
    }
    HINSTANCE result = ShellExecuteW(nullptr, L"runas", path.c_str(),
                                     nullptr, nullptr, SW_SHOWDEFAULT);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace bhplus
