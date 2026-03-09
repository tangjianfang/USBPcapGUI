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

#pragma comment(lib, "setupapi.lib")

namespace bhplus {

/* ──────────── Install Check ──────────── */

bool IsUsbPcapInstalled() {
    // Cache result — probing 16 device handles is too slow to call repeatedly
    static int cachedResult = -1;
    if (cachedResult != -1) return cachedResult == 1;

    // Quick probe: try to open \\.\USBPcap1
    for (int n = 1; n <= 16; ++n) {
        std::wstring path = L"\\\\.\\USBPcap" + std::to_wstring(n);
        HANDLE h = CreateFileW(path.c_str(),
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            cachedResult = 1;
            return true;
        }
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
            // Device exists but may need elevation — still installed
            cachedResult = 1;
            return true;
        }
    }
    cachedResult = 0;
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
            // Access denied or other — device exists but can't open
            info.available = false;
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
            info.hubSymLink = std::wstring(symBuf,
                returned / sizeof(wchar_t));
        }

        CloseHandle(h);
        result.push_back(std::move(info));
    }
    return result;
}

/* ──────────── EnumerateUsbDevicesOnHub ──────────── */

std::vector<BHPLUS_USB_DEVICE_INFO> EnumerateUsbDevicesOnHub(
    const std::wstring& hubSymLink,
    uint16_t            busIndex)
{
    std::vector<BHPLUS_USB_DEVICE_INFO> devices;

    if (hubSymLink.empty()) return devices;

    HANDLE hHub = CreateFileW(hubSymLink.c_str(),
        GENERIC_WRITE, FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hHub == INVALID_HANDLE_VALUE) return devices;

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
        dev.DeviceClass   = pConn->DeviceDescriptor.bDeviceClass;

        switch (pConn->Speed) {
            case UsbLowSpeed:    dev.Speed = BHPLUS_USB_SPEED_LOW;   break;
            case UsbFullSpeed:   dev.Speed = BHPLUS_USB_SPEED_FULL;  break;
            case UsbHighSpeed:   dev.Speed = BHPLUS_USB_SPEED_HIGH;  break;
            case UsbSuperSpeed:  dev.Speed = BHPLUS_USB_SPEED_SUPER; break;
            default:             dev.Speed = BHPLUS_USB_SPEED_UNKNOWN; break;
        }

        dev.IsHub = (pConn->DeviceIsHub != 0);

        // Friendly name: try to read string descriptor index 2 (product)
        // (simplified — full implementation reads string descriptor via IOCTL)
        dev.DeviceName[0] = L'\0';
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

UsbPcapReader::UsbPcapReader(const RootHubInfo& hub)
    : m_hub(hub) {}

UsbPcapReader::~UsbPcapReader() {
    StopCapture();
    Close();
}

bool UsbPcapReader::Open(uint32_t snapshotLen) {
    if (m_handle != INVALID_HANDLE_VALUE) return true;

    m_handle = CreateFileW(m_hub.devicePath.c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);

    if (m_handle == INVALID_HANDLE_VALUE) {
        m_lastError = "Cannot open " +
            std::string(m_hub.devicePath.begin(), m_hub.devicePath.end()) +
            " error=" + std::to_string(GetLastError());
        spdlog::error("[usbpcap] {}", m_lastError);
        return false;
    }

    // IOCTL_USBPCAP_SETUP_BUFFER: set snapshot length
    DWORD bufLen = (snapshotLen == 0) ? 65535u : snapshotLen;
    DWORD returned = 0;
    if (!DeviceIoControl(m_handle,
                         IOCTL_USBPCAP_SETUP_BUFFER,
                         &bufLen, sizeof(bufLen),
                         &bufLen, sizeof(bufLen),
                         &returned, nullptr)) {
        m_lastError = "IOCTL_USBPCAP_SETUP_BUFFER failed: " +
                       std::to_string(GetLastError());
        spdlog::error("[usbpcap] {}", m_lastError);
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    spdlog::info("[usbpcap] Opened {} snaplen={}", 
        std::string(m_hub.devicePath.begin(), m_hub.devicePath.end()),
        bufLen);
    return true;
}

void UsbPcapReader::Close() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

bool UsbPcapReader::SetFilter(const std::vector<uint16_t>& deviceAddresses) {
    if (m_handle == INVALID_HANDLE_VALUE) return false;

    UsbPcapAddressFilter filter{};
    if (deviceAddresses.empty()) {
        filter.filterAll = 1;
    } else {
        filter.filterAll = 0;
        for (uint16_t addr : deviceAddresses) {
            if (addr >= 1 && addr <= 127) {
                uint8_t idx = (uint8_t)(addr - 1);
                filter.addresses[idx / 32] |= (1u << (idx % 32));
            }
        }
    }

    DWORD returned = 0;
    if (!DeviceIoControl(m_handle,
                         IOCTL_USBPCAP_SET_FILTER,
                         &filter, sizeof(filter),
                         nullptr, 0,
                         &returned, nullptr)) {
        m_lastError = "IOCTL_USBPCAP_SET_FILTER failed: " +
                       std::to_string(GetLastError());
        spdlog::warn("[usbpcap] {}", m_lastError);
        return false;
    }
    return true;
}

bool UsbPcapReader::StartCapture(EventCallback cb,
                                  std::atomic<uint64_t>& seqCounter) {
    if (m_running.load()) return true;
    if (m_handle == INVALID_HANDLE_VALUE) {
        m_lastError = "Not open";
        return false;
    }

    m_running = true;
    m_thread = std::thread([this, cb, &seqCounter]() {
        ReadLoop(cb, seqCounter);
    });
    return true;
}

void UsbPcapReader::StopCapture() {
    if (!m_running.exchange(false)) return;

    // Cancel any pending I/O on the handle — will unblock ReadFile
    if (m_handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_handle, nullptr);
    }

    if (m_thread.joinable()) m_thread.join();
}

void UsbPcapReader::ReadLoop(EventCallback cb,
                              std::atomic<uint64_t>& seqCounter) {
    spdlog::info("[usbpcap] ReadLoop start: {}",
        std::string(m_hub.devicePath.begin(), m_hub.devicePath.end()));

    PcapStream stream(m_handle);
    if (!stream.ReadGlobalHeader()) {
        spdlog::error("[usbpcap] ReadGlobalHeader: {}", stream.lastError());
        m_running = false;
        return;
    }

    constexpr uint64_t EXPIRE_INTERVAL_US = 1'000'000ULL; // 1s
    uint64_t lastExpire = 0;

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

        if (cb) cb(std::move(evt), std::move(rec.data));
    }

    m_running = false;
    spdlog::info("[usbpcap] ReadLoop end: {}",
        std::string(m_hub.devicePath.begin(), m_hub.devicePath.end()));
}

