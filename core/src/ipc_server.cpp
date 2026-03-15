/*
 * USBPcapGUI - JSON-RPC IPC Server
 * Named Pipe server for Node.js GUI backend communication.
 * Protocol: newline-delimited JSON-RPC 2.0 over \\.\pipe\bhplus-core
 */
#define NOMINMAX   // prevent windows.h min/max macros

#include "ipc_server.h"
#include "capture_config_parser.h"
#include "capture_engine.h"
#include "pcap_parser.h"
#include "parser_interface.h"
#include "driver_manager.h"
#include "usb_actions.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <sstream>
#include <algorithm>
#include <iomanip>

using json = nlohmann::json;

namespace bhplus {

// ──────────── Helpers ────────────

static std::string WideToUtf8(const wchar_t* wstr, size_t maxLen = 0) {
    if (!wstr) return "";
    int len = maxLen > 0
        ? static_cast<int>(wcsnlen(wstr, maxLen))
        : static_cast<int>(wcslen(wstr));
    if (len == 0) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr, len, nullptr, 0, nullptr, nullptr);
    std::string out(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, len, out.data(), sz, nullptr, nullptr);
    return out;
}

static std::string BytesToHex(const uint8_t* data, uint32_t len) {
    if (!data || len == 0) return "";
    std::ostringstream oss;
    for (uint32_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return oss.str();
}

static const char* EventTypeName(BHPLUS_EVENT_TYPE type) {
    switch (type) {
        case BHPLUS_EVENT_URB_DOWN:   return "URB_DOWN";
        case BHPLUS_EVENT_URB_UP:     return "URB_UP";
        case BHPLUS_EVENT_NVME_ADMIN: return "NVMe_Admin";
        case BHPLUS_EVENT_NVME_IO:    return "NVMe_IO";
        case BHPLUS_EVENT_SCSI_CDB:   return "SCSI_CDB";
        case BHPLUS_EVENT_ATA_CMD:    return "ATA_CMD";
        case BHPLUS_EVENT_SERIAL_TX:  return "Serial_TX";
        case BHPLUS_EVENT_SERIAL_RX:  return "Serial_RX";
        default:                      return "Unknown";
    }
}

static const char* BusTypeName(uint32_t busType) {
    switch (static_cast<BHPLUS_BUS_TYPE>(busType)) {
        case BHPLUS_BUS_USB:       return "USB";
        case BHPLUS_BUS_NVME:      return "NVMe";
        case BHPLUS_BUS_SATA:      return "SATA";
        case BHPLUS_BUS_SCSI:      return "SCSI";
        case BHPLUS_BUS_SERIAL:    return "Serial";
        case BHPLUS_BUS_BLUETOOTH: return "Bluetooth";
        case BHPLUS_BUS_FIREWIRE:  return "FireWire";
        default:                   return "Unknown";
    }
}

// Maps bDeviceClass to a human-readable protocol label.
// See USB Device Class Codes: https://www.usb.org/defined-class-codes
static const char* UsbDeviceClassLabel(uint8_t deviceClass) {
    switch (deviceClass) {
        case 0x01: return "Audio";
        case 0x02: return "CDC";
        case 0x03: return "HID";
        case 0x05: return "PID";
        case 0x06: return "Image";
        case 0x07: return "Printer";
        case 0x08: return "MSD";
        case 0x09: return "HUB";
        case 0x0A: return "CDC-Data";
        case 0x0B: return "CCID";
        case 0x0D: return "Content-Sec";
        case 0x0E: return "Video";
        case 0x0F: return "Personal-Health";
        case 0x10: return "AV";
        case 0x11: return "Billboard";
        case 0x12: return "USB-C-Bridge";
        case 0xDC: return "Diagnostic";
        case 0xE0: return "Wireless";
        case 0xEF: return "Misc";
        case 0xFE: return "App-Specific";
        case 0xFF: return "Vendor";
        default:   return nullptr;  // unknown / use-class-info-in-interface
    }
}

// ──────────── Constructor / Destructor ────────────

IpcServer::IpcServer() {
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_flushEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset
}

IpcServer::~IpcServer() {
    Stop();
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    if (m_flushEvent) {
        CloseHandle(m_flushEvent);
        m_flushEvent = nullptr;
    }
}

void IpcServer::SetCaptureEngine(CaptureEngine* engine) {
    m_engine = engine;
}

// ──────────── Device Cache ────────────

void IpcServer::UpdateDeviceCache() {
    if (!m_engine) return;
    auto devices = m_engine->EnumerateDevices();
    std::lock_guard<std::mutex> lock(m_deviceCacheMutex);
    m_deviceCache.clear();
    for (const auto& dev : devices) {
        uint32_t key = (static_cast<uint32_t>(dev.Bus) << 16) | dev.DeviceAddress;
        m_deviceCache[key] = dev;
    }
    spdlog::debug("Device cache updated: {} devices", m_deviceCache.size());
}

// ──────────── Start / Stop ────────────

bool IpcServer::Start() {
    if (m_running) return true;

    ResetEvent(m_stopEvent);
    m_running = true;

    m_acceptThread = std::thread([this]() { AcceptThread(); });
    m_flushThread = std::thread([this]() { FlushThread(); });

    spdlog::info("IPC server started on pipe: bhplus-core");
    return true;
}

void IpcServer::Stop() {
    if (!m_running) return;
    m_running = false;
    SetEvent(m_stopEvent);
    if (m_flushEvent) SetEvent(m_flushEvent); // wake flush thread

    // Flush remaining events
    FlushEventQueue();

    // Disconnect all clients
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& ctx : m_clients) {
            DisconnectClient(ctx.get());
        }
    }

    if (m_flushThread.joinable()) m_flushThread.join();
    if (m_acceptThread.joinable()) m_acceptThread.join();

    // Join client threads
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& ctx : m_clients) {
            if (ctx->readThread.joinable()) ctx->readThread.join();
        }
        m_clients.clear();
    }

    spdlog::info("IPC server stopped");
}

