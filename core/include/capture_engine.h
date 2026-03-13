#pragma once
/*
 * USBPcapGUI - Capture Engine
 *
 * Manages USB capture through USBPcap.
 * Does NOT require a custom kernel driver for USB capture.
 *
 * Replaces previous IOCTL-based approach that required bhplus.sys.
 */

#include "bhplus_types.h"
#include "usbpcap_reader.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bhplus {

// EventCallback is imported from usbpcap_reader.h:
//   using EventCallback = std::function<void(BHPLUS_CAPTURE_EVENT, std::vector<uint8_t>)>;

class CaptureEngine {
public:
    CaptureEngine();
    ~CaptureEngine();

    CaptureEngine(const CaptureEngine&) = delete;
    CaptureEngine& operator=(const CaptureEngine&) = delete;

    /// Check that USBPcap is installed and at least one hub is accessible.
    bool IsDriverLoaded() const;

    /// Open USBPcap devices according to config.
    bool OpenDriver(const BHPLUS_CAPTURE_CONFIG& config = {});
    void CloseDriver();

    /// Start capture. cb is invoked on each arrived event (from background thread).
    bool StartCapture(const BHPLUS_CAPTURE_CONFIG& config);
    bool StopCapture();
    bool IsCapturing() const;

    void SetEventCallback(EventCallback callback);

    /// Enumerate connected USB devices across all open hubs.
    /// Uses cached hub info — does NOT open USBPcap device handles.
    std::vector<BHPLUS_USB_DEVICE_INFO> EnumerateDevices();

    /// Enumerate available Root Hubs (does not require OpenDriver).
    /// First call queries the driver; subsequent calls return cached data.
    /// Call RefreshHubs() to force a re-query.
    std::vector<RootHubInfo> EnumerateHubs();

    /// Force re-enumeration of root hubs (e.g. after rescan).
    void RefreshHubs();

    BHPLUS_STATS GetStatistics() const;

    std::string LastError() const { return m_lastError; }

private:
    std::unique_ptr<UsbPcapMultiReader> m_reader;
    EventCallback                       m_callback;
    mutable BHPLUS_STATS                m_stats{};
    std::string                         m_lastError;
    std::vector<RootHubInfo>            m_cachedHubs;  ///< Cached from first enumeration
};

} // namespace bhplus
