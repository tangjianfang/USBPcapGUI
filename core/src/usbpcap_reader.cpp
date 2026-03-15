/*
 * USBPcapGUI - USBPcap Device Reader Implementation
 */

#include "usbpcap_reader.h"
#include <spdlog/spdlog.h>
#include <winioctl.h>
#include <setupapi.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <cassert>
#include <cstring>
#include <vector>
#include <unordered_map>

#pragma comment(lib, "setupapi.lib")

namespace bhplus {

static std::string Narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

/* ──────────── Install Check ──────────── */

// Check registry-based evidence that USBPcap is installed, without needing
// open device handles (which only appear after USB enumeration or reboot).
static bool UsbPcapRegistryInstalled() {
    // 1. Service key present in SCM
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, L"USBPcap", SERVICE_QUERY_STATUS);
        if (svc) {
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return true;
        }
        CloseServiceHandle(scm);
    }

    // 2. Registry service key
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Services\\USBPcap",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }

    // 3. USB class UpperFilters contains "USBPcap"
    DWORD type = 0, cb = 0;
    const wchar_t* usbClassKey =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{36fc9e60-c465-11cf-8056-444553540000}";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, usbClassKey, L"UpperFilters",
                     RRF_RT_REG_MULTI_SZ, &type, nullptr, &cb) == ERROR_SUCCESS && cb > 0) {
        std::vector<wchar_t> buf((cb / sizeof(wchar_t)) + 2, L'\0');
        if (RegGetValueW(HKEY_LOCAL_MACHINE, usbClassKey, L"UpperFilters",
                         RRF_RT_REG_MULTI_SZ, &type, buf.data(), &cb) == ERROR_SUCCESS) {
            for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
                if (_wcsicmp(p, L"USBPcap") == 0) return true;
            }
        }
    }

    // 4. Driver .sys file on disk
    if (GetFileAttributesW(L"C:\\Windows\\System32\\drivers\\USBPcap.sys") != INVALID_FILE_ATTRIBUTES)
        return true;
    if (GetFileAttributesW(L"C:\\Program Files\\USBPcap\\USBPcap.sys") != INVALID_FILE_ATTRIBUTES)
        return true;

    return false;
}

bool IsUsbPcapInstalled() {
    // Only cache positive results — a negative result may become positive
    // after USB re-enumeration or without requiring a reboot.
    static bool cachedTrue = false;
    if (cachedTrue) return true;

    // Fast path: try to open \\.\USBPcapN device handles
    for (int n = 1; n <= 16; ++n) {
        std::wstring path = L"\\\\.\\USBPcap" + std::to_wstring(n);
        HANDLE h = CreateFileW(path.c_str(),
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            cachedTrue = true;
            return true;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
            // Device node exists but we lack permission — driver is present
            cachedTrue = true;
            return true;
        }
    }

    // Slow path: check registry / SCM / filesystem evidence.
    // USBPcap device nodes (\\.\USBPcapN) are only created after USB
    // re-enumeration; the driver itself may be fully installed even if
    // no device node is visible yet (e.g. right after install, before
    // the first USB device plug-in or system restart).
    if (UsbPcapRegistryInstalled()) {
        // Don't cache this path — device nodes might appear soon
        return true;
    }

    return false;
}

std::wstring FindBundledUsbPcapInstaller() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    auto slash = dir.rfind(L'\\');
    if (slash != std::wstring::npos) dir.resize(slash + 1);

    for (const auto& name : {
            L"USBPcap-installer.exe",
            L"USBPcapSetup.exe",
            L"USBPcap-setup.exe" }) {
        std::wstring candidate = dir + name;
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
    }
    return {};
}

/* ──────────── Hub Enumeration ──────────── */