bool IpcServer::IsRunning() const {
    return m_running;
}

// ──────────── Accept Thread ────────────

void IpcServer::AcceptThread() {
    spdlog::debug("IPC accept thread started");

    while (m_running) {
        // Create a new pipe instance (overlapped)
        HANDLE hPipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            MAX_CLIENTS,
            PIPE_BUFFER_SIZE,
            PIPE_BUFFER_SIZE,
            0,
            nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            spdlog::error("CreateNamedPipe failed: {}", GetLastError());
            Sleep(1000);
            continue;
        }

        // Wait for a client to connect (overlapped)
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        BOOL connected = ConnectNamedPipe(hPipe, &ov);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Wait for connection or stop
                HANDLE waitHandles[] = { ov.hEvent, m_stopEvent };
                DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitResult == WAIT_OBJECT_0 + 1) {
                    // Stop requested
                    CancelIo(hPipe);
                    CloseHandle(ov.hEvent);
                    CloseHandle(hPipe);
                    break;
                }
                // Check if connection succeeded
                DWORD bytesTransferred = 0;
                if (!GetOverlappedResult(hPipe, &ov, &bytesTransferred, FALSE)) {
                    CloseHandle(ov.hEvent);
                    CloseHandle(hPipe);
                    continue;
                }
            } else if (err != ERROR_PIPE_CONNECTED) {
                spdlog::error("ConnectNamedPipe failed: {}", err);
                CloseHandle(ov.hEvent);
                CloseHandle(hPipe);
                continue;
            }
        }

        CloseHandle(ov.hEvent);

        // New client connected
        spdlog::info("IPC client connected");

        auto ctx = std::make_unique<ClientContext>();
        ctx->pipeHandle = hPipe;
        ctx->connected = true;

        ClientContext* rawCtx = ctx.get();
        ctx->readThread = std::thread([this, rawCtx]() { ClientReadLoop(rawCtx); });

        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            m_clients.push_back(std::move(ctx));
        }
    }

    spdlog::debug("IPC accept thread exiting");
}

// ──────────── Client Read Loop ────────────

void IpcServer::ClientReadLoop(ClientContext* ctx) {
    std::string buffer;
    char readBuf[4096];

    while (m_running && ctx->connected) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        DWORD bytesRead = 0;
        BOOL readOk = ReadFile(ctx->pipeHandle, readBuf, sizeof(readBuf), &bytesRead, &ov);

        if (!readOk) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waitHandles[] = { ov.hEvent, m_stopEvent };
                DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, 5000);
                if (waitResult == WAIT_OBJECT_0 + 1 || waitResult == WAIT_TIMEOUT) {
                    CancelIo(ctx->pipeHandle);
                    CloseHandle(ov.hEvent);
                    if (waitResult == WAIT_OBJECT_0 + 1) break;
                    continue;
                }
                if (!GetOverlappedResult(ctx->pipeHandle, &ov, &bytesRead, FALSE)) {
                    CloseHandle(ov.hEvent);
                    break;  // Pipe broken
                }
            } else {
                CloseHandle(ov.hEvent);
                break;  // Pipe broken
            }
        }

        CloseHandle(ov.hEvent);

        if (bytesRead == 0) continue;

        buffer.append(readBuf, bytesRead);

        // Process newline-delimited JSON messages
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (!line.empty()) {
                HandleRequest(ctx, line);
            }
        }
    }

    DisconnectClient(ctx);
    spdlog::info("IPC client disconnected");
}

