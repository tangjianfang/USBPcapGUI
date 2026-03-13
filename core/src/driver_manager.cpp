/*
 * USBPcapGUI - Driver Manager Implementation
 */

#include "driver_manager.h"
#include <spdlog/spdlog.h>
#include <windows.h>
#include <shellapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <vector>

#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "shell32.lib")

namespace bhplus {

namespace {

constexpr wchar_t kUsbPcapServiceName[] = L"USBPcap";
constexpr wchar_t kUsbClassKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{36fc9e60-c465-11cf-8056-444553540000}";
constexpr wchar_t kUsbPcapServiceKey[] = L"SYSTEM\\CurrentControlSet\\Services\\USBPcap";

uint32_t ProbeUsbPcapInterfaceCount() {
    uint32_t count = 0;
    for (int n = 1; n <= 16; ++n) {
        std::wstring path = L"\\\\.\\USBPcap" + std::to_wstring(n);
        HANDLE h = CreateFileW(path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            ++count;
            CloseHandle(h);
            continue;
        }

        const DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION) {
            ++count;
        }
    }
    return count;
}

bool RegistryKeyExists(HKEY root, const wchar_t* subKey) {
    HKEY hKey = nullptr;
    const LONG rc = RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey);
    if (rc == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool RegistryMultiSzContains(HKEY root,
                             const wchar_t* subKey,
                             const wchar_t* valueName,
                             const wchar_t* needle) {
    DWORD type = 0;
    DWORD size = 0;
    LONG rc = RegGetValueW(root, subKey, valueName,
                           RRF_RT_REG_MULTI_SZ, &type, nullptr, &size);
    if (rc != ERROR_SUCCESS || size == 0) {
        return false;
    }

    std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 2, L'\0');
    rc = RegGetValueW(root, subKey, valueName,
                      RRF_RT_REG_MULTI_SZ, &type, buffer.data(), &size);
    if (rc != ERROR_SUCCESS) {
        return false;
    }

    for (const wchar_t* p = buffer.data(); *p != L'\0'; p += wcslen(p) + 1) {
        if (_wcsicmp(p, needle) == 0) {
            return true;
        }
    }
    return false;
}

bool ServiceExists(const wchar_t* name) {
    SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = OpenServiceW(scManager, name, SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);
    return true;
}

bool QueryServiceRunning(const wchar_t* name) {
    SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = OpenServiceW(scManager, name, SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    const BOOL ok = QueryServiceStatusEx(
        service,
        SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status),
        sizeof(status),
        &bytesNeeded);

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return ok && status.dwCurrentState == SERVICE_RUNNING;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf) {
    if (dir.empty()) return leaf;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + leaf;
    return dir + L"\\" + leaf;
}

std::wstring FindInstallerInDirectory(const std::wstring& dir) {
    if (dir.empty()) return {};

    for (const wchar_t* name : {
             L"USBPcap-installer.exe",
             L"USBPcapSetup.exe",
             L"USBPcap-setup.exe",
             L"USBPcapSetup-1.5.4.0.exe" }) {
        std::wstring candidate = JoinPath(dir, name);
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return candidate;
        }
    }

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = JoinPath(dir, L"USBPcap*.exe");
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::wstring result = JoinPath(dir, data.cFileName);
    FindClose(find);
    return result;
}

std::vector<std::wstring> GetInstallerSearchRoots() {
    std::vector<std::wstring> roots;

    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0) {
        std::wstring exeDir(exePath);
        const auto slash = exeDir.rfind(L'\\');
        if (slash != std::wstring::npos) {
            exeDir.resize(slash);
            roots.push_back(exeDir);
            roots.push_back(JoinPath(exeDir, L"USBPcap"));
        }
    }

    wchar_t cwd[MAX_PATH]{};
    if (GetCurrentDirectoryW(MAX_PATH, cwd) != 0) {
        roots.push_back(cwd);
        roots.push_back(JoinPath(cwd, L"USBPcap"));
        roots.push_back(JoinPath(cwd, L"usbPcap"));
    }

    return roots;
}

} // namespace

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
    return GetUSBPcapInterfaceCount() > 0 ||
           IsUSBPcapServiceInstalled() ||
           HasUSBPcapUpperFilter() ||
           RegistryKeyExists(HKEY_LOCAL_MACHINE, kUsbPcapServiceKey) ||
           GetFileAttributesW(L"C:\\Program Files\\USBPcap\\USBPcap.sys") != INVALID_FILE_ATTRIBUTES;
}

