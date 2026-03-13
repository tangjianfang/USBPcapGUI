#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bhplus {

struct UsbControlTransferSetup {
    uint8_t  requestType = 0;
    uint8_t  request = 0;
    uint16_t value = 0;
    uint16_t index = 0;
    uint16_t length = 0;
    uint32_t timeoutMs = 1000;
};

struct UsbActionResult {
    bool                ok = false;
    uint32_t            deviceId = 0;
    uint32_t            bytesTransferred = 0;
    std::vector<uint8_t> data;
    std::string         message;
};

bool DecodeHexString(std::string_view hex, std::vector<uint8_t>& out, std::string* error = nullptr);
std::string EncodeHexString(const uint8_t* data, size_t size);
bool IsUsbControlTransferIn(const UsbControlTransferSetup& setup);

UsbActionResult ResetUsbDevice(uint32_t deviceId);
UsbActionResult SendUsbControlTransfer(uint32_t deviceId,
                                       const UsbControlTransferSetup& setup,
                                       const std::vector<uint8_t>& outData);

} // namespace bhplus