// ──────────── Request Handling ────────────

void IpcServer::HandleRequest(ClientContext* ctx, const std::string& requestJson) {
    try {
        auto req = json::parse(requestJson);

        // JSON-RPC 2.0: { "jsonrpc": "2.0", "method": "...", "params": {...}, "id": ... }
        std::string method = req.value("method", "");
        std::string paramsStr = req.contains("params") ? req["params"].dump() : "{}";
        auto id = req.value("id", json(nullptr));

        std::string result;

        if (method == "capture.start") {
            result = HandleCaptureStart(paramsStr);
        } else if (method == "capture.stop") {
            result = HandleCaptureStop(paramsStr);
        } else if (method == "capture.status" || method == "stats.get") {
            result = HandleCaptureStatus(paramsStr);
        } else if (method == "devices.list" || method == "devices.enumerate") {
            result = HandleDevicesList(paramsStr);
        } else if (method == "hubs.list" || method == "usbpcap.instances") {
            result = HandleHubsList(paramsStr);
        } else if (method == "usbpcap.status") {
            result = HandleUsbPcapStatus(paramsStr);
        } else if (method == "usbpcap.install") {
            result = HandleUsbPcapInstall(paramsStr);
        } else if (method == "usbpcap.rescan") {
            result = HandleUsbPcapRescan(paramsStr);
        } else if (method == "events.query") {
            result = HandleEventsQuery(paramsStr);
        } else if (method == "device.reset") {
            result = HandleDeviceReset(paramsStr);
        } else if (method == "command.send") {
            result = HandleCommandSend(paramsStr);
        } else {
            // Unknown method
            json resp;
            resp["jsonrpc"] = "2.0";
            resp["id"] = id;
            resp["error"] = { {"code", -32601}, {"message", "Method not found: " + method} };
            SendResponse(ctx, resp.dump() + "\n");
            return;
        }

        // Wrap in JSON-RPC response
        json resp;
        resp["jsonrpc"] = "2.0";
        resp["id"] = id;
        resp["result"] = json::parse(result);
        SendResponse(ctx, resp.dump() + "\n");

    } catch (const std::exception& ex) {
        spdlog::error("IPC request parse error: {}", ex.what());
        json resp;
        resp["jsonrpc"] = "2.0";
        resp["id"] = nullptr;
        resp["error"] = { {"code", -32700}, {"message", "Parse error"} };
        SendResponse(ctx, resp.dump() + "\n");
    }
}

void IpcServer::SendResponse(ClientContext* ctx, const std::string& responseJson) {
    if (!ctx || !ctx->connected) return;

    std::lock_guard<std::mutex> lock(ctx->writeMutex);
    DWORD written = 0;
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    BOOL ok = WriteFile(ctx->pipeHandle, responseJson.data(),
                        static_cast<DWORD>(responseJson.size()), &written, &ov);

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD waitResult = WaitForSingleObject(ov.hEvent, 5000);
            if (waitResult == WAIT_OBJECT_0) {
                GetOverlappedResult(ctx->pipeHandle, &ov, &written, FALSE);
            } else {
                // Timeout or error — cancel the pending I/O to avoid
                // writing to a destroyed OVERLAPPED after we return.
                CancelIoEx(ctx->pipeHandle, &ov);
                // Wait briefly for cancellation to complete
                WaitForSingleObject(ov.hEvent, 500);
                spdlog::warn("[ipc] Pipe write timed out ({} bytes)", responseJson.size());
            }
        } else {
            spdlog::warn("[ipc] WriteFile failed: error {}", err);
        }
    }
    CloseHandle(ov.hEvent);
}

void IpcServer::SendToAll(const std::string& jsonMsg) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    for (auto& ctx : m_clients) {
        if (ctx->connected) {
            SendResponse(ctx.get(), jsonMsg);
        }
    }
}

