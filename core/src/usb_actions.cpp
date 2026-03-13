#define NOMINMAX

#include "usb_actions.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <winusb.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "winusb.lib")

namespace bhplus {
namespace {

struct ResolvedUsbDevice {
    uint32_t     deviceId = 0;
    uint16_t     bus = 0;
    uint16_t     address = 0;
    uint32_t     port = 0;
    std::wstring driverKey;
    std::wstring instanceId;
    std::wstring interfacePath;
};

std::wstring TrimTrailingNulls(std::wstring value) {
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::wstring GetRegistryProperty(HDEVINFO devInfo, SP_DEVINFO_DATA& devData, DWORD property) {
    DWORD dataType = 0;
    DWORD requiredSize = 0;
    SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, property, &dataType, nullptr, 0, &requiredSize);
    if (requiredSize == 0) return {};

    std::wstring value(requiredSize / sizeof(wchar_t), L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(devInfo,
                                           &devData,
                                           property,
                                           &dataType,
                                           reinterpret_cast<PBYTE>(value.data()),
                                           requiredSize,
                                           nullptr)) {
        return {};
    }
    return TrimTrailingNulls(std::move(value));
}

bool FindInstanceIdByDriverKey(const std::wstring& driverKey, std::wstring& instanceId) {
    HDEVINFO devInfo = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(devData);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(devInfo, index, &devData); ++index) {
        const auto currentDriverKey = GetRegistryProperty(devInfo, devData, SPDRP_DRIVER);
        if (_wcsicmp(currentDriverKey.c_str(), driverKey.c_str()) != 0) {
            continue;
        }

        wchar_t buffer[MAX_DEVICE_ID_LEN]{};
        if (SetupDiGetDeviceInstanceIdW(devInfo, &devData, buffer, MAX_DEVICE_ID_LEN, nullptr)) {
            instanceId = buffer;
            SetupDiDestroyDeviceInfoList(devInfo);
            return true;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return false;
}

bool FindUsbInterfacePath(const std::wstring& instanceId, std::wstring& interfacePath) {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE,
                                            nullptr,
                                            nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_DEVINTERFACE_USB_DEVICE, index, &ifData); ++index) {
        SP_DEVINFO_DATA devData{};
        devData.cbSize = sizeof(devData);
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, &devData);
        if (requiredSize == 0) {
            continue;
        }

        std::vector<BYTE> detailBuffer(requiredSize, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo,
                                              &ifData,
                                              detail,
                                              requiredSize,
                                              nullptr,
                                              &devData)) {
            continue;
        }

        wchar_t currentInstanceId[MAX_DEVICE_ID_LEN]{};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &devData, currentInstanceId, MAX_DEVICE_ID_LEN, nullptr)) {
            continue;
        }

        if (_wcsicmp(currentInstanceId, instanceId.c_str()) == 0) {
            interfacePath = detail->DevicePath;
            SetupDiDestroyDeviceInfoList(devInfo);
            return true;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return false;
}

bool ResolveUsbDevice(uint32_t deviceId, ResolvedUsbDevice& resolved, std::string& error) {
    const uint16_t bus = static_cast<uint16_t>((deviceId >> 16) & 0xffffu);
    const uint16_t address = static_cast<uint16_t>(deviceId & 0xffffu);
    if (bus == 0 || address == 0) {
        error = "Invalid USB deviceId";
        return false;
    }

    const std::wstring hubPath = L"\\\\.\\USBPcap" + std::to_wstring(bus);
    HANDLE hubHandle = CreateFileW(hubPath.c_str(),
                                   GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr,
                                   OPEN_EXISTING,
                                   0,
                                   nullptr);
    if (hubHandle == INVALID_HANDLE_VALUE) {
        error = "Unable to open USBPcap hub handle for device resolution";
        return false;
    }

    USB_NODE_INFORMATION nodeInfo{};
    DWORD returned = 0;
    if (!DeviceIoControl(hubHandle,
                         IOCTL_USB_GET_NODE_INFORMATION,
                         &nodeInfo,
                         sizeof(nodeInfo),
                         &nodeInfo,
                         sizeof(nodeInfo),
                         &returned,
                         nullptr)) {
        CloseHandle(hubHandle);
        error = "Failed to query root hub information";
        return false;
    }

    const ULONG portCount = nodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts;
    for (ULONG port = 1; port <= portCount; ++port) {
        std::vector<BYTE> connBuf(512, 0);
        auto* connection = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(connBuf.data());
        connection->ConnectionIndex = port;
        returned = 0;
        if (!DeviceIoControl(hubHandle,
                             IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                             connection,
                             static_cast<DWORD>(connBuf.size()),
                             connection,
                             static_cast<DWORD>(connBuf.size()),
                             &returned,
                             nullptr)) {
            continue;
        }
        if (connection->ConnectionStatus != DeviceConnected || connection->DeviceAddress != address) {
            continue;
        }

        std::vector<BYTE> keyBuf(sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) + 1024 * sizeof(wchar_t), 0);
        auto* driverKey = reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(keyBuf.data());
        driverKey->ConnectionIndex = port;
        returned = 0;
        if (!DeviceIoControl(hubHandle,
                             IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,
                             driverKey,
                             static_cast<DWORD>(keyBuf.size()),
                             driverKey,
                             static_cast<DWORD>(keyBuf.size()),
                             &returned,
                             nullptr)) {
            CloseHandle(hubHandle);
            error = "Failed to resolve USB device driver key";
            return false;
        }

        resolved.deviceId = deviceId;
        resolved.bus = bus;
        resolved.address = address;
        resolved.port = port;
        resolved.driverKey = driverKey->DriverKeyName;
        CloseHandle(hubHandle);

        if (!FindInstanceIdByDriverKey(resolved.driverKey, resolved.instanceId)) {
            error = "Unable to map USB device to a present instance";
            return false;
        }

        FindUsbInterfacePath(resolved.instanceId, resolved.interfacePath);
        return true;
    }

    CloseHandle(hubHandle);
    error = "USB device was not found on the selected root hub";
    return false;
}

