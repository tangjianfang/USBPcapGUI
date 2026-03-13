#pragma once
/*
 * USBPcapGUI - Shared Logging Initialiser
 *
 * Call InitLogger("bhplus-core") once at process startup (before any spdlog calls).
 * Creates <exe_dir>/Logs/<name>.log with rotating file sink:
 *   - max 5 MB per file
 *   - up to 3 rotated files  (name.log, name.1.log, name.2.log)
 *   - console sink for programs that run with a visible terminal
 *
 * Usage:
 *   InitLogger("bhplus-core");           // stdout + file
 *   InitLogger("USBPcapGUI", false);     // file only (GUI / no-console apps)
 */

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/null_sink.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

inline void InitLogger(const std::string& logName, bool withConsole = true)
{
    // ---------------------------------------------------------------
    // Resolve  <exe_dir>/Logs/
    // ---------------------------------------------------------------
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::error_code ec;
    const std::filesystem::path logDir =
        std::filesystem::path(exePath).parent_path() / L"Logs";
    std::filesystem::create_directories(logDir, ec);  // ignore errors

    const auto logFile = (logDir / (logName + ".log")).string();

    // ---------------------------------------------------------------
    // Build sink list
    // ---------------------------------------------------------------
    std::vector<spdlog::sink_ptr> sinks;

    if (withConsole) {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        sinks.push_back(std::move(consoleSink));
    }

    // rotating_file_sink: 5 MB limit, keep 3 files (name.log / name.1.log / name.2.log)
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFile,
        5ULL * 1024 * 1024,  // 5 MB
        3,                   // max 3 rotated copies
        false                // do NOT rotate on open (append to existing)
    );
    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    sinks.push_back(std::move(fileSink));

    // ---------------------------------------------------------------
    // Create and install as default logger
    // ---------------------------------------------------------------
    auto logger = std::make_shared<spdlog::logger>(
        logName,
        sinks.begin(), sinks.end()
    );
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);   // flush immediately on every debug/info/warn/err

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::milliseconds(500)); // safety flush every 500ms

    spdlog::info("Logger started → {}", logFile);
}