void IpcServer::DisconnectClient(ClientContext* ctx) {
    if (!ctx) return;
    ctx->connected = false;
    if (ctx->pipeHandle != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(ctx->pipeHandle);
        CloseHandle(ctx->pipeHandle);
        ctx->pipeHandle = INVALID_HANDLE_VALUE;
    }
}

// ──────────── Broadcast ────────────

void IpcServer::BroadcastEvent(const BHPLUS_CAPTURE_EVENT& event, const uint8_t* data, uint32_t dataLen) {
    if (!m_running) return;

    PendingEvent pe;
    pe.event = event;
    if (data && dataLen > 0)
        pe.data.assign(data, data + dataLen);

    bool shouldFlush = false;
    {
        std::lock_guard<std::mutex> lock(m_eventQueueMutex);
        m_eventQueue.push_back(std::move(pe));
        shouldFlush = (m_eventQueue.size() >= BATCH_MAX_EVENTS);
    }

    // Signal flush thread: either batch is full or new data arrived
    if (m_flushEvent) SetEvent(m_flushEvent);

    // If batch is full, flush immediately (don't wait for timer)
    if (shouldFlush) FlushEventQueue();
}

void IpcServer::FlushThread() {
    spdlog::debug("IPC flush thread started");
    while (m_running) {
        // Wait for data or stop, with periodic timeout for flush interval
        HANDLE handles[] = { m_flushEvent, m_stopEvent };
        WaitForMultipleObjects(2, handles, FALSE, BATCH_FLUSH_MS);

        if (!m_running) break;
        FlushEventQueue();
    }
    spdlog::debug("IPC flush thread exiting");
}

void IpcServer::FlushEventQueue() {
    std::vector<PendingEvent> batch;
    {
        std::lock_guard<std::mutex> lock(m_eventQueueMutex);
        if (m_eventQueue.empty()) return;
        batch.swap(m_eventQueue);
        m_eventQueue.reserve(128);
    }

    // Serialize all events into a single JSON-RPC notification
    json eventsArray = json::array();
    for (const auto& pe : batch) {
        eventsArray.push_back(json::parse(
            EventToJson(pe.event, pe.data.data(),
                        static_cast<uint32_t>(pe.data.size()))));
    }

    json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "capture.events";
    notification["params"] = std::move(eventsArray);

    std::string msg = notification.dump() + "\n";

    spdlog::debug("[ipc] Flushing {} events ({} bytes)", batch.size(), msg.size());
    SendToAll(msg);
}

void IpcServer::BroadcastNotification(const std::string& method, const std::string& paramsJson) {
    if (!m_running) return;

    json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = method;
    notification["params"] = json::parse(paramsJson);

    SendToAll(notification.dump() + "\n");
}

// ──────────── Method Handlers ────────────

std::string IpcServer::HandleCaptureStart(const std::string& paramsJson) {
    if (!m_engine) return R"({"ok": false, "error": "No capture engine"})";

    BHPLUS_CAPTURE_CONFIG config = ParseCaptureStartParams(paramsJson);

    bool ok = m_engine->StartCapture(config);

    if (ok) {
        BroadcastNotification("capture.started", "{}");
    }

    json result;
    result["ok"] = ok;
    if (!ok) {
        const auto& err = m_engine->LastError();
        result["error"] = err.empty() ? "Failed to start capture" : err;
        spdlog::warn("[ipc] capture.start failed: {}", result["error"].get<std::string>());
    }
    return result.dump();
}

std::string IpcServer::HandleCaptureStop(const std::string& /*paramsJson*/) {
    if (!m_engine) return R"({"ok": false, "error": "No capture engine"})";

    bool ok = m_engine->StopCapture();

    if (ok) {
        BroadcastNotification("capture.stopped", "{}");
    }

    json result;
    result["ok"] = ok;
    return result.dump();
}

std::string IpcServer::HandleCaptureStatus(const std::string& /*paramsJson*/) {
    json result;
    if (m_engine) {
        result["capturing"] = m_engine->IsCapturing();
        result["driverLoaded"] = m_engine->IsDriverLoaded();
        result["stats"] = json::parse(StatsToJson(m_engine->GetStatistics()));
    } else {
        result["capturing"] = false;
        result["driverLoaded"] = false;
    }
    return result.dump();
}