bool RestartByInstanceId(const std::wstring& instanceId, std::string& error) {
    HDEVINFO devInfo = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE) {
        error = "SetupDiGetClassDevs failed";
        return false;
    }

    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(devData);
    if (!SetupDiOpenDeviceInfoW(devInfo, instanceId.c_str(), nullptr, 0, &devData)) {
        SetupDiDestroyDeviceInfoList(devInfo);
        error = "Unable to open device instance for reset";
        return false;
    }

    SP_PROPCHANGE_PARAMS params{};
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    params.StateChange = DICS_PROPCHANGE;
    params.Scope = DICS_FLAG_GLOBAL;
    params.HwProfile = 0;

    const bool ok = SetupDiSetClassInstallParamsW(devInfo,
                                                  &devData,
                                                  reinterpret_cast<SP_CLASSINSTALL_HEADER*>(&params),
                                                  sizeof(params)) &&
                    SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devInfo, &devData);
    if (!ok) {
        error = "Device reset request failed (administrator privileges may be required)";
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return ok;
}

} // namespace

bool DecodeHexString(std::string_view hex, std::vector<uint8_t>& out, std::string* error) {
    std::string normalized;
    normalized.reserve(hex.size());
    for (const unsigned char ch : hex) {
        if (std::isspace(ch) || ch == ':' || ch == '-') {
            continue;
        }
        if (!std::isxdigit(ch)) {
            if (error) *error = "Hex data contains non-hex characters";
            return false;
        }
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }

    if (normalized.empty()) {
        out.clear();
        return true;
    }

    if ((normalized.size() % 2) != 0) {
        normalized.insert(normalized.begin(), '0');
    }

    out.clear();
    out.reserve(normalized.size() / 2);
    for (size_t i = 0; i < normalized.size(); i += 2) {
        const auto high = normalized[i];
        const auto low = normalized[i + 1];
        const auto byte = static_cast<uint8_t>((std::stoi(std::string{high, low}, nullptr, 16)) & 0xff);
        out.push_back(byte);
    }
    return true;
}

std::string EncodeHexString(const uint8_t* data, size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    if (!data || size == 0) return result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result.push_back(digits[(data[i] >> 4) & 0x0f]);
        result.push_back(digits[data[i] & 0x0f]);
    }
    return result;
}

bool IsUsbControlTransferIn(const UsbControlTransferSetup& setup) {
    return (setup.requestType & 0x80u) != 0;
}

UsbActionResult ResetUsbDevice(uint32_t deviceId) {
    UsbActionResult result;
    result.deviceId = deviceId;

    ResolvedUsbDevice device;
    std::string error;
    if (!ResolveUsbDevice(deviceId, device, error)) {
        result.message = std::move(error);
        return result;
    }

    if (!RestartByInstanceId(device.instanceId, error)) {
        result.message = std::move(error);
        return result;
    }

    result.ok = true;
    result.message = "Reset request submitted";
    return result;
}

UsbActionResult SendUsbControlTransfer(uint32_t deviceId,
                                       const UsbControlTransferSetup& setup,
                                       const std::vector<uint8_t>& outData) {
    UsbActionResult result;
    result.deviceId = deviceId;

    ResolvedUsbDevice device;
    std::string error;
    if (!ResolveUsbDevice(deviceId, device, error)) {
        result.message = std::move(error);
        return result;
    }

    if (device.interfacePath.empty()) {
        result.message = "No USB device interface path was found for this device";
        return result;
    }

    HANDLE devHandle = CreateFileW(device.interfacePath.c_str(),
                                   GENERIC_WRITE | GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (devHandle == INVALID_HANDLE_VALUE) {
        result.message = "Unable to open the device interface path";
        return result;
    }

    WINUSB_INTERFACE_HANDLE winusbHandle = nullptr;
    if (!WinUsb_Initialize(devHandle, &winusbHandle)) {
        CloseHandle(devHandle);
        result.message = "The selected device is not bound to WinUSB; custom control transfer is unavailable";
        return result;
    }

    WINUSB_SETUP_PACKET packet{};
    packet.RequestType = setup.requestType;
    packet.Request = setup.request;
    packet.Value = setup.value;
    packet.Index = setup.index;
    packet.Length = setup.length;

    std::vector<uint8_t> buffer;
    if (IsUsbControlTransferIn(setup)) {
        buffer.assign(setup.length, 0);
    } else {
        buffer = outData;
        if (buffer.size() < setup.length) {
            buffer.resize(setup.length, 0);
        }
    }

    ULONG transferred = 0;
    const BOOL ok = WinUsb_ControlTransfer(winusbHandle,
                                           packet,
                                           buffer.empty() ? nullptr : buffer.data(),
                                           static_cast<ULONG>(buffer.size()),
                                           &transferred,
                                           nullptr);
    if (!ok) {
        result.message = "WinUSB control transfer failed";
        WinUsb_Free(winusbHandle);
        CloseHandle(devHandle);
        return result;
    }

    result.ok = true;
    result.bytesTransferred = transferred;
    if (IsUsbControlTransferIn(setup)) {
        buffer.resize(transferred);
        result.data = std::move(buffer);
    }
    result.message = "Control transfer completed";

    WinUsb_Free(winusbHandle);
    CloseHandle(devHandle);
    return result;
}

} // namespace bhplus