/* ──────────── UsbPcapMultiReader ──────────── */

bool UsbPcapMultiReader::Open(const BHPLUS_CAPTURE_CONFIG& config) {
    m_config = config;
    m_readers.clear();

    auto hubs = EnumerateRootHubs();
    if (hubs.empty()) {
        m_lastError = "No USBPcap devices found. Is USBPcap installed?";
        spdlog::error("[multi] {}", m_lastError);
        return false;
    }

    bool anyOpen = false;
    for (auto& hub : hubs) {
        // If FilterBus is set, skip other hubs
        if (config.FilterBus != 0 && hub.index != config.FilterBus) continue;
        if (!hub.available) continue;

        auto reader = std::make_unique<UsbPcapReader>(hub);
        uint32_t snapLen = (config.SnapshotLength > 0) ? config.SnapshotLength : 65535u;
        if (!reader->Open(snapLen)) {
            spdlog::warn("[multi] Cannot open hub {}: {}", hub.index, reader->LastError());
            continue;
        }

        // Build address filter list from config
        std::vector<uint16_t> addrs;
        for (size_t i = 0; i < config.FilterDeviceCount && i < BHPLUS_MAX_FILTER_DEVICES; ++i) {
            addrs.push_back(config.FilterDeviceAddresses[i]);
        }
        reader->SetFilter(addrs); // ignore failure — defaults to all

        m_readers.push_back(std::move(reader));
        anyOpen = true;
    }

    if (!anyOpen) {
        m_lastError = "Failed to open any USBPcap device";
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
