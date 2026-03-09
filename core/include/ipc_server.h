#pragma once

/*
 * USBPcapGUI - JSON-RPC IPC Server
 * Provides Named Pipe server for Node.js GUI backend communication.
 * Uses JSON-RPC 2.0 protocol over a single duplex pipe.
 *
 * Pipe name: \\.\pipe\bhplus-core
 *
 * Supported methods:
 *   capture.start   { config }       -> { ok }
 *   capture.stop    {}               -> { ok }
 *   capture.status  {}               -> { capturing, stats }
 *   devices.list    {}               -> { devices[] }
 *   events.query    { filter, limit } -> { events[] }
 *   device.reset    { deviceId }     -> { ok }
 *
 * Notifications (server → client):
 *   capture.event   { event }
 *   capture.started {}
 *   capture.stopped {}
 */

#include "bhplus_types.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>

namespace bhplus {

class CaptureEngine;  // forward decl

class IpcServer {
public:
    IpcServer();
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    /// Set the capture engine to forward commands to
    void SetCaptureEngine(CaptureEngine* engine);

    /// Start listening on the named pipe
    bool Start();

    /// Stop the server and close all connections
    void Stop();

    /// Check if server is running
    bool IsRunning() const;

    /// Push a capture event to all connected clients
    void BroadcastEvent(const BHPLUS_CAPTURE_EVENT& event, const uint8_t* data, uint32_t dataLen);

    /// Push an arbitrary notification to all connected clients
    void BroadcastNotification(const std::string& method, const std::string& paramsJson);

private:
    static constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\bhplus-core";
    static constexpr DWORD PIPE_BUFFER_SIZE = 64 * 1024;  // 64KB
    static constexpr int MAX_CLIENTS = 4;

    struct ClientContext {
        HANDLE      pipeHandle = INVALID_HANDLE_VALUE;
        OVERLAPPED  overlapped{};
        HANDLE      event = nullptr;
        std::thread readThread;
        std::atomic<bool> connected{false};
        std::mutex  writeMutex;
    };

    void AcceptThread();
    void ClientReadLoop(ClientContext* ctx);
    void HandleRequest(ClientContext* ctx, const std::string& requestJson);
    void SendResponse(ClientContext* ctx, const std::string& responseJson);
    void SendToAll(const std::string& json);
    void DisconnectClient(ClientContext* ctx);

    // JSON-RPC handlers
    std::string HandleCaptureStart(const std::string& paramsJson);
    std::string HandleCaptureStop(const std::string& paramsJson);
    std::string HandleCaptureStatus(const std::string& paramsJson);
    std::string HandleDevicesList(const std::string& paramsJson);
    std::string HandleHubsList(const std::string& paramsJson);
    std::string HandleUsbPcapStatus(const std::string& paramsJson);
    std::string HandleUsbPcapInstall(const std::string& paramsJson);
    std::string HandleEventsQuery(const std::string& paramsJson);
    std::string HandleDeviceReset(const std::string& paramsJson);

    // Helpers
    std::string EventToJson(const BHPLUS_CAPTURE_EVENT& event, const uint8_t* data, uint32_t dataLen);
    std::string DeviceInfoToJson(const BHPLUS_USB_DEVICE_INFO& device);
    std::string StatsToJson(const BHPLUS_STATS& stats);

    CaptureEngine* m_engine = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_acceptThread;
    std::mutex m_clientsMutex;
    std::vector<std::unique_ptr<ClientContext>> m_clients;
    HANDLE m_stopEvent = nullptr;
};

} // namespace bhplus
