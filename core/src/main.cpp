/*
 * USBPcapGUI - Core Service Entry Point
 */

#include "capture_engine.h"
#include "driver_manager.h"
#include "ipc_server.h"
#include <spdlog/spdlog.h>
#include <iostream>

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("USBPcapGUI Core Service starting...");

    // Check if USBPcap is installed
    if (!bhplus::DriverManager::IsUSBPcapInstalled()) {
        spdlog::warn("USBPcap is not installed. USB capture unavailable.");
        spdlog::warn("Install USBPcap from https://github.com/desowin/usbpcap");
    } else {
        spdlog::info("USBPcap detected.");
    }

    // Enumerate available USB Root Hubs
    auto hubs = bhplus::CaptureEngine::EnumerateHubs();
    spdlog::info("Found {} USB Root Hub(s)", hubs.size());
    for (const auto& hub : hubs) {
        spdlog::info("  Hub {}: {} (available={})",
            hub.index,
            std::string(hub.devicePath.begin(), hub.devicePath.end()),
            hub.available);
    }

    // Start IPC server (JSON-RPC over Named Pipe)
    bhplus::IpcServer ipcServer;
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