std::vector<RootHubInfo> EnumerateRootHubs() {
    std::vector<RootHubInfo> result;
    for (uint32_t n = 1; n <= 16; ++n) {
        RootHubInfo info;
        info.index      = n;
        info.devicePath = L"\\\\.\\USBPcap" + std::to_wstring(n);
        info.available  = false;

        HANDLE h = CreateFileW(info.devicePath.c_str(),
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);

        if (h == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                // No more devices at this index (they may not be contiguous though)
                continue;
            }
            if (err == ERROR_ACCESS_DENIED) {
                // Device exists but is already held by an active capture session.
                // Mark as available so the UI doesn't show it as broken.
                spdlog::debug("[usbpcap] \\\\.\\USBPcap{} in use (access denied) — "
                              "capture session active", n);
                info.available = true;
            } else {
                char errMsg[256] = {};
                FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, err, 0, errMsg, sizeof(errMsg), nullptr);
                for (int i = (int)strlen(errMsg) - 1;
                     i >= 0 && (errMsg[i] == '\n' || errMsg[i] == '\r' || errMsg[i] == ' ');
                     --i) errMsg[i] = '\0';
                spdlog::warn("[usbpcap] \\\\.\\USBPcap{} open failed (err={}): {}", n, err, errMsg);
                info.available = false;
            }
            result.push_back(std::move(info));
            continue;
        }

        info.available = true;

        // Query hub symlink (optional — ignore failure)
        wchar_t symBuf[512]{};
        DWORD   returned = 0;
        if (DeviceIoControl(h, IOCTL_USBPCAP_GET_HUB_SYMLINK,
                            nullptr, 0,
                            symBuf, sizeof(symBuf),
                            &returned, nullptr)) {
            // Strip NUL terminator(s) that USBPcap includes in the byte count
            DWORD wchars = returned / sizeof(wchar_t);
            while (wchars > 0 && symBuf[wchars - 1] == L'\0') --wchars;
            std::wstring raw(symBuf, wchars);

            // IOCTL_USBPCAP_GET_HUB_SYMLINK returns a kernel NT path that
            // starts with \??\ (e.g. \??\USB#ROOT_HUB30#...).
            // CreateFileW needs the Win32 device prefix \\.\  in its place.
            static const wchar_t* kKernelPfx = L"\\??\\";
            if (raw.size() > 4 && raw.compare(0, 4, kKernelPfx) == 0) {
                info.hubSymLink = std::wstring(L"\\\\.\\") + raw.substr(4);
            } else {
                info.hubSymLink = std::move(raw);
            }
            spdlog::debug("[usbpcap] USBPcap{} hub symlink: {}", n,
                std::string(info.hubSymLink.begin(), info.hubSymLink.end()));
        }

        CloseHandle(h);
        result.push_back(std::move(info));
    }
    return result;
}

/* ──────────── EnumerateUsbDevicesOnHub ──────────── */

/* ──────────── SetupAPI FriendlyName Cache ──────────── */

