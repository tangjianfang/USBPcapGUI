/*
 * USBPcapGUI - Core Service Entry Point
 */

#include "capture_engine.h"
#include "driver_manager.h"
#include "ipc_server.h"
#include "log_init.h"
#include <spdlog/spdlog.h>
#include <iostream>

static std::string Narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

int main(int argc, char* argv[]) {
    InitLogger("bhplus-core");
    spdlog::info("USBPcapGUI Core Service starting...");

    bhplus::CaptureEngine captureEngine;

    // Check if USBPcap is installed
    const bool usbPcapInstalled = bhplus::DriverManager::IsUSBPcapInstalled();
    if (!usbPcapInstalled) {
        spdlog::warn("USBPcap is not installed. USB capture unavailable.");
        spdlog::warn("Install USBPcap from https://github.com/desowin/usbpcap");
    } else {
        spdlog::info("USBPcap detected.");
        if (!bhplus::DriverManager::IsUSBPcapDriverRunning()) {
            if (bhplus::DriverManager::StartUSBPcapDriver()) {
                spdlog::info("USBPcap driver started.");
            } else {
                spdlog::warn("USBPcap driver service is installed but could not be started.");
            }
        }

        const auto interfaceCount = bhplus::DriverManager::GetUSBPcapInterfaceCount();
        if (interfaceCount == 0 && bhplus::DriverManager::HasUSBPcapUpperFilter()) {
            spdlog::warn("USBPcap is installed but no capture interfaces are visible yet.");
            spdlog::warn("A reboot or USB device restart is likely required before live capture will work.");
        }
    }

    // Enumerate available USB Root Hubs
    auto hubs = captureEngine.EnumerateHubs();
    spdlog::info("Found {} USB Root Hub(s)", hubs.size());
    for (const auto& hub : hubs) {
        spdlog::info("  Hub {}: {} (available={})",
            hub.index,
            Narrow(hub.devicePath),
            hub.available);
    }

    // Start IPC server (JSON-RPC over Named Pipe)
    bhplus::IpcServer ipcServer;
    ipcServer.SetCaptureEngine(&captureEngine);

    captureEngine.SetEventCallback([&ipcServer](BHPLUS_CAPTURE_EVENT event,
                                                std::vector<uint8_t> data) {
        ipcServer.BroadcastEvent(event, data.data(), static_cast<uint32_t>(data.size()));
    });

    if (!ipcServer.Start()) {
        spdlog::error("Failed to start IPC server");
        return 1;
    }
    spdlog::info("IPC server listening on \\\\.\\pipe\\bhplus-core");

    // Block until Ctrl+C
    spdlog::info("Press Ctrl+C to stop...");
    while (ipcServer.IsRunning()) {
        Sleep(500);
    }

    spdlog::info("USBPcapGUI Core Service stopped.");
    return 0;
}