std::string IpcServer::HandleDevicesList(const std::string& /*paramsJson*/) {
    json result = json::array();

    if (m_engine) {
        auto devices = m_engine->EnumerateDevices();
        // Update cache so subsequent capture events get enriched with VID/PID/name
        {
            std::lock_guard<std::mutex> lock(m_deviceCacheMutex);
            m_deviceCache.clear();
            for (const auto& dev : devices) {
                uint32_t key = (static_cast<uint32_t>(dev.Bus) << 16) | dev.DeviceAddress;
                m_deviceCache[key] = dev;
            }
        }
        for (const auto& dev : devices) {
            result.push_back(json::parse(DeviceInfoToJson(dev)));
        }
    } else {
        // Fallback: enumerate directly from root hubs before capture starts
        const auto hubs = EnumerateRootHubs();
        std::lock_guard<std::mutex> lock(m_deviceCacheMutex);
        m_deviceCache.clear();
        for (const auto& hub : hubs) {
            auto devices = EnumerateUsbDevicesOnHub(hub.hubSymLink, hub.index);
            for (const auto& dev : devices) {
                uint32_t key = (static_cast<uint32_t>(dev.Bus) << 16) | dev.DeviceAddress;
                m_deviceCache[key] = dev;
                result.push_back(json::parse(DeviceInfoToJson(dev)));
            }
        }
    }

    return result.dump();
}

std::string IpcServer::HandleHubsList(const std::string& /*paramsJson*/) {
    json result = json::array();

    const auto hubs = m_engine ? m_engine->EnumerateHubs()
                               : EnumerateRootHubs();
    for (const auto& hub : hubs) {
        json item;
        item["index"] = hub.index;
        item["devicePath"] = WideToUtf8(hub.devicePath.c_str());
        item["hubSymLink"] = WideToUtf8(hub.hubSymLink.c_str());
        item["available"] = hub.available;
        result.push_back(std::move(item));
    }

    return result.dump();
}

std::string IpcServer::HandleUsbPcapStatus(const std::string& /*paramsJson*/) {
    json result;
    const auto installerPath = DriverManager::GetUSBPcapInstallerPath();
    const auto interfaceCount = DriverManager::GetUSBPcapInterfaceCount();
    const bool installed = DriverManager::IsUSBPcapInstalled();
    const bool serviceInstalled = DriverManager::IsUSBPcapServiceInstalled();
    const bool serviceRunning = DriverManager::IsUSBPcapDriverRunning();
    const bool upperFilterRegistered = DriverManager::HasUSBPcapUpperFilter();

    // Check if running as Administrator
    bool isAdmin = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elev{};
        DWORD sz = sizeof(elev);
        if (GetTokenInformation(token, TokenElevation, &elev, sz, &sz))
            isAdmin = elev.TokenIsElevated != 0;
        CloseHandle(token);
    }

    result["installed"] = installed;
    result["installerFound"] = !installerPath.empty();
    result["installerPath"] = installerPath.empty() ? std::string() : WideToUtf8(installerPath.c_str());
    result["interfacesAvailable"] = interfaceCount;
    result["driverServiceInstalled"] = serviceInstalled;
    result["driverServiceRunning"] = serviceRunning;
    result["upperFilterRegistered"] = upperFilterRegistered;
    result["isAdmin"] = isAdmin;

    json hubs = json::array();
    bool anyDenied = false;
    for (const auto& hub : m_engine->EnumerateHubs()) {
        hubs.push_back({
            {"index", hub.index},
            {"devicePath", WideToUtf8(hub.devicePath.c_str())},
            {"hubSymLink", WideToUtf8(hub.hubSymLink.c_str())},
            {"available", hub.available}
        });
        if (!hub.available) anyDenied = true;
    }
    result["restartRecommended"] = installed && interfaceCount == 0 && upperFilterRegistered;
    result["virtualUsbDetected"] = DriverManager::DetectVirtualUsbEnvironment();
    // accessDenied = hubs exist but none openable + not running as admin
    result["accessDenied"] = installed && anyDenied && !hubs.empty() && !isAdmin;
    result["hubs"] = std::move(hubs);
    return result.dump();
}

std::string IpcServer::HandleUsbPcapInstall(const std::string& /*paramsJson*/) {
    json result;
    result["ok"] = DriverManager::LaunchUSBPcapInstaller();
    result["installed"] = DriverManager::IsUSBPcapInstalled();
    result["installerFound"] = !DriverManager::GetUSBPcapInstallerPath().empty();
    if (!result["ok"].get<bool>()) {
        result["message"] = "Unable to launch USBPcap installer";
    }
    return result.dump();
}

