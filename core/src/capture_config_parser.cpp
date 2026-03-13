/*
 * USBPcapGUI - Capture Config Parser Implementation
 */

#include "capture_config_parser.h"
#include "bhplus_types.h"

#include <nlohmann/json.hpp>
#include <algorithm>

using json = nlohmann::json;

namespace bhplus {

BHPLUS_CAPTURE_CONFIG ParseCaptureStartParams(const std::string& paramsJson) {
    auto params = json::parse(paramsJson);   // throws on invalid JSON

    BHPLUS_CAPTURE_CONFIG config{};

    config.MaxEvents = params.value("maxEvents", 100000u);

    // snapshotLen from interface-options dialog
    // Legacy alias "maxDataPerEvent" accepted when "snapshotLen" is absent/zero
    uint32_t snapLen = params.value("snapshotLen", 0u);
    if (snapLen == 0) snapLen = params.value("maxDataPerEvent", 65535u);
    config.SnapshotLength = snapLen;

    config.BufferLength      = params.value("bufferLen",          1048576u);
    config.CaptureData       = params.value("captureData",        true) ? 1 : 0;
    config.FilterBus         = static_cast<UINT16>(params.value("filterBus", 0u));

    // capture scope flags
    config.CaptureAllDevices = params.value("captureAll",         true) ? 1 : 0;
    config.CaptureNewDevices = params.value("captureNew",         true) ? 1 : 0;
    config.InjectDescriptors = params.value("injectDescriptors",  true) ? 1 : 0;

    // Device address filter
    if (params.contains("deviceIds") && params["deviceIds"].is_array()) {
        config.FilterDeviceCount = std::min<uint32_t>(
            static_cast<uint32_t>(params["deviceIds"].size()),
            BHPLUS_MAX_FILTER_DEVICES
        );
        for (uint32_t i = 0; i < config.FilterDeviceCount; ++i) {
            config.FilterDeviceAddresses[i] = static_cast<UINT16>(
                params["deviceIds"][i].get<uint32_t>());
        }
    } else if (params.contains("deviceId") && params["deviceId"].is_number_unsigned()) {
        config.FilterDeviceCount = 1;
        config.FilterDeviceAddresses[0] = static_cast<UINT16>(
            params["deviceId"].get<uint32_t>());
    }

    return config;
}

} // namespace bhplus