// Build a VID/PID → FriendlyName lookup map from the Windows device database.
// Called once (static init) and remains valid for the process lifetime.
static std::unordered_map<uint32_t, std::wstring> BuildUsbFriendlyNameMap() {
    std::unordered_map<uint32_t, std::wstring> nameMap;

    HDEVINFO hInfo = SetupDiGetClassDevsW(
        nullptr, L"USB", nullptr,
        DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hInfo == INVALID_HANDLE_VALUE) return nameMap;

    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(devData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hInfo, i, &devData); ++i) {
        // Read hardware IDs (MULTI_SZ)
        wchar_t hwIdBuf[512]{};
        if (!SetupDiGetDeviceRegistryPropertyW(hInfo, &devData, SPDRP_HARDWAREID,
                nullptr, reinterpret_cast<PBYTE>(hwIdBuf),
                sizeof(hwIdBuf) - sizeof(wchar_t), nullptr))
            continue;

        // Parse first matching VID_XXXX&PID_XXXX token
        uint32_t vid = 0, pid = 0;
        bool parsed = false;
        for (const wchar_t* p = hwIdBuf; *p; p += wcslen(p) + 1) {
            const wchar_t* pv = wcsstr(p, L"VID_");
            const wchar_t* pp = pv ? wcsstr(pv + 4, L"&PID_") : nullptr;
            if (pv && pp) {
                vid = wcstoul(pv + 4, nullptr, 16);
                pid = wcstoul(pp + 5, nullptr, 16);
                parsed = true;
                break;
            }
        }
        if (!parsed) continue;

        // Read friendly name; fall back to device description
        wchar_t nameBuf[256]{};
        const bool gotName =
            SetupDiGetDeviceRegistryPropertyW(hInfo, &devData, SPDRP_FRIENDLYNAME,
                nullptr, reinterpret_cast<PBYTE>(nameBuf),
                sizeof(nameBuf) - sizeof(wchar_t), nullptr)
            ||
            SetupDiGetDeviceRegistryPropertyW(hInfo, &devData, SPDRP_DEVICEDESC,
                nullptr, reinterpret_cast<PBYTE>(nameBuf),
                sizeof(nameBuf) - sizeof(wchar_t), nullptr);

        if (gotName && nameBuf[0]) {
            const uint32_t key = (static_cast<uint32_t>(vid) << 16) | pid;
            nameMap.emplace(key, nameBuf); // first entry wins
        }
    }

    SetupDiDestroyDeviceInfoList(hInfo);
    spdlog::debug("[usbpcap] SetupAPI USB name map built: {} entries", nameMap.size());
    return nameMap;
}

std::vector<BHPLUS_USB_DEVICE_INFO> EnumerateUsbDevicesOnHub(
    const std::wstring& hubSymLink,
    uint16_t            busIndex)
{
    std::vector<BHPLUS_USB_DEVICE_INFO> devices;

    if (hubSymLink.empty()) return devices;

    // USB hub IOCTLs (IOCTL_USB_GET_NODE_*) require FILE_ANY_ACCESS; open with
    // GENERIC_READ|GENERIC_WRITE and full share flags so enumeration can run
    // even while a capture session holds the USBPcap device.
    HANDLE hHub = CreateFileW(hubSymLink.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hHub == INVALID_HANDLE_VALUE) {
        spdlog::debug("[usbpcap] EnumerateUsbDevicesOnHub: cannot open hub '{}' (err={})",
            std::string(hubSymLink.begin(), hubSymLink.end()), GetLastError());
        return devices;
    }

    // Query number of ports
    USB_NODE_INFORMATION nodeInfo{};
    DWORD returned = 0;
    if (!DeviceIoControl(hHub,
                         IOCTL_USB_GET_NODE_INFORMATION,
                         &nodeInfo, sizeof(nodeInfo),
                         &nodeInfo, sizeof(nodeInfo),
                         &returned, nullptr)) {
        CloseHandle(hHub);
        return devices;
    }

    ULONG portCount = nodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts;

    for (ULONG port = 1; port <= portCount; ++port) {
        // Query connection info — use heap buffer to avoid MSVC C2466 (zero-length PipeList)
        std::vector<BYTE> connBuf(512, 0);
        auto* pConn = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(connBuf.data());
        pConn->ConnectionIndex = port;
        returned = 0;
        if (!DeviceIoControl(hHub,
                             IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                             pConn, static_cast<DWORD>(connBuf.size()),
                             pConn, static_cast<DWORD>(connBuf.size()),
                             &returned, nullptr)) {
            continue;
        }
        if (pConn->ConnectionStatus != DeviceConnected) continue;

        BHPLUS_USB_DEVICE_INFO dev{};
        dev.Bus           = busIndex;
        dev.DeviceAddress = pConn->DeviceAddress;
        dev.VendorId      = pConn->DeviceDescriptor.idVendor;
        dev.ProductId     = pConn->DeviceDescriptor.idProduct;
        dev.DeviceClass    = pConn->DeviceDescriptor.bDeviceClass;
        dev.DeviceSubClass = pConn->DeviceDescriptor.bDeviceSubClass;
        dev.DeviceProtocol = pConn->DeviceDescriptor.bDeviceProtocol;

        switch (pConn->Speed) {
            case UsbLowSpeed:    dev.Speed = BHPLUS_USB_SPEED_LOW;   break;
            case UsbFullSpeed:   dev.Speed = BHPLUS_USB_SPEED_FULL;  break;
            case UsbHighSpeed:   dev.Speed = BHPLUS_USB_SPEED_HIGH;  break;
            case UsbSuperSpeed:  dev.Speed = BHPLUS_USB_SPEED_SUPER; break;
            default:             dev.Speed = BHPLUS_USB_SPEED_UNKNOWN; break;
        }

        dev.IsHub = (pConn->DeviceIsHub != 0);

        // Friendly name via SetupAPI (VID/PID → SPDRP_FRIENDLYNAME / SPDRP_DEVICEDESC)
        {
            static const auto nameMap = BuildUsbFriendlyNameMap();
            const uint32_t key = (static_cast<uint32_t>(dev.VendorId) << 16) | dev.ProductId;
            auto it = nameMap.find(key);
            if (it != nameMap.end()) {
                wcsncpy_s(dev.DeviceName, BHPLUS_MAX_DEVICE_NAME,
                          it->second.c_str(), _TRUNCATE);
            } else {
                // Fallback: VID:PID notation
                swprintf_s(dev.DeviceName, BHPLUS_MAX_DEVICE_NAME,
                           L"VID_%04X&PID_%04X", dev.VendorId, dev.ProductId);
            }
        }
        dev.SerialNumber[0] = L'\0';

        // Store the hub symlink for later use
        wcsncpy_s(dev.RootHubSymLink,
                  ARRAYSIZE(dev.RootHubSymLink),
                  hubSymLink.c_str(),
                  _TRUNCATE);

        devices.push_back(dev);
    }

    CloseHandle(hHub);
    return devices;
}

