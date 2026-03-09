/*
 * USBPcapGUI - Device Enumerator Implementation
 * Uses SetupDi APIs to discover system devices.
 */

#include "device_enumerator.h"
#include <spdlog/spdlog.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <initguid.h>
#include <usbiodef.h>
#include <ntddstor.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace bhplus {

static std::wstring GetDeviceProperty(HDEVINFO devInfo, SP_DEVINFO_DATA& devData, DWORD property) {
    DWORD dataType = 0;
    DWORD requiredSize = 0;
    
    SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, property, &dataType, nullptr, 0, &requiredSize);
    if (requiredSize == 0)
        return L"";

    std::wstring result(requiredSize / sizeof(WCHAR), L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, property, &dataType,
            reinterpret_cast<PBYTE>(result.data()), requiredSize, nullptr)) {
        return L"";
    }

    // Trim null terminators
    while (!result.empty() && result.back() == L'\0')
        result.pop_back();

    return result;
}

static std::wstring GetDeviceInstanceId(HDEVINFO devInfo, SP_DEVINFO_DATA& devData) {
    WCHAR instanceId[MAX_DEVICE_ID_LEN] = {};
    if (!SetupDiGetDeviceInstanceIdW(devInfo, &devData, instanceId, MAX_DEVICE_ID_LEN, nullptr))
        return L"";
    return instanceId;
}

std::vector<DeviceNode> DeviceEnumerator::EnumerateAll() {
    std::vector<DeviceNode> devices;

    HDEVINFO devInfo = SetupDiGetClassDevsW(
        nullptr,       // All classes
        nullptr,       // All enumerators
        nullptr,
        DIGCF_PRESENT | DIGCF_ALLCLASSES
    );

    if (devInfo == INVALID_HANDLE_VALUE) {
        spdlog::error("SetupDiGetClassDevs failed: {}", GetLastError());
        return devices;
    }

    SP_DEVINFO_DATA devData = {};
    devData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); i++) {
        DeviceNode node;
        node.instanceId   = GetDeviceInstanceId(devInfo, devData);
        node.description  = GetDeviceProperty(devInfo, devData, SPDRP_DEVICEDESC);
        node.friendlyName = GetDeviceProperty(devInfo, devData, SPDRP_FRIENDLYNAME);
        node.manufacturer = GetDeviceProperty(devInfo, devData, SPDRP_MFG);
        node.hardwareIds  = GetDeviceProperty(devInfo, devData, SPDRP_HARDWAREID);
        node.locationInfo = GetDeviceProperty(devInfo, devData, SPDRP_LOCATION_INFORMATION);
        node.isPresent    = true;

        // Determine bus type from enumerator
        std::wstring enumerator = GetDeviceProperty(devInfo, devData, SPDRP_ENUMERATOR_NAME);
        if (enumerator == L"USB") {
            node.busType = BHPLUS_BUS_USB;
        } else if (enumerator == L"PCI" || enumerator == L"SCSI") {
            // Could be NVMe, SATA, etc. — need deeper inspection
            node.busType = BHPLUS_BUS_UNKNOWN;
        } else if (enumerator == L"ACPI") {
            node.busType = BHPLUS_BUS_UNKNOWN;
        } else {
            node.busType = BHPLUS_BUS_UNKNOWN;
        }

        // Parse VID/PID from hardware ID for USB devices
        if (node.busType == BHPLUS_BUS_USB && !node.hardwareIds.empty()) {
            // Format: USB\VID_xxxx&PID_xxxx
            auto vidPos = node.hardwareIds.find(L"VID_");
            auto pidPos = node.hardwareIds.find(L"PID_");
            if (vidPos != std::wstring::npos) {
                node.vendorId = static_cast<uint16_t>(
                    wcstoul(node.hardwareIds.substr(vidPos + 4, 4).c_str(), nullptr, 16));
            }
            if (pidPos != std::wstring::npos) {
                node.productId = static_cast<uint16_t>(
                    wcstoul(node.hardwareIds.substr(pidPos + 4, 4).c_str(), nullptr, 16));
            }
        }

        devices.push_back(std::move(node));
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    
    spdlog::info("Enumerated {} devices", devices.size());
    return devices;
}

std::vector<DeviceNode> DeviceEnumerator::EnumerateByBus(BHPLUS_BUS_TYPE busType) {
    auto all = EnumerateAll();
    std::vector<DeviceNode> filtered;
    
    for (auto& node : all) {
        if (node.busType == busType) {
            filtered.push_back(std::move(node));
        }
    }
    
    return filtered;
}

GUID DeviceEnumerator::GetDeviceClassGuid(BHPLUS_BUS_TYPE busType) {
    switch (busType) {
        case BHPLUS_BUS_USB:    return GUID_DEVINTERFACE_USB_DEVICE;
        case BHPLUS_BUS_SCSI:   return GUID_DEVINTERFACE_DISK;
        default:                return GUID_NULL;
    }
}

DeviceNode DeviceEnumerator::GetDeviceByInstanceId(const std::wstring& instanceId) {
    DeviceNode node;
    
    HDEVINFO devInfo = SetupDiGetClassDevsW(
        nullptr, instanceId.c_str(), nullptr,
        DIGCF_PRESENT | DIGCF_ALLCLASSES | DIGCF_DEVICEINTERFACE
    );

    if (devInfo == INVALID_HANDLE_VALUE)
        return node;

    SP_DEVINFO_DATA devData = {};
    devData.cbSize = sizeof(SP_DEVINFO_DATA);

    if (SetupDiEnumDeviceInfo(devInfo, 0, &devData)) {
        node.instanceId   = instanceId;
        node.description  = GetDeviceProperty(devInfo, devData, SPDRP_DEVICEDESC);
        node.friendlyName = GetDeviceProperty(devInfo, devData, SPDRP_FRIENDLYNAME);
        node.isPresent    = true;
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return node;
}

} // namespace bhplus
