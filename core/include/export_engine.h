#pragma once

#include "bhplus_types.h"
#include "parser_interface.h"
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace bhplus {

enum class ExportFormat {
    Text,
    Csv,
    Json,
    Pcap
};

struct ExportRecord {
    BHPLUS_CAPTURE_EVENT event{};
    std::vector<uint8_t> payload;
    DecodedEvent         decoded;
};

class ExportEngine {
public:
    static ExportFormat ParseFormat(const std::string& format);
    static const char* FormatName(ExportFormat format);
    static bool Write(std::ostream& out,
                      ExportFormat format,
                      const std::vector<ExportRecord>& records);

    static std::string FormatTimestamp(uint64_t timestamp);
    static std::string ToHex(const uint8_t* data, size_t size);
    static std::string BuildTextLine(const ExportRecord& record);
};

} // namespace bhplus