/* ──────────── UsbPcapReader ──────────── */

/* ---- FindUSBPcapCMD ---- */

std::wstring UsbPcapReader::FindUSBPcapCMD() {
    // 1. PATH / current directory
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"USBPcapCMD.exe", nullptr, MAX_PATH, found, nullptr))
        return found;

    // 2. Default install locations
    const wchar_t* wellKnown[] = {
        L"C:\\Program Files\\USBPcap\\USBPcapCMD.exe",
        L"C:\\Program Files (x86)\\USBPcap\\USBPcapCMD.exe",
    };
    for (auto p : wellKnown) {
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES)
            return p;
    }

    // 3. Registry install path
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\USBPcap", 0,
                      KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t installDir[MAX_PATH]{};
        DWORD cb = sizeof(installDir);
        if (RegGetValueW(hKey, nullptr, L"InstallDir",
                         RRF_RT_REG_SZ, nullptr,
                         installDir, &cb) == ERROR_SUCCESS) {
            std::wstring cand = std::wstring(installDir) + L"\\USBPcapCMD.exe";
            if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
                RegCloseKey(hKey);
                return cand;
            }
        }
        RegCloseKey(hKey);
    }

    // 4. Next to our own executable
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    auto slash = dir.rfind(L'\\');
    if (slash != std::wstring::npos) {
        dir.resize(slash + 1);
        std::wstring cand = dir + L"USBPcapCMD.exe";
        if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES)
            return cand;
    }

    return {};
}

/* ---- Constructor / Destructor ---- */

UsbPcapReader::UsbPcapReader(const RootHubInfo& hub)
    : m_hub(hub)
{
    m_filter.filterAll = 1;  // default: capture all devices on this hub
}

UsbPcapReader::~UsbPcapReader() {
    StopCapture();
}

/* ---- Open / Close ---- */

bool UsbPcapReader::Open(uint32_t snapshotLen, uint32_t bufferLen) {
    if (m_opened) return true;
    m_accessDenied = false;
    m_snapshotLen = (snapshotLen == 0) ? 65535u   : snapshotLen;
    m_bufferLen   = (bufferLen   == 0) ? 1048576u : bufferLen;
    m_opened = true;

    spdlog::info("[usbpcap] Configured {} snaplen={} buflen={}",
        Narrow(m_hub.devicePath), m_snapshotLen, m_bufferLen);
    return true;
}

