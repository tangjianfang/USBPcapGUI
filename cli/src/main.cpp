/*
 * USBPcapGUI - CLI Tool (similar to buslog.exe)
 * Captures data from the command line and spools to file.
 */
#define NOMINMAX

#include "capture_engine.h"
#include "driver_manager.h"
#include "export_engine.h"
#include "filter_engine.h"
#include "parser_interface.h"
#include "usb_actions.h"
#include "log_init.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <fstream>
#include <fmt/format.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running.store(false);
}

struct CliOptions {
    std::string outputFile;
    std::string filterText;
    std::string payloadHex;
    bhplus::ExportFormat format = bhplus::ExportFormat::Text;
    uint32_t snapshotLength = 4096;
    uint32_t maxEvents = 0;
    uint16_t deviceAddress = 0;
    uint16_t hubIndex = 0;
    uint32_t timeoutMs = 1000;
    std::optional<uint32_t> resetDeviceId;
    std::optional<uint32_t> controlDeviceId;
    std::optional<uint32_t> requestType;
    std::optional<uint32_t> request;
    std::optional<uint32_t> value;
    std::optional<uint32_t> index;
    std::optional<uint32_t> length;
    bool listHubs = false;
    bool listDevices = false;
    bool statusOnly = false;
};

std::optional<uint32_t> ParseUnsignedValue(const std::string& text) {
    if (text.empty()) return std::nullopt;

    uint32_t value = 0;
    const bool isHex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const auto* begin = text.data() + (isHex ? 2 : 0);
    const auto* end = text.data() + text.size();
    const auto base = isHex ? 16 : 10;
    auto [ptr, ec] = std::from_chars(begin, end, value, base);
    if (ec != std::errc() || ptr != end) return std::nullopt;
    return value;
}

void printUsage() {
    std::cout << "USBPcapGUI CLI v0.1.0\n"
              << "Usage: bhplus-cli [options]\n"
              << "\n"
              << "Options:\n"
              << "  -o <file>         Output file (default: stdout)\n"
              << "  -d <addr>         USB device address filter (default: all)\n"
              << "  --hub <index>     USBPcap hub index to open (default: all)\n"
              << "  --snap <bytes>    Snapshot length (default: 4096)\n"
              << "  --format <fmt>    txt | csv | json | pcap (default: txt)\n"
              << "  -f <expr>         Filter expression (protocol/cmd/status/data/...)\n"
              << "  -n <count>        Max matching events to capture (default: unlimited)\n"
              << "  --status          Print USBPcap runtime status and exit\n"
              << "  --list-hubs       List visible USBPcap hubs and exit\n"
              << "  --list-devices    Enumerate USB devices and exit\n"
              << "  --reset-device <deviceId>      Reset a USB device by combined deviceId\n"
              << "  --control-device <deviceId>    Send one USB control transfer to deviceId\n"
              << "  --request-type <hex>           bmRequestType (hex or decimal)\n"
              << "  --request <hex>                bRequest (hex or decimal)\n"
              << "  --value <hex>                  wValue (hex or decimal)\n"
              << "  --index <hex>                  wIndex (hex or decimal)\n"
              << "  --length <n>                   wLength\n"
              << "  --payload <hex>                OUT transfer payload bytes\n"
              << "  --timeout <ms>                 Control transfer timeout (default: 1000)\n"
              << "  -h            Show this help\n";
}

void printStatus() {
    const auto installed = bhplus::DriverManager::IsUSBPcapInstalled();
    const auto interfaces = bhplus::DriverManager::GetUSBPcapInterfaceCount();
    const auto serviceInstalled = bhplus::DriverManager::IsUSBPcapServiceInstalled();
    const auto serviceRunning = bhplus::DriverManager::IsUSBPcapDriverRunning();
    const auto upperFilter = bhplus::DriverManager::HasUSBPcapUpperFilter();

    std::cout << "USBPcap status\n"
              << "  installed: " << (installed ? "yes" : "no") << '\n'
              << "  interfaces: " << interfaces << '\n'
              << "  serviceInstalled: " << (serviceInstalled ? "yes" : "no") << '\n'
              << "  serviceRunning: " << (serviceRunning ? "yes" : "no") << '\n'
              << "  upperFilterRegistered: " << (upperFilter ? "yes" : "no") << '\n'
              << "  restartRecommended: " << ((installed && interfaces == 0 && upperFilter) ? "yes" : "no") << '\n';
}

