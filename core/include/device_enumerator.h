#pragma once

/*
 * USBPcapGUI - Device Enumerator
 * Uses SetupDi APIs to discover and enumerate devices.
 */

#include "bhplus_types.h"
#include <vector>
#include <string>

namespace bhplus {

struct DeviceNode {
    std::wstring    instanceId;
    std::wstring    description;
    std::wstring    friendlyName;
    std::wstring    manufacturer;
    std::wstring    hardwareIds;
    std::wstring    locationInfo;
    BHPLUS_BUS_TYPE busType;
    uint16_t        vendorId;
    uint16_t        productId;
    bool            isPresent;
    std::vector<DeviceNode> children;
};

class DeviceEnumerator {
public:
    // Build full device tree
    static std::vector<DeviceNode> EnumerateAll();
    
    // Enumerate devices for a specific bus type
    static std::vector<DeviceNode> EnumerateByBus(BHPLUS_BUS_TYPE busType);
    
    // Get GUID for a bus type
    static GUID GetDeviceClassGuid(BHPLUS_BUS_TYPE busType);

    // Get device details by instance ID
    static DeviceNode GetDeviceByInstanceId(const std::wstring& instanceId);
};

} // namespace bhplus