std::string IpcServer::HandleUsbPcapRescan(const std::string& /*paramsJson*/) {
    const bool ok = DriverManager::RescanUsbDevices();
    // Wait briefly for PnP to react, then re-read status
    if (ok) Sleep(1500);
    // Force re-enumeration of hubs after rescan, then refresh device cache
    if (m_engine) m_engine->RefreshHubs();
    UpdateDeviceCache();
    json result;
    result["ok"] = ok;
    result["interfacesAvailable"] = DriverManager::GetUSBPcapInterfaceCount();
    result["driverServiceRunning"] = DriverManager::IsUSBPcapDriverRunning();
    result["virtualUsbDetected"] = DriverManager::DetectVirtualUsbEnvironment();
    json hubs = json::array();
    const auto hubList = m_engine ? m_engine->EnumerateHubs()
                                  : EnumerateRootHubs();
    for (const auto& hub : hubList) {
        hubs.push_back({
            {"index", hub.index},
            {"devicePath", WideToUtf8(hub.devicePath.c_str())},
            {"hubSymLink", WideToUtf8(hub.hubSymLink.c_str())},
            {"available", hub.available}
        });
    }
    result["hubs"] = std::move(hubs);
    return result.dump();
}

std::string IpcServer::HandleEventsQuery(const std::string& /*paramsJson*/) {
    // Events are streamed in real-time; this returns an empty result
    // The Node.js layer maintains its own event buffer
    return "[]";
}

std::string IpcServer::HandleDeviceReset(const std::string& paramsJson) {
    const auto params = json::parse(paramsJson.empty() ? "{}" : paramsJson);
    const uint32_t deviceId = params.value("deviceId", 0u);
    if (deviceId == 0) {
        return R"({"ok": false, "message": "deviceId is required"})";
    }

    const auto result = ResetUsbDevice(deviceId);
    json resp;
    resp["ok"] = result.ok;
    resp["deviceId"] = deviceId;
    resp["message"] = result.message;
    resp["bytesTransferred"] = result.bytesTransferred;
    return resp.dump();
}

std::string IpcServer::HandleCommandSend(const std::string& paramsJson) {
    const auto params = json::parse(paramsJson.empty() ? "{}" : paramsJson);
    const uint32_t deviceId = params.value("deviceId", 0u);
    if (deviceId == 0) {
        return R"({"ok": false, "message": "deviceId is required"})";
    }

    UsbControlTransferSetup setup;
    setup.requestType = static_cast<uint8_t>(params.value("requestType", 0u) & 0xffu);
    setup.request = static_cast<uint8_t>(params.value("request", 0u) & 0xffu);
    setup.value = static_cast<uint16_t>(params.value("value", 0u) & 0xffffu);
    setup.index = static_cast<uint16_t>(params.value("index", 0u) & 0xffffu);
    setup.length = static_cast<uint16_t>(params.value("length", 0u) & 0xffffu);
    setup.timeoutMs = params.value("timeoutMs", 1000u);

    std::vector<uint8_t> payload;
    if (params.contains("payloadHex") && params["payloadHex"].is_string()) {
        std::string error;
        if (!DecodeHexString(params["payloadHex"].get<std::string>(), payload, &error)) {
            json resp;
            resp["ok"] = false;
            resp["deviceId"] = deviceId;
            resp["message"] = error;
            return resp.dump();
        }
    }

    const auto result = SendUsbControlTransfer(deviceId, setup, payload);
    json resp;
    resp["ok"] = result.ok;
    resp["deviceId"] = deviceId;
    resp["message"] = result.message;
    resp["bytesTransferred"] = result.bytesTransferred;
    resp["dataHex"] = EncodeHexString(result.data.data(), result.data.size());
    return resp.dump();
}

// ──────────── JSON Serialization ────────────