void printHubs() {
    const auto hubs = bhplus::EnumerateRootHubs();
    if (hubs.empty()) {
        std::cout << "No USBPcap hubs found.\n";
        return;
    }

    for (const auto& hub : hubs) {
        std::wcout << L"[" << hub.index << L"] " << hub.devicePath
                   << L"  available=" << (hub.available ? L"yes" : L"no");
        if (!hub.hubSymLink.empty()) {
            std::wcout << L"  hub=" << hub.hubSymLink;
        }
        std::wcout << L'\n';
    }
}

void printDevices() {
    if (!bhplus::DriverManager::IsUSBPcapInstalled() ||
        bhplus::DriverManager::GetUSBPcapInterfaceCount() == 0) {
        std::cout << "No USBPcap capture interfaces are available yet.\n";
        printStatus();
        return;
    }

    bhplus::CaptureEngine engine;
    const auto devices = engine.EnumerateDevices();
    if (devices.empty()) {
        std::cout << "No USB devices found through USBPcap hub enumeration.\n";
        return;
    }

    for (const auto& device : devices) {
        const auto deviceId = (static_cast<uint32_t>(device.Bus) << 16) | device.DeviceAddress;
        std::wcout << L"deviceId=" << deviceId
                   << L"  bus=" << device.Bus
                   << L"  dev=" << device.DeviceAddress
                   << L"  vid=0x" << std::hex << std::setw(4) << std::setfill(L'0') << device.VendorId
                   << L"  pid=0x" << std::setw(4) << device.ProductId << std::dec;
        if (device.DeviceName[0] != L'\0') {
            std::wcout << L"  name=" << device.DeviceName;
        }
        std::wcout << L'\n';
    }
}

int runResetAction(uint32_t deviceId) {
    const auto result = bhplus::ResetUsbDevice(deviceId);
    if (!result.ok) {
        std::cerr << "Reset failed: " << result.message << "\n";
        return 1;
    }
    std::cout << "Reset submitted for deviceId=" << deviceId << "\n";
    return 0;
}

int runControlTransfer(const CliOptions& options) {
    if (!options.controlDeviceId.has_value() ||
        !options.requestType.has_value() ||
        !options.request.has_value() ||
        !options.value.has_value() ||
        !options.index.has_value() ||
        !options.length.has_value()) {
        std::cerr << "Error: --control-device requires --request-type, --request, --value, --index, and --length.\n";
        return 1;
    }

    bhplus::UsbControlTransferSetup setup;
    setup.requestType = static_cast<uint8_t>(*options.requestType & 0xffu);
    setup.request = static_cast<uint8_t>(*options.request & 0xffu);
    setup.value = static_cast<uint16_t>(*options.value & 0xffffu);
    setup.index = static_cast<uint16_t>(*options.index & 0xffffu);
    setup.length = static_cast<uint16_t>(*options.length & 0xffffu);
    setup.timeoutMs = options.timeoutMs;

    std::vector<uint8_t> payload;
    if (!options.payloadHex.empty()) {
        std::string error;
        if (!bhplus::DecodeHexString(options.payloadHex, payload, &error)) {
            std::cerr << "Error: " << error << "\n";
            return 1;
        }
    }

    const auto result = bhplus::SendUsbControlTransfer(*options.controlDeviceId, setup, payload);
    if (!result.ok) {
        std::cerr << "Control transfer failed: " << result.message << "\n";
        return 1;
    }

    std::cout << "Control transfer completed. bytesTransferred=" << result.bytesTransferred << "\n";
    if (!result.data.empty()) {
        std::cout << bhplus::EncodeHexString(result.data.data(), result.data.size()) << "\n";
    }
    return 0;
}