void UsbPcapReader::Close() {
    StopCapture();
    m_opened = false;
}

void UsbPcapReader::SetFilter(const std::vector<uint16_t>& deviceAddresses) {
    // Filter is cached here and converted to command-line args in StartCapture().
    m_filter = {};
    if (deviceAddresses.empty()) {
        m_filter.filterAll = 1;
    } else {
        m_filter.filterAll = 0;
        for (uint16_t addr : deviceAddresses) {
            if (addr >= 1 && addr <= 127) {
                uint8_t idx = (uint8_t)(addr - 1);
                m_filter.addresses[idx / 32] |= (1u << (idx % 32));
            }
        }
    }
}

bool UsbPcapReader::StartCapture(EventCallback cb,
                                  std::atomic<uint64_t>& seqCounter) {
    if (m_running.load()) return true;
    if (!m_opened) {
        m_lastError = "Not configured";
        return false;
    }

    // ── 1. Locate USBPcapCMD.exe ──
    std::wstring cmdExe = FindUSBPcapCMD();
    if (cmdExe.empty()) {
        m_lastError = "USBPcapCMD.exe not found — ensure USBPcap is properly installed";
        spdlog::error("[usbpcap] {}", m_lastError);
        return false;
    }

    // ── 2. Build command line ──
    //   USBPcapCMD.exe -d \\.\USBPcap1 -o - -s 65535 -b 1048576 [-A | -a addr ...]
    std::wstring cmdLine = L"\"" + cmdExe + L"\""
        + L" -d " + m_hub.devicePath
        + L" -o -"
        + L" -s " + std::to_wstring(m_snapshotLen)
        + L" -b " + std::to_wstring(m_bufferLen);

    if (m_filter.filterAll) {
        cmdLine += L" -A";
    } else {
        for (int addr = 1; addr <= 127; ++addr) {
            int idx = addr - 1;
            if (m_filter.addresses[idx / 32] & (1u << (idx % 32))) {
                cmdLine += L" -a " + std::to_wstring(addr);
            }
        }
    }

    spdlog::info("[usbpcap] Launching: {}", Narrow(cmdLine));

    // ── 3. Create anonymous pipes for subprocess stdout AND stderr ──
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe  = INVALID_HANDLE_VALUE;
    HANDLE hWritePipe = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        m_lastError = "CreatePipe failed: " + std::to_string(GetLastError());
        spdlog::error("[usbpcap] {}", m_lastError);
        return false;
    }
    // Read end must NOT be inherited by child
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    // Stderr pipe — captured to spdlog::warn for diagnostics
    HANDLE hStderrRead  = INVALID_HANDLE_VALUE;
    HANDLE hStderrWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        // Non-fatal: fall back to NUL
        hStderrRead = hStderrWrite = INVALID_HANDLE_VALUE;
    } else {
        SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);
    }

    // ── 4. Spawn USBPcapCMD.exe ──
    // NUL handle for stdin only
    HANDLE hNulR = CreateFileW(L"NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = hNulR;
    si.hStdOutput = hWritePipe;
    si.hStdError  = (hStderrWrite != INVALID_HANDLE_VALUE) ? hStderrWrite : hWritePipe;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        nullptr,
        const_cast<LPWSTR>(cmdLine.c_str()),
        nullptr, nullptr,
        TRUE,               // inherit handles
        CREATE_NO_WINDOW,   // no console flash
        nullptr, nullptr,
        &si, &pi);

    // Close handles no longer needed in the parent
    CloseHandle(hWritePipe);
    if (hNulR != INVALID_HANDLE_VALUE) CloseHandle(hNulR);
    // Close stderr write end — child holds its own copy now
    if (hStderrWrite != INVALID_HANDLE_VALUE) CloseHandle(hStderrWrite);

    if (!ok) {
        DWORD err = GetLastError();
        CloseHandle(hReadPipe);
        if (hStderrRead != INVALID_HANDLE_VALUE) CloseHandle(hStderrRead);
        m_lastError = "Failed to start USBPcapCMD.exe: error " + std::to_string(err);
        spdlog::error("[usbpcap] {}", m_lastError);
        return false;
    }

    m_procInfo     = pi;
    m_handle       = hReadPipe;       // PcapStream reads from this
    m_stderrHandle = hStderrRead;     // StderrLoop reads from this

    spdlog::info("[usbpcap] USBPcapCMD.exe started (PID={}) for {}",
        pi.dwProcessId, Narrow(m_hub.devicePath));

    // ── 5. Start read threads ──
    m_running = true;
    m_thread = std::thread([this, cb, &seqCounter]() {
        ReadLoop(cb, seqCounter);
    });
    if (m_stderrHandle != INVALID_HANDLE_VALUE) {
        m_stderrThread = std::thread([this]() { StderrLoop(); });
    }
    return true;
}