std::string IpcServer::EventToJson(const BHPLUS_CAPTURE_EVENT& event,
                                    const uint8_t* data, uint32_t dataLen) {
    json j;

    const auto decoded = ParserRegistry::Instance().Decode(event, data, dataLen);
    const std::string defaultCommand = (event.TransferType == BHPLUS_USB_TRANSFER_CONTROL &&
        event.Detail.Control.Stage == BHPLUS_USB_CONTROL_STAGE_SETUP)
        ? UsbStandardRequestName(event.Detail.Control.SetupPacket[1])
        : UrbFunctionName(event.UrbFunction);

    auto phaseName = [&event]() -> std::string {
        if (event.TransferType == BHPLUS_USB_TRANSFER_CONTROL) {
            switch (event.Detail.Control.Stage) {
                case BHPLUS_USB_CONTROL_STAGE_SETUP:    return "SETUP";
                case BHPLUS_USB_CONTROL_STAGE_DATA:     return "DATA";
                case BHPLUS_USB_CONTROL_STAGE_STATUS:   return "STATUS";
                case BHPLUS_USB_CONTROL_STAGE_COMPLETE: return "COMPLETE";
                default:                                return "CONTROL";
            }
        }
        return (event.Direction == BHPLUS_DIR_DOWN) ? "REQUEST" : "COMPLETE";
    };

    const auto statusText = (event.Status == 0)
        ? std::string("OK")
        : fmt::format("0x{:08X}", event.Status);

    // ── Common fields ──────────────────────────────────────────────────────────
    j["seq"]       = event.SequenceNumber;
    j["timestamp"] = event.Timestamp;
    j["type"]      = EventTypeName(event.EventType);
    j["status"]    = statusText;
    j["statusCode"] = event.Status;
    j["duration"]  = event.Duration;
    j["dataLength"]= event.DataLength;
    j["direction"] = (event.Direction == BHPLUS_DIR_DOWN) ? ">>>" : "<<<";
    j["phase"]     = phaseName();
    j["deviceId"]  = (static_cast<uint32_t>(event.Bus) << 16) | event.Device;

    // ── USB identity ────────────────────────────────────────────────────────
    j["bus"]           = event.Bus;
    j["deviceAddress"] = event.Device;
    j["endpoint"]      = event.Endpoint;
    j["transferType"]  = UsbTransferTypeName(event.TransferType);
    j["urbFunction"]   = UrbFunctionName(event.UrbFunction);
    j["irpId"]         = event.IrpId;

    // Enrich with VID/PID/class/name from device cache (populated by devices.list)
    std::string deviceLabel = fmt::format("Bus{}/Dev{}", event.Bus, event.Device);
    std::string protocolLabel = (decoded.protocol.empty() || decoded.protocol == "Unknown") ? "USB" : decoded.protocol;
    {
        std::lock_guard<std::mutex> cacheLock(m_deviceCacheMutex);
        uint32_t key = (static_cast<uint32_t>(event.Bus) << 16) | event.Device;
        auto it = m_deviceCache.find(key);
        if (it != m_deviceCache.end()) {
            const auto& dev = it->second;
            j["vid"]      = dev.VendorId;
            j["pid"]      = dev.ProductId;
            char vidStr[8], pidStr[8];
            snprintf(vidStr, sizeof(vidStr), "%04X", dev.VendorId);
            snprintf(pidStr, sizeof(pidStr), "%04X", dev.ProductId);
            j["vidHex"]   = vidStr;
            j["pidHex"]   = pidStr;
            j["class"]    = dev.DeviceClass;
            j["subClass"] = dev.DeviceSubClass;

            std::string name = WideToUtf8(dev.DeviceName, BHPLUS_MAX_DEVICE_NAME);
            if (!name.empty()) {
                j["deviceName"] = name;
                deviceLabel = fmt::format("{} [{:s}:{:s}]", name, vidStr, pidStr);
            } else if (dev.VendorId || dev.ProductId) {
                deviceLabel = fmt::format("{:s}:{:s}", vidStr, pidStr);
            }

            // Resolve protocol from device class if it's still generic "USB"
            if (protocolLabel == "USB" && dev.DeviceClass != 0) {
                const char* classLabel = UsbDeviceClassLabel(dev.DeviceClass);
                if (classLabel) protocolLabel = classLabel;
            }
        }
    }
    j["device"]   = deviceLabel;
    j["protocol"] = protocolLabel;
    j["command"]      = decoded.commandName.empty() ? defaultCommand : decoded.commandName;
    j["summary"]      = decoded.summary.empty()
        ? fmt::format("{} {}", UsbTransferTypeName(event.TransferType), defaultCommand)
        : decoded.summary;

    // ── Transfer-type detail ─────────────────────────────────────────────────
    json details;
    switch (event.TransferType) {
        case BHPLUS_USB_TRANSFER_CONTROL:
            details["stage"] = event.Detail.Control.Stage;
            if (event.Detail.Control.Stage == BHPLUS_USB_CONTROL_STAGE_SETUP) {
                details["setupPacket"] = BytesToHex(
                    event.Detail.Control.SetupPacket, 8);
            }
            break;

        case BHPLUS_USB_TRANSFER_ISOCHRONOUS:
            details["startFrame"]   = event.Detail.Isoch.StartFrame;
            details["numPackets"]   = event.Detail.Isoch.NumberOfPackets;
            details["errorCount"]   = event.Detail.Isoch.ErrorCount;
            break;

        case BHPLUS_USB_TRANSFER_BULK:
        case BHPLUS_USB_TRANSFER_INTERRUPT:
        default:
            break; // no extra fields
    }

    // Phase-3 storage / serial details (populated when Source == DRIVER)
    if (event.EventType == BHPLUS_EVENT_NVME_ADMIN ||
        event.EventType == BHPLUS_EVENT_NVME_IO) {
        j["protocol"]           = "NVMe";
        details["opcode"]       = event.Detail.Nvme.Opcode;
        details["nsid"]         = event.Detail.Nvme.NSID;
    } else if (event.EventType == BHPLUS_EVENT_SCSI_CDB) {
        j["protocol"]           = "SCSI";
        details["cdb"]          = BytesToHex(event.Detail.Scsi.Cdb,
                                             event.Detail.Scsi.CdbLength);
        details["cdbLength"]    = event.Detail.Scsi.CdbLength;
        details["scsiStatus"]   = event.Detail.Scsi.ScsiStatus;
    } else if (event.EventType == BHPLUS_EVENT_ATA_CMD) {
        j["protocol"]           = "ATA";
        details["command"]      = event.Detail.Ata.Command;
        details["lba"]          = event.Detail.Ata.Lba;
        details["sectorCount"]  = event.Detail.Ata.SectorCount;
    } else if (event.EventType == BHPLUS_EVENT_SERIAL_TX ||
               event.EventType == BHPLUS_EVENT_SERIAL_RX) {
        j["protocol"]           = "Serial";
        details["baudRate"]     = event.Detail.Serial.BaudRate;
        details["dataBits"]     = event.Detail.Serial.DataBits;
    }

    if (!decoded.fields.empty()) {
        json decodedFields = json::array();
        for (const auto& field : decoded.fields) {
            decodedFields.push_back({
                {"name", field.name},
                {"value", field.value},
                {"description", field.description},
                {"offset", field.offset},
                {"length", field.length}
            });
        }
        j["decodedFields"] = std::move(decodedFields);
    }

    if (!details.empty()) j["details"] = details;

    // ── Data payload ──────────────────────────────────────────────────────────
    if (data && dataLen > 0) {
        j["data"] = BytesToHex(data, std::min(dataLen, 4096u));
    }

    return j.dump();
}

