/*
 * USBPcapGUI - CLI Tool (similar to buslog.exe)
 * Captures data from the command line and spools to file.
 */
#define NOMINMAX

#include "capture_engine.h"
#include "parser_interface.h"
#include <iostream>
#include <fstream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <fmt/format.h>

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running.store(false);
}

void printUsage() {
    std::cout << "USBPcapGUI CLI v0.1.0\n"
              << "Usage: bhplus-cli [options]\n"
              << "\n"
              << "Options:\n"
              << "  -o <file>     Output file (default: stdout)\n"
              << "  -d <id>       Device ID to capture (default: all)\n"
              << "  -b <MB>       Buffer size in MB (default: 16)\n"
              << "  -n <count>    Max events to capture (default: unlimited)\n"
              << "  -h            Show this help\n";
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::string outputFile;
    uint32_t bufferSizeMB = 16;
    uint32_t maxEvents = 0;

    // Simple arg parsing
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) outputFile = argv[++i];
        else if (arg == "-b" && i + 1 < argc) bufferSizeMB = std::stoul(argv[++i]);
        else if (arg == "-n" && i + 1 < argc) maxEvents = std::stoul(argv[++i]);
        else if (arg == "-h") { printUsage(); return 0; }
    }

    bhplus::CaptureEngine engine;
    if (!engine.OpenDriver()) {
        std::cerr << "Error: Could not connect to BHPlus driver.\n"
                  << "Make sure the driver is installed and running.\n";
        return 1;
    }

    std::ofstream outFile;
    std::ostream* out = &std::cout;
    if (!outputFile.empty()) {
        outFile.open(outputFile);
        if (!outFile.is_open()) {
            std::cerr << "Error: Could not open output file: " << outputFile << "\n";
            return 1;
        }
        out = &outFile;
    }

    uint64_t eventCount = 0;

    engine.SetEventCallback([&](BHPLUS_CAPTURE_EVENT event, std::vector<uint8_t> payloadData) {
        const uint8_t* data = payloadData.data();
        auto decoded = bhplus::ParserRegistry::Instance().Decode(event, data, event.DataLength);
        
        *out << fmt::format("{:>8}  {:>12.3f}  {}\n",
            event.SequenceNumber,
            event.Timestamp / 1000.0,
            decoded.summary);
        
        eventCount++;
        if (maxEvents > 0 && eventCount >= maxEvents) {
            g_running.store(false);
        }
    });

    BHPLUS_CAPTURE_CONFIG config = {};
    config.MaxEvents     = maxEvents;
    config.SnapshotLength = 4096;
    config.CaptureData   = 1;

    if (!engine.StartCapture(config)) {
        std::cerr << "Error: Failed to start capture.\n";
        return 1;
    }

    std::cerr << "Capturing... Press Ctrl+C to stop.\n";

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    engine.StopCapture();
    std::cerr << fmt::format("\nCapture complete. {} events captured.\n", eventCount);

    return 0;
}
