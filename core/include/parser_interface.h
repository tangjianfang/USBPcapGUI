#pragma once

/*
 * USBPcapGUI - Parser Interface
 * Base class for all protocol parsers.
 */

#include "bhplus_types.h"
#include <string>
#include <vector>
#include <memory>

namespace bhplus {

// A decoded field within a protocol event
struct DecodedField {
    std::string name;       // Field name (e.g., "bRequest")
    std::string value;      // Formatted value (e.g., "GET_DESCRIPTOR (0x06)")
    std::string description;// Optional description
    uint32_t    offset;     // Byte offset in raw data
    uint32_t    length;     // Byte length of field
};

// Full decoded protocol event
struct DecodedEvent {
    std::string summary;                // One-line summary
    std::string protocol;               // Protocol name (e.g., "USB", "NVMe")
    std::string commandName;            // Command name (e.g., "GET_DESCRIPTOR")
    std::vector<DecodedField> fields;   // Decoded fields
};

// Abstract parser interface
class IProtocolParser {
public:
    virtual ~IProtocolParser() = default;
    
    // Check if this parser can handle the given event type
    virtual bool CanParse(BHPLUS_EVENT_TYPE eventType) const = 0;
    
    // Decode an event into human-readable form
    virtual DecodedEvent Decode(const BHPLUS_CAPTURE_EVENT& event,
                                 const uint8_t* data,
                                 uint32_t dataLength) const = 0;
    
    // Get parser name
    virtual std::string GetName() const = 0;
};

// Parser registry
class ParserRegistry {
public:
    static ParserRegistry& Instance();
    
    void Register(std::unique_ptr<IProtocolParser> parser);
    
    const IProtocolParser* FindParser(BHPLUS_EVENT_TYPE eventType) const;
    
    DecodedEvent Decode(const BHPLUS_CAPTURE_EVENT& event,
                        const uint8_t* data,
                        uint32_t dataLength) const;

private:
    std::vector<std::unique_ptr<IProtocolParser>> m_parsers;
};

} // namespace bhplus
