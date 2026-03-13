/*
 * USBPcapGUI - SDK Implementation
 */
#define NOMINMAX

#include "bhplus_sdk.h"
#include "capture_engine.h"
#include "usb_actions.h"
#include <memory>
#include <vector>
#include <algorithm>

struct BHPlusSession {
    std::unique_ptr<bhplus::CaptureEngine> engine;
    BHPLUS_EVENT_CALLBACK userCallback = nullptr;
    void* userData = nullptr;
};

extern "C" {

BHPLUS_API BHPLUS_HANDLE BHPlus_Open(void) {
    auto* session = new BHPlusSession();
    session->engine = std::make_unique<bhplus::CaptureEngine>();
    return session;
}

BHPLUS_API void BHPlus_Close(BHPLUS_HANDLE handle) {
    auto* session = static_cast<BHPlusSession*>(handle);
    if (session) {
        session->engine->StopCapture();
        session->engine->CloseDriver();
        delete session;
    }
}

BHPLUS_API int BHPlus_IsDriverLoaded(BHPLUS_HANDLE handle) {
    auto* session = static_cast<BHPlusSession*>(handle);
    return session && session->engine->IsDriverLoaded() ? 1 : 0;
}

BHPLUS_API int BHPlus_StartCapture(BHPLUS_HANDLE handle, const BHPLUS_CAPTURE_CONFIG* config) {
    auto* session = static_cast<BHPlusSession*>(handle);
    if (!session || !config) return 0;
    return session->engine->StartCapture(*config) ? 1 : 0;
}

BHPLUS_API int BHPlus_StopCapture(BHPLUS_HANDLE handle) {
    auto* session = static_cast<BHPlusSession*>(handle);
    if (!session) return 0;
    return session->engine->StopCapture() ? 1 : 0;
}

BHPLUS_API int BHPlus_IsCapturing(BHPLUS_HANDLE handle) {
    auto* session = static_cast<BHPlusSession*>(handle);
    return session && session->engine->IsCapturing() ? 1 : 0;
}

BHPLUS_API void BHPlus_SetEventCallback(BHPLUS_HANDLE handle,
                                         BHPLUS_EVENT_CALLBACK callback,
                                         void* userData) {
    auto* session = static_cast<BHPlusSession*>(handle);
    if (!session) return;
    
    session->userCallback = callback;
    session->userData = userData;

    session->engine->SetEventCallback(
        [session](BHPLUS_CAPTURE_EVENT event, std::vector<uint8_t> payload) {
            if (session->userCallback) {
                session->userCallback(&event, payload.data(), session->userData);
            }
        });
}

BHPLUS_API int BHPlus_EnumerateDevices(BHPLUS_HANDLE handle,
                                        BHPLUS_DEVICE_INFO* devices,
                                        int maxDevices) {
    auto* session = static_cast<BHPlusSession*>(handle);
    if (!session || !devices) return 0;

    auto devList = session->engine->EnumerateDevices();
    int count = static_cast<int>((std::min)(devList.size(), static_cast<size_t>(maxDevices)));
    for (int i = 0; i < count; i++) {
        // Convert BHPLUS_USB_DEVICE_INFO to BHPLUS_DEVICE_INFO (legacy SDK struct)
        devices[i].DeviceId  = (static_cast<UINT32>(devList[i].Bus) << 16)
                             | devList[i].DeviceAddress;
        devices[i].BusType   = BHPLUS_BUS_USB;
        devices[i].VendorId  = devList[i].VendorId;
        devices[i].ProductId = devList[i].ProductId;
        wcsncpy_s(devices[i].DeviceName, BHPLUS_MAX_DEVICE_NAME,
                  devList[i].DeviceName, _TRUNCATE);
        wcsncpy_s(devices[i].InstanceId, BHPLUS_MAX_DEVICE_NAME,
                  devList[i].SerialNumber, _TRUNCATE);
    }
    return count;
}

BHPLUS_API int BHPlus_SendCommand(BHPLUS_HANDLE handle,
                                   unsigned int deviceId,
                                   const unsigned char* command,
                                   unsigned int commandLength,
                                   unsigned char* response,
                                   unsigned int* responseLength,
                                   unsigned int timeoutMs) {
    auto* session = static_cast<BHPlusSession*>(handle);
    (void)session;
    if (!command || commandLength < 8) return 0;

    bhplus::UsbControlTransferSetup setup;
    setup.requestType = command[0];
    setup.request = command[1];
    setup.value = static_cast<UINT16>(command[2] | (command[3] << 8));
    setup.index = static_cast<UINT16>(command[4] | (command[5] << 8));
    setup.length = static_cast<UINT16>(command[6] | (command[7] << 8));
    setup.timeoutMs = timeoutMs;

    std::vector<uint8_t> payload;
    if (!bhplus::IsUsbControlTransferIn(setup) && commandLength > 8) {
        payload.assign(command + 8, command + commandLength);
    }

    const auto result = bhplus::SendUsbControlTransfer(deviceId, setup, payload);
    if (!result.ok) return 0;

    if (bhplus::IsUsbControlTransferIn(setup) && response && responseLength) {
        const auto capacity = *responseLength;
        const auto copyBytes = (std::min)(capacity, static_cast<unsigned int>(result.data.size()));
        if (copyBytes > 0) {
            std::copy_n(result.data.data(), copyBytes, response);
        }
        *responseLength = copyBytes;
    } else if (responseLength) {
        *responseLength = 0;
    }

    return 1;
}

BHPLUS_API int BHPlus_ResetDevice(BHPLUS_HANDLE handle, unsigned int deviceId) {
    auto* session = static_cast<BHPlusSession*>(handle);
    (void)session;
    return bhplus::ResetUsbDevice(deviceId).ok ? 1 : 0;
}

BHPLUS_API int BHPlus_GetStatistics(BHPLUS_HANDLE handle, BHPLUS_STATS* stats) {
    auto* session = static_cast<BHPlusSession*>(handle);
    if (!session || !stats) return 0;
    *stats = session->engine->GetStatistics();
    return 1;
}

} // extern "C"
