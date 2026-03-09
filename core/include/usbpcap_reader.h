#pragma once
/*
 * USBPcapGUI - USBPcap Device Reader
 *
 * Enumerates \\.\USBPcapN devices, configures them via IOCTL,
 * and streams pcap packets on a background thread.
 *
 * One UsbPcapReader instance manages ONE Root Hub handle.
 * UsbPcapMultiReader manages an array of readers.
 */

#include "bhplus_types.h"
#include "pcap_parser.h"
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <optional>

namespace bhplus {

/* ──────────── USBPcap IOCTL Codes ──────────── */

#define USBPCAP_DEVICE_TYPE  0x22  // FILE_DEVICE_UNKNOWN

#define IOCTL_USBPCAP_SETUP_BUFFER   \
    CTL_CODE(USBPCAP_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_USBPCAP_START          \
    CTL_CODE(USBPCAP_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_USBPCAP_STOP           \
    CTL_CODE(USBPCAP_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_USBPCAP_SET_FILTER     \
    CTL_CODE(USBPCAP_DEVICE_TYPE, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_USBPCAP_GET_HUB_SYMLINK \
    CTL_CODE(USBPCAP_DEVICE_TYPE, 0x805, METHOD_BUFFERED, FILE_READ_ACCESS)

/* ──────────── USBPcap Filter Structure ──────────── */

#pragma pack(push, 1)
struct UsbPcapAddressFilter {
    uint32_t addresses[16]; ///< Bit-field: bit N = capture device address N+1
    uint8_t  filterAll;     ///< 1 = capture all devices on hub
};
#pragma pack(pop)

/* ──────────── Root Hub Info ──────────── */

struct RootHubInfo {
    uint32_t     index;          ///< N in \\.\USBPcapN  (1-based)
    std::wstring devicePath;     ///< e.g. L"\\\\.\\USBPcap1"
    std::wstring hubSymLink;     ///< e.g. L"\\\\?\\USB#ROOT_HUB30#..."
    bool         available;      ///< Whether CreateFile succeeded
};

/// Enumerate all present \\.\\USBPcap[N] devices (N=1..16).
std::vector<RootHubInfo> EnumerateRootHubs();

/* ──────────── Event Callback ──────────── */

using EventCallback = std::function<void(BHPLUS_CAPTURE_EVENT evt,
                                         std::vector<uint8_t> data)>;

/* ──────────── UsbPcapReader ──────────── */

/**
 * Manages one \\.\USBPcapN handle.
 *
 * Thread model:
 *   - Open() / Close() called from any thread.
 *   - StartCapture() spawns m_thread (read loop).
 *   - EventCallback invoked from m_thread.
 *   - StopCapture() cancels I/O and joins thread.
 */
class UsbPcapReader {
public:
    explicit UsbPcapReader(const RootHubInfo& hub);
    ~UsbPcapReader();

    UsbPcapReader(const UsbPcapReader&) = delete;
    UsbPcapReader& operator=(const UsbPcapReader&) = delete;

    /// Open device handle and send IOCTL_USBPCAP_SETUP_BUFFER.
    /// @param snapshotLen max bytes per packet (0 → default 65535)
    bool Open(uint32_t snapshotLen = 65535);
    void Close();
    bool IsOpen() const { return m_handle != INVALID_HANDLE_VALUE; }

    /// Configure address filter. Call before StartCapture.
    /// Empty vector → capture all devices on this hub.
    bool SetFilter(const std::vector<uint16_t>& deviceAddresses);

    /// Start background read thread. Calls cb for each decoded event.
    bool StartCapture(EventCallback cb, std::atomic<uint64_t>& seqCounter);
    void StopCapture();
    bool IsCapturing() const { return m_running.load(); }

    const RootHubInfo& HubInfo() const { return m_hub; }
    std::string   LastError() const { return m_lastError; }

private:
    bool SendControlIoctl(DWORD code, const char* name);
    bool SetupBuffer(uint32_t snapshotLen);
    void ReadLoop(EventCallback cb, std::atomic<uint64_t>& seqCounter);

    RootHubInfo  m_hub;
    HANDLE       m_handle      = INVALID_HANDLE_VALUE;
    std::atomic<bool> m_running{ false };
    std::thread  m_thread;
    std::string  m_lastError;
    IrpPairingTable m_irpTable;
};

/* ──────────── UsbPcapMultiReader ──────────── */

/**
 * Manages N UsbPcapReader instances (one per Root Hub).
 *
 * Usage:
 *   UsbPcapMultiReader mr;
 *   mr.Open(config);
 *   mr.StartCapture(callback);
 *   ...
 *   mr.StopCapture();
 */
class UsbPcapMultiReader {
public:
    UsbPcapMultiReader() = default;
    ~UsbPcapMultiReader() { StopCapture(); }

    UsbPcapMultiReader(const UsbPcapMultiReader&) = delete;
    UsbPcapMultiReader& operator=(const UsbPcapMultiReader&) = delete;

    /**
     * Open USBPcap devices according to config.
     * config.FilterBus == 0  → open all hubs
     * config.FilterBus == N  → open only hub N
     */
    bool Open(const BHPLUS_CAPTURE_CONFIG& config);

    void Close();

    bool StartCapture(EventCallback cb);
    void StopCapture();

    bool IsCapturing() const;

    /// How many hub handles are open
    size_t OpenHubCount() const { return m_readers.size(); }

    /// Enumerate USB devices visible to all open hubs
    std::vector<BHPLUS_USB_DEVICE_INFO> EnumerateDevices() const;

    std::string LastError() const { return m_lastError; }

private:
    std::vector<std::unique_ptr<UsbPcapReader>> m_readers;
    std::atomic<uint64_t> m_seqCounter{ 0 };
    std::string m_lastError;
    BHPLUS_CAPTURE_CONFIG m_config{};
};

/* ──────────── Install Check ──────────── */

/// True if \\.\USBPcap1 (or higher) is accessible.
bool IsUsbPcapInstalled();

/// Find the USBPcap installer bundled next to our exe, if present.
/// Returns empty string if not found.
std::wstring FindBundledUsbPcapInstaller();

/// Enumerate USB device info via SetupAPI for a given Root Hub symlink.
std::vector<BHPLUS_USB_DEVICE_INFO> EnumerateUsbDevicesOnHub(
    const std::wstring& hubSymLink,
    uint16_t busIndex);

} // namespace bhplus