int main(int argc, char* argv[]) {
    InitLogger("bhplus-cli");
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    CliOptions options;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) options.outputFile = argv[++i];
        else if (arg == "-d" && i + 1 < argc) options.deviceAddress = static_cast<uint16_t>(std::stoul(argv[++i]));
        else if (arg == "--hub" && i + 1 < argc) options.hubIndex = static_cast<uint16_t>(std::stoul(argv[++i]));
        else if (arg == "--snap" && i + 1 < argc) options.snapshotLength = std::stoul(argv[++i]);
        else if (arg == "--format" && i + 1 < argc) options.format = bhplus::ExportEngine::ParseFormat(argv[++i]);
        else if ((arg == "-f" || arg == "--filter") && i + 1 < argc) options.filterText = argv[++i];
        else if (arg == "-n" && i + 1 < argc) options.maxEvents = std::stoul(argv[++i]);
        else if (arg == "--payload" && i + 1 < argc) options.payloadHex = argv[++i];
        else if (arg == "--timeout" && i + 1 < argc) options.timeoutMs = std::stoul(argv[++i]);
        else if (arg == "--status") options.statusOnly = true;
        else if (arg == "--list-hubs") options.listHubs = true;
        else if (arg == "--list-devices") options.listDevices = true;
        else if (arg == "--reset-device" && i + 1 < argc) options.resetDeviceId = ParseUnsignedValue(argv[++i]);
        else if (arg == "--control-device" && i + 1 < argc) options.controlDeviceId = ParseUnsignedValue(argv[++i]);
        else if (arg == "--request-type" && i + 1 < argc) options.requestType = ParseUnsignedValue(argv[++i]);
        else if (arg == "--request" && i + 1 < argc) options.request = ParseUnsignedValue(argv[++i]);
        else if (arg == "--value" && i + 1 < argc) options.value = ParseUnsignedValue(argv[++i]);
        else if (arg == "--index" && i + 1 < argc) options.index = ParseUnsignedValue(argv[++i]);
        else if (arg == "--length" && i + 1 < argc) options.length = ParseUnsignedValue(argv[++i]);
        else if (arg == "-h") { printUsage(); return 0; }
    }

    if (options.statusOnly) {
        printStatus();
        return 0;
    }

    if (options.listHubs) {
        printHubs();
        return 0;
    }

    if (options.listDevices) {
        printDevices();
        return 0;
    }

    if (options.resetDeviceId.has_value()) {
        return runResetAction(*options.resetDeviceId);
    }

    if (options.controlDeviceId.has_value()) {
        return runControlTransfer(options);
    }

    bhplus::CaptureEngine engine;
    if (!engine.IsDriverLoaded()) {
        std::cerr << "Error: USBPcap is not installed or no capture interface is currently available.\n";
        printStatus();
        return 1;
    }

    const auto conditions = bhplus::FilterEngine::Parse(options.filterText);
    const bool streamTextToStdout = options.outputFile.empty() && options.format == bhplus::ExportFormat::Text;
    std::vector<bhplus::ExportRecord> captured;
    captured.reserve(options.maxEvents > 0 ? options.maxEvents : 1024);

    uint64_t matchedCount = 0;

    engine.SetEventCallback([&](BHPLUS_CAPTURE_EVENT event, std::vector<uint8_t> payloadData) {
        const auto* data = payloadData.empty() ? nullptr : payloadData.data();
        auto decoded = bhplus::ParserRegistry::Instance().Decode(event, data, event.DataLength);
        const auto hex = bhplus::ExportEngine::ToHex(data, payloadData.size());

        if (!bhplus::FilterEngine::Matches(event, decoded, hex, conditions)) {
            return;
        }

        bhplus::ExportRecord record;
        record.event = event;
        record.payload = std::move(payloadData);
        record.decoded = std::move(decoded);

        if (streamTextToStdout) {
            std::cout << bhplus::ExportEngine::BuildTextLine(record) << '\n';
        } else {
            captured.push_back(std::move(record));
        }

        matchedCount++;
        if (options.maxEvents > 0 && matchedCount >= options.maxEvents) {
            g_running.store(false);
        }
    });

    BHPLUS_CAPTURE_CONFIG config = {};
    config.MaxEvents = options.maxEvents;
    config.SnapshotLength = options.snapshotLength;
    config.CaptureData   = 1;
    if (options.deviceAddress > 0) {
        config.FilterDeviceCount = 1;
        config.FilterDeviceAddresses[0] = options.deviceAddress;
    }
    if (options.hubIndex > 0) {
        config.FilterBus = options.hubIndex;
    }

    if (!engine.StartCapture(config)) {
        std::cerr << "Error: Failed to start capture: " << engine.LastError() << "\n";
        return 1;
    }

    std::cerr << "Capturing USB traffic";
    if (!conditions.empty()) {
        std::cerr << " with filter: " << bhplus::FilterEngine::Describe(conditions);
    }
    std::cerr << "... Press Ctrl+C to stop.\n";

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    engine.StopCapture();

    if (!streamTextToStdout) {
        std::unique_ptr<std::ostream> ownedStream;
        std::ostream* out = &std::cout;
        if (!options.outputFile.empty()) {
            auto fileMode = std::ios::out;
            if (options.format == bhplus::ExportFormat::Pcap) fileMode |= std::ios::binary;
            auto file = std::make_unique<std::ofstream>(options.outputFile, fileMode);
            if (!file->is_open()) {
                std::cerr << "Error: Could not open output file: " << options.outputFile << "\n";
                return 1;
            }
            out = file.get();
            ownedStream = std::move(file);
        }

        if (!bhplus::ExportEngine::Write(*out, options.format, captured)) {
            std::cerr << "Error: Failed to write export output.\n";
            return 1;
        }
    }

    std::cerr << fmt::format("\nCapture complete. {} matching event(s) exported as {}.\n",
        matchedCount,
        bhplus::ExportEngine::FormatName(options.format));

    return 0;
}