void UsbPcapReader::StopCapture() {
    if (!m_running.exchange(false)) return;

    // Terminate the subprocess — this closes the write end of both pipes.
    if (m_procInfo.hProcess) {
        TerminateProcess(m_procInfo.hProcess, 0);
        WaitForSingleObject(m_procInfo.hProcess, 5000);
    }

    // Close stdout pipe to unblock ReadFile in ReadLoop.
    if (m_handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_handle, nullptr);
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }

    if (m_thread.joinable()) m_thread.join();

    // Close stderr pipe to unblock StderrLoop, then join.
    if (m_stderrHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_stderrHandle);
        m_stderrHandle = INVALID_HANDLE_VALUE;
    }
    if (m_stderrThread.joinable()) m_stderrThread.join();

    // Clean up process handles
    if (m_procInfo.hProcess) {
        CloseHandle(m_procInfo.hProcess);
        CloseHandle(m_procInfo.hThread);
        m_procInfo = {};
    }
}

void UsbPcapReader::StderrLoop() {
    char    buf[512];
    std::string line;
    DWORD   rd = 0;
    const std::string hubName = Narrow(m_hub.devicePath);

    while (ReadFile(m_stderrHandle, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
        buf[rd] = '\0';
        line += buf;
        size_t pos;
        while ((pos = line.find('\n')) != std::string::npos) {
            std::string msg = line.substr(0, pos);
            if (!msg.empty() && msg.back() == '\r') msg.pop_back();
            if (!msg.empty())
                spdlog::warn("[usbpcap-cmd] {} stderr: {}", hubName, msg);
            line = line.substr(pos + 1);
        }
    }
    if (!line.empty())
        spdlog::warn("[usbpcap-cmd] {} stderr: {}", hubName, line);
}

void UsbPcapReader::ReadLoop(EventCallback cb,
                              std::atomic<uint64_t>& seqCounter) {
    spdlog::info("[usbpcap] ReadLoop start: {}",
        Narrow(m_hub.devicePath));

    PcapStream stream(m_handle);
    if (!stream.ReadGlobalHeader()) {
        // Pipe closed usually means USBPcapCMD.exe exited immediately
        // (e.g. hub has no devices or exclusive access denied).
        // Check stderr log for the reason — this is not a fatal process error.
        spdlog::warn("[usbpcap] {} ReadGlobalHeader: {} — USBPcapCMD exited early "
                     "(check [usbpcap-cmd] stderr lines for details)",
                     Narrow(m_hub.devicePath), stream.lastError());
        m_running = false;
        return;
    }

    constexpr uint64_t EXPIRE_INTERVAL_US  = 1'000'000ULL;  // 1s in µs
    constexpr uint64_t STATS_INTERVAL_US   = 30'000'000ULL; // 30s in µs
    uint64_t lastExpire  = 0;
    uint64_t lastStatLog = 0;
    uint64_t pktCount    = 0;
    const std::string hubName = Narrow(m_hub.devicePath);

    while (m_running.load()) {
        UsbPcapRecord rec;
        if (!stream.ReadNextPacket(rec)) {
            if (m_running.load()) {
                spdlog::warn("[usbpcap] ReadNextPacket: {}", stream.lastError());
            }
            break;
        }

        uint64_t seq = seqCounter.fetch_add(1, std::memory_order_relaxed);
        BHPLUS_CAPTURE_EVENT evt{};
        PcapStream::RecordToEvent(rec, evt, seq);

        // IRP pairing
        const bool isCompletion = (evt.Direction == BHPLUS_DIR_UP);
        if (!isCompletion) {
            // This is a request — save it in the table
            IrpPairingTable::PendingEntry pending{ evt.Timestamp, seq };
            m_irpTable.Insert(evt.IrpId, pending);
        } else {
            // Completion — try to pair
            IrpPairingTable::PendingEntry pending{};
            if (m_irpTable.Consume(evt.IrpId, pending)) {
                evt.Duration = (evt.Timestamp > pending.timestamp)
                    ? (evt.Timestamp - pending.timestamp)
                    : 0ULL;
            }
        }

        // Periodic table expiry (remove unmatched requests older than 5s)
        uint64_t now = evt.Timestamp;
        if (now - lastExpire > EXPIRE_INTERVAL_US) {
            m_irpTable.Expire(now);
            lastExpire = now;
        }

        // ── Packet diagnostic logging ─────────────────────────────────────
        ++pktCount;
        const bool diagNow = (pktCount <= 50) || (pktCount % 500 == 0);
        if (diagNow) {
            const char* typeStr = UsbTransferTypeName(evt.TransferType);
            const char* dirStr  = (evt.Direction == BHPLUS_DIR_DOWN) ? ">>>" : "<<<";

            if (evt.TransferType == BHPLUS_USB_TRANSFER_CONTROL &&
                rec.controlStage == BHPLUS_USB_CONTROL_STAGE_SETUP) {
                const uint8_t* sp = rec.setupPacket;
                const uint16_t wValue = static_cast<uint16_t>(sp[2] | (sp[3] << 8));
                const uint16_t wIndex = static_cast<uint16_t>(sp[4] | (sp[5] << 8));
                spdlog::debug("[pcap-diag] {} pkt={} {} dev={} ep={:#04x} {} "
                              "SETUP req={} val={:#06x} idx={} len={}",
                    hubName, pktCount, dirStr,
                    evt.Device, evt.Endpoint, typeStr,
                    UsbStandardRequestName(sp[1]),
                    wValue, wIndex, evt.DataLength);
            } else {
                spdlog::debug("[pcap-diag] {} pkt={} {} dev={} ep={:#04x} {} "
                              "dataLen={} status={:#010x} irp={:#018x}",
                    hubName, pktCount, dirStr,
                    evt.Device, evt.Endpoint, typeStr,
                    evt.DataLength, evt.Status, evt.IrpId);
            }
        }

        // 30-second stats
        if (lastStatLog == 0) lastStatLog = now;
        if (now > lastStatLog && (now - lastStatLog) >= STATS_INTERVAL_US) {
            spdlog::info("[pcap-diag] {} total packets captured: {}", hubName, pktCount);
            lastStatLog = now;
        }
        // ─────────────────────────────────────────────────────────────────

        if (cb) cb(std::move(evt), std::move(rec.data));
    }

    m_running = false;
    spdlog::info("[usbpcap] ReadLoop end: {}",
        Narrow(m_hub.devicePath));
}

/* ──────────── UsbPcapMultiReader ──────────── */

bool UsbPcapMultiReader::Open(const BHPLUS_CAPTURE_CONFIG& config) {
    m_config = config;
    m_readers.clear();

    // Early check: verify USBPcapCMD.exe is available (needed for capture)
    std::wstring cmdPath = UsbPcapReader::FindUSBPcapCMD();
    if (cmdPath.empty()) {
        m_lastError = "USBPcapCMD.exe not found. Please ensure USBPcap is properly installed.";
        spdlog::error("[multi] {}", m_lastError);
        return false;
    }
    spdlog::info("[multi] Found USBPcapCMD: {}", Narrow(cmdPath));

    auto hubs = EnumerateRootHubs();
    if (hubs.empty()) {
        // Distinguish between "not installed" and "installed but no interfaces yet"
        if (UsbPcapRegistryInstalled()) {
            m_lastError = "USBPcap is installed but no capture interfaces are visible yet. "
                          "Restart the computer or re-plug a USB device to complete the driver setup.";
            spdlog::warn("[multi] {}", m_lastError);
        } else {
            m_lastError = "No USBPcap capture interfaces found. "
                          "Please install USBPcap from https://desowin.org/usbpcap/";
            spdlog::error("[multi] {}", m_lastError);
        }
        return false;
    }

    bool anyOpen = false;
    bool anyDenied = false;
    for (auto& hub : hubs) {
        // If FilterBus is set, skip other hubs
        if (config.FilterBus != 0 && hub.index != config.FilterBus) continue;
        if (!hub.available) continue;

        auto reader = std::make_unique<UsbPcapReader>(hub);
        uint32_t snapLen = (config.SnapshotLength > 0) ? config.SnapshotLength : 65535u;
        uint32_t bufLen  = (config.BufferLength   > 0) ? config.BufferLength   : 1048576u;
        if (!reader->Open(snapLen, bufLen)) {
            if (reader->WasAccessDenied()) anyDenied = true;
            spdlog::warn("[multi] Cannot open hub {}: {}", hub.index, reader->LastError());
            continue;
        }

        // Build address filter list from config.
        // CaptureAllDevices == 1  → pass empty list so SetFilter uses filterAll=true (-A flag).
        // CaptureAllDevices == 0  → pass only the explicit device addresses.
        std::vector<uint16_t> addrs;
        if (!config.CaptureAllDevices) {
            for (size_t i = 0; i < config.FilterDeviceCount && i < BHPLUS_MAX_FILTER_DEVICES; ++i) {
                addrs.push_back(config.FilterDeviceAddresses[i]);
            }
        }
        reader->SetFilter(addrs);

        m_readers.push_back(std::move(reader));
        anyOpen = true;
    }

    if (!anyOpen) {
        if (anyDenied) {
            m_lastError = "Access denied opening USBPcap device(s). "
                          "Please run bhplus-core.exe as Administrator.";
            spdlog::error("[multi] {}", m_lastError);
        } else {
            m_lastError = "Failed to open any USBPcap device";
            spdlog::warn("[multi] {}", m_lastError);
        }
        return false;
    }

    spdlog::info("[multi] Opened {} hub(s)", m_readers.size());
    return true;
}

void UsbPcapMultiReader::Close() {
    StopCapture();
    m_readers.clear();
}

bool UsbPcapMultiReader::StartCapture(EventCallback cb) {
    bool allOk = true;
    for (auto& r : m_readers) {
        if (!r->StartCapture(cb, m_seqCounter)) {
            spdlog::error("[multi] StartCapture failed for hub {}: {}",
                r->HubInfo().index, r->LastError());
            allOk = false;
        }
    }
    return allOk;
}

void UsbPcapMultiReader::StopCapture() {
    for (auto& r : m_readers) r->StopCapture();
}

bool UsbPcapMultiReader::IsCapturing() const {
    for (const auto& r : m_readers) {
        if (r->IsCapturing()) return true;
    }
    return false;
}

std::vector<BHPLUS_USB_DEVICE_INFO> UsbPcapMultiReader::EnumerateDevices() const {
    std::vector<BHPLUS_USB_DEVICE_INFO> all;
    for (const auto& r : m_readers) {
        auto devs = EnumerateUsbDevicesOnHub(
            r->HubInfo().hubSymLink,
            static_cast<uint16_t>(r->HubInfo().index));
        all.insert(all.end(), devs.begin(), devs.end());
    }
    return all;
}

} // namespace bhplus
