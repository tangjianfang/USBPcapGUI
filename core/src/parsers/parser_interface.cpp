/*
 * USBPcapGUI - Parser Registry Implementation
 */

#include "parser_interface.h"
#include <spdlog/spdlog.h>

namespace bhplus {

ParserRegistry& ParserRegistry::Instance() {
    static ParserRegistry instance;
    return instance;
}

void ParserRegistry::Register(std::unique_ptr<IProtocolParser> parser) {
    spdlog::info("Registered parser: {}", parser->GetName());
    m_parsers.push_back(std::move(parser));
}

const IProtocolParser* ParserRegistry::FindParser(BHPLUS_EVENT_TYPE eventType) const {
    for (const auto& parser : m_parsers) {
        if (parser->CanParse(eventType))
            return parser.get();
    }
    return nullptr;
}

DecodedEvent ParserRegistry::Decode(const BHPLUS_CAPTURE_EVENT& event,
                                     const uint8_t* data,
                                     uint32_t dataLength) const {
    const auto* parser = FindParser(event.EventType);
    if (parser) {
        return parser->Decode(event, data, dataLength);
    }

    // Fallback: generic decode
    DecodedEvent decoded;
    decoded.protocol = "Unknown";
    decoded.summary = "Unknown event type";
    return decoded;
}

} // namespace bhplus