std::string IpcServer::DeviceInfoToJson(const BHPLUS_USB_DEVICE_INFO& device) {
    json j;
    j["id"]          = (static_cast<uint32_t>(device.Bus) << 16) | device.DeviceAddress;
    j["busType"]     = "USB";
    j["bus"]         = device.Bus;
    j["device"]      = device.DeviceAddress;
    j["vid"]         = device.VendorId;
    j["pid"]         = device.ProductId;
    j["class"]       = device.DeviceClass;
    j["subClass"]    = device.DeviceSubClass;
    j["protocol"]    = device.DeviceProtocol;
    j["speed"]       = device.Speed;
    j["isHub"]       = device.IsHub;
    j["name"]        = WideToUtf8(device.DeviceName, BHPLUS_MAX_DEVICE_NAME);
    j["serial"]      = WideToUtf8(device.SerialNumber, 64);
    // vidHex / pidHex for display convenience
    char vidStr[8], pidStr[8];
    snprintf(vidStr, sizeof(vidStr), "%04X", device.VendorId);
    snprintf(pidStr, sizeof(pidStr), "%04X", device.ProductId);
    j["vidHex"]      = vidStr;
    j["pidHex"]      = pidStr;
    return j.dump();
}

std::string IpcServer::StatsToJson(const BHPLUS_STATS& stats) {
    json j;
    j["totalEvents"]      = stats.TotalEventsCaptured;
    j["totalBytes"]       = stats.TotalBytesCaptured;
    j["eventsDropped"]    = stats.EventsDropped;
    j["uptimeMs"]         = stats.UptimeMs;
    j["activeDeviceCount"]= stats.ActiveDeviceCount;
    j["activeRootHubs"]   = stats.ActiveRootHubs;
    return j.dump();
}

} // namespace bhplus
