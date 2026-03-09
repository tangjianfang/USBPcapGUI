/*
 * USBPcapGUI - Capture Engine Implementation (USBPcap-backed)
 *
 * All USB capture is done through USBPcap — no custom kernel driver needed.
 */

#include "capture_engine.h"
#include <spdlog/spdlog.h>
#include <windows.h>

namespace bhplus {

CaptureEngine::CaptureEngine() = default;

CaptureEngine::~CaptureEngine() {
    StopCapture();
    CloseDriver();
}


bool CaptureEngine::IsDriverLoaded() const {
    return IsUsbPcapInstalled();
}

bool CaptureEngine::OpenDriver(const BHPLUS_CAPTURE_CONFIG& config) {
    if (m_reader && m_reader->OpenHubCount() > 0) return true;

    m_reader = std::make_unique<UsbPcapMultiReader>();
    if (!m_reader->Open(config)) {
        m_lastError = m_reader->LastError();
        spdlog::error("[capture] OpenDriver: {}", m_lastError);
        m_reader.reset();
        return false;
    }
    return true;
}

void CaptureEngine::CloseDriver() {
    if (m_reader) {
        m_reader->Close();
        m_reader.reset();
    }
}

bool CaptureEngine::StartCapture(const BHPLUS_CAPTURE_CONFIG& config) {
    // (Re-)open with the new config
    CloseDriver();
    if (!OpenDriver(config)) return false;

    if (m_reader->IsCapturing()) {
        spdlog::warn("[capture] Already capturing");
        return false;
    }

    m_stats = {};

    bool ok = m_reader->StartCapture([this](BHPLUS_CAPTURE_EVENT evt,
                                             std::vector<uint8_t> data) {
        // Update stats
        m_stats.TotalEventsCaptured++;
        m_stats.TotalBytesCaptured += evt.DataLength;

        if (m_callback) m_callback(std::move(evt), std::move(data));
    });

    if (!ok) {
        m_lastError = m_reader->LastError();
        spdlog::error("[capture] StartCapture: {}", m_lastError);
        return false;
    }

    spdlog::info("[capture] Capture started ({} hub(s))", m_reader->OpenHubCount());
    return true;
}

bool CaptureEngine::StopCapture() {
    if (!m_reader) return false;
    m_reader->StopCapture();
    spdlog::info("[capture] Capture stopped");
    return true;
}

bool CaptureEngine::IsCapturing() const {
    return m_reader && m_reader->IsCapturing();
}

void CaptureEngine::SetEventCallback(EventCallback callback) {
    m_callback = std::move(callback);
}

std::vector<BHPLUS_USB_DEVICE_INFO> CaptureEngine::EnumerateDevices() {
    if (!m_reader) {
        // Open temporarily with default config just for enumeration
        BHPLUS_CAPTURE_CONFIG cfg{};
        OpenDriver(cfg);
    }
    if (!m_reader) return {};
    return m_reader->EnumerateDevices();
}

std::vector<RootHubInfo> CaptureEngine::EnumerateHubs() {
    return EnumerateRootHubs();
}

BHPLUS_STATS CaptureEngine::GetStatistics() const {
    BHPLUS_STATS stats = m_stats;
    return stats;
}

} // namespace bhplus