uint32_t DriverManager::GetUSBPcapInterfaceCount() {
    return ProbeUsbPcapInterfaceCount();
}

bool DriverManager::IsUSBPcapServiceInstalled() {
    return ServiceExists(kUsbPcapServiceName) ||
           RegistryKeyExists(HKEY_LOCAL_MACHINE, kUsbPcapServiceKey);
}

bool DriverManager::IsUSBPcapDriverRunning() {
    return QueryServiceRunning(kUsbPcapServiceName);
}

bool DriverManager::HasUSBPcapUpperFilter() {
    return RegistryMultiSzContains(HKEY_LOCAL_MACHINE,
                                   kUsbClassKey,
                                   L"UpperFilters",
                                   L"USBPcap");
}

bool DriverManager::StartUSBPcapDriver() {
    SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = OpenServiceW(scManager,
                                     kUsbPcapServiceName,
                                     SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    BOOL started = StartServiceW(service, 0, nullptr);
    const DWORD err = started ? ERROR_SUCCESS : GetLastError();
    const bool running = started || err == ERROR_SERVICE_ALREADY_RUNNING || QueryServiceRunning(kUsbPcapServiceName);

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);
    return running;
}

std::wstring DriverManager::GetUSBPcapInstallerPath() {
    for (const auto& dir : GetInstallerSearchRoots()) {
        std::wstring candidate = FindInstallerInDirectory(dir);
        if (!candidate.empty()) {
            return candidate;
        }
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

bool DriverManager::DetectVirtualUsbEnvironment() {
    // Known virtual/emulated USB host controller registry keys
    static const wchar_t* kVirtualKeys[] = {
        // USB/IP Windows (wuying cloud PC, VirtualHere, usbipwin)
        L"SYSTEM\\CurrentControlSet\\Enum\\USBIPWIN",
        // Hyper-V / VirtualBox virtual USB
        L"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_80EE&PID_0021",  // VirtualBox OHCI
        L"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_8087&PID_0024",  // VirtualBox EHCI
    };

    for (auto keyPath : kVirtualKeys) {
        HKEY h = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &h) == ERROR_SUCCESS) {
            RegCloseKey(h);
            return true;
        }
    }

    // Check if all USB root hubs are controlled by non-standard (virtual) drivers
    // by checking if there are ANY PCI-attached USB host controllers
    // (class "USB", parent begins with "PCI\\")
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
                                             nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devData{};
        devData.cbSize = sizeof(devData);
        bool foundPhysical = false;
        for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {
            wchar_t instanceId[256]{};
            if (SetupDiGetDeviceInstanceIdW(devInfo, &devData, instanceId,
                                             ARRAYSIZE(instanceId), nullptr)) {
                // PCI-attached controllers have instance IDs starting with "PCI\"
                if (_wcsnicmp(instanceId, L"PCI\\", 4) == 0) {
                    foundPhysical = true;
                    break;
                }
            }
        }
        SetupDiDestroyDeviceInfoList(devInfo);
        if (!foundPhysical) return true;   // no physical PCI USB controllers found
    }

    return false;
}

bool DriverManager::RescanUsbDevices() {
    // Use CM_Locate_DevNode on the root + CM_Reenumerate_DevNode for a full rescan
    DEVINST rootInst = 0;
    CONFIGRET cr = CM_Locate_DevNodeW(&rootInst, nullptr, CM_LOCATE_DEVNODE_NORMAL);
    if (cr != CR_SUCCESS) {
        spdlog::warn("[driver] CM_Locate_DevNode failed: 0x{:x}", cr);
        return false;
    }
    cr = CM_Reenumerate_DevNode(rootInst, CM_REENUMERATE_RETRY_INSTALLATION);
    if (cr != CR_SUCCESS) {
        spdlog::warn("[driver] CM_Reenumerate_DevNode failed: 0x{:x}", cr);
        return false;
    }
    spdlog::info("[driver] USB device rescan initiated");
    return true;
}

} // namespace bhplus
