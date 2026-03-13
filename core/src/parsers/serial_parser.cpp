/*
 * USBPcapGUI - Serial Protocol Parser (stub)
 */

#include "parser_interface.h"
#include <fmt/format.h>

namespace bhplus {

class SerialParser : public IProtocolParser {
public:
    bool CanParse(BHPLUS_EVENT_TYPE eventType) const override {
        return eventType == BHPLUS_EVENT_SERIAL_TX || eventType == BHPLUS_EVENT_SERIAL_RX;
    }
    std::string GetName() const override { return "Serial"; }

    DecodedEvent Decode(const BHPLUS_CAPTURE_EVENT& event,
                        const uint8_t* data, uint32_t dataLength) const override {
        DecodedEvent decoded;
        decoded.protocol = "Serial";
        
        bool isTx = (event.EventType == BHPLUS_EVENT_SERIAL_TX);
        decoded.summary = fmt::format("Serial {} {} bytes", isTx ? "TX" : "RX", dataLength);
        decoded.fields.push_back({"Direction", isTx ? "TX" : "RX", "", 0, 0});
        decoded.fields.push_back({"Length", fmt::format("{}", dataLength), "", 0, 0});

        const auto& serial = event.Detail.Serial;
        if (serial.BaudRate > 0) {
            decoded.fields.push_back({"Baud Rate", fmt::format("{}", serial.BaudRate), "", 0, 0});
        }

        return decoded;
    }
};

static struct SerialParserRegistrar {
    SerialParserRegistrar() {
        ParserRegistry::Instance().Register(std::make_unique<SerialParser>());
    }
} s_serialParserRegistrar;

} // namespace bhplus
