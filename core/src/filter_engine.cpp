/*
 * USBPcapGUI - Filter Engine
 */

#include "filter_engine.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <regex>
#include <sstream>

namespace bhplus {

namespace {

std::string ToLower(std::string_view value) {
    std::string out(value.begin(), value.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool MatchString(std::string_view value, std::string_view pattern) {
    if (value.empty() && !pattern.empty()) return false;

    try {
        return std::regex_search(std::string(value), std::regex(std::string(pattern), std::regex::icase));
    } catch (...) {
        const auto text = ToLower(value);
        const auto needle = ToLower(pattern);
        return text.find(needle) != std::string::npos;
    }
}

bool MatchNumber(uint64_t actual, std::string_view raw, char op) {
    try {
        const auto expected = static_cast<uint64_t>(std::stoull(std::string(raw)));
        if (op == '>') return actual > expected;
        if (op == '<') return actual < expected;
        return actual == expected;
    } catch (...) {
        return false;
    }
}

bool MatchDirection(uint8_t direction, std::string_view pattern) {
    const auto normalized = ToLower(pattern);
    if (normalized == "in" || normalized == "<<<") return direction == BHPLUS_DIR_UP;
    if (normalized == "out" || normalized == ">>>") return direction == BHPLUS_DIR_DOWN;
    return false;
}

std::string BuildPhase(const BHPLUS_CAPTURE_EVENT& event) {
    if (event.TransferType == BHPLUS_USB_TRANSFER_CONTROL) {
        switch (event.Detail.Control.Stage) {
            case BHPLUS_USB_CONTROL_STAGE_SETUP: return "SETUP";
            case BHPLUS_USB_CONTROL_STAGE_DATA: return "DATA";
            case BHPLUS_USB_CONTROL_STAGE_STATUS: return "STATUS";
            case BHPLUS_USB_CONTROL_STAGE_COMPLETE: return "COMPLETE";
            default: return "CONTROL";
        }
    }
    return event.Direction == BHPLUS_DIR_DOWN ? "REQUEST" : "COMPLETE";
}

std::string BuildTransfer(const BHPLUS_CAPTURE_EVENT& event) {
    switch (event.TransferType) {
        case BHPLUS_USB_TRANSFER_ISOCHRONOUS: return "ISO";
        case BHPLUS_USB_TRANSFER_INTERRUPT: return "INT";
        case BHPLUS_USB_TRANSFER_CONTROL: return "CTRL";
        case BHPLUS_USB_TRANSFER_BULK: return "BULK";
        default: return "UNKNOWN";
    }
}

bool MatchText(const BHPLUS_CAPTURE_EVENT& event,
               const DecodedEvent& decoded,
               std::string_view hexData,
               std::string_view pattern) {
    return MatchString(decoded.protocol, pattern) ||
           MatchString(decoded.commandName, pattern) ||
           MatchString(decoded.summary, pattern) ||
           MatchString(BuildPhase(event), pattern) ||
           MatchString(BuildTransfer(event), pattern) ||
           MatchString(hexData, pattern);
}

} // namespace

std::vector<FilterCondition> FilterEngine::Parse(std::string_view filterText) {
    std::vector<FilterCondition> conditions;
    if (filterText.empty()) return conditions;

    std::istringstream iss{std::string(filterText)};
    std::string token;
    while (iss >> std::quoted(token)) {
        const auto colon = token.find(':');
        FilterCondition cond;
        if (colon == std::string::npos) {
            cond.key = "_text";
            cond.value = token;
            conditions.push_back(std::move(cond));
            continue;
        }

        cond.key = ToLower(std::string_view(token).substr(0, colon));
        cond.value = token.substr(colon + 1);
        if (!cond.value.empty() && cond.value.front() == '!') {
            cond.negate = true;
            cond.value.erase(cond.value.begin());
        }
        if (!cond.value.empty() && (cond.value.front() == '>' || cond.value.front() == '<')) {
            cond.op = cond.value.front();
            cond.value.erase(cond.value.begin());
        }
        conditions.push_back(std::move(cond));
    }

    return conditions;
}

bool FilterEngine::Matches(const BHPLUS_CAPTURE_EVENT& event,
                           const DecodedEvent& decoded,
                           std::string_view hexData,
                           const std::vector<FilterCondition>& conditions) {
    for (const auto& cond : conditions) {
        bool match = true;

        if (cond.key == "protocol" || cond.key == "proto") {
            match = MatchString(decoded.protocol, cond.value);
        } else if (cond.key == "command" || cond.key == "cmd") {
            match = MatchString(decoded.commandName.empty() ? decoded.summary : decoded.commandName, cond.value);
        } else if (cond.key == "summary") {
            match = MatchString(decoded.summary, cond.value);
        } else if (cond.key == "status") {
            match = MatchNumber(event.Status, cond.value, cond.op) || MatchString(event.Status == 0 ? "OK" : "ERROR", cond.value);
        } else if (cond.key == "dir" || cond.key == "direction") {
            match = MatchDirection(event.Direction, cond.value);
        } else if (cond.key == "phase") {
            match = MatchString(BuildPhase(event), cond.value);
        } else if (cond.key == "transfer" || cond.key == "type") {
            match = MatchString(BuildTransfer(event), cond.value);
        } else if (cond.key == "len" || cond.key == "length") {
            match = MatchNumber(event.DataLength, cond.value, cond.op);
        } else if (cond.key == "deviceid") {
            const uint32_t deviceId = (static_cast<uint32_t>(event.Bus) << 16) | event.Device;
            match = MatchNumber(deviceId, cond.value, cond.op);
        } else if (cond.key == "device") {
            match = MatchNumber(event.Device, cond.value, cond.op);
        } else if (cond.key == "bus") {
            match = MatchNumber(event.Bus, cond.value, cond.op);
        } else if (cond.key == "endpoint" || cond.key == "ep") {
            match = MatchNumber(event.Endpoint, cond.value, cond.op);
        } else if (cond.key == "seq") {
            match = MatchNumber(event.SequenceNumber, cond.value, cond.op);
        } else if (cond.key == "irp" || cond.key == "irpid") {
            match = MatchNumber(event.IrpId, cond.value, cond.op);
        } else if (cond.key == "data") {
            match = MatchString(hexData, cond.value);
        } else if (cond.key == "_text") {
            match = MatchText(event, decoded, hexData, cond.value);
        }

        if (cond.negate) match = !match;
        if (!match) return false;
    }

    return true;
}

std::string FilterEngine::Describe(const std::vector<FilterCondition>& conditions) {
    std::ostringstream oss;
    for (size_t i = 0; i < conditions.size(); ++i) {
        const auto& cond = conditions[i];
        if (i > 0) oss << " AND ";
        if (cond.key == "_text") {
            oss << '"' << cond.value << '"';
            continue;
        }
        if (cond.negate) oss << "NOT ";
        oss << cond.key;
        oss << (cond.op == '>' || cond.op == '<' ? cond.op : ':');
        oss << cond.value;
    }
    return oss.str();
}

} // namespace bhplus
