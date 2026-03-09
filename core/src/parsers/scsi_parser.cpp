/*
 * USBPcapGUI - SCSI Protocol Parser
 */

#include "parser_interface.h"
#include "bhplus_protocol.h"
#include <fmt/format.h>

namespace bhplus {

class ScsiParser : public IProtocolParser {
public:
    bool CanParse(BHPLUS_EVENT_TYPE eventType) const override {
        return eventType == BHPLUS_EVENT_SCSI_CDB;
    }

    std::string GetName() const override { return "SCSI"; }

    DecodedEvent Decode(const BHPLUS_CAPTURE_EVENT& event,
                        const uint8_t* data,
                        uint32_t dataLength) const override {
        DecodedEvent decoded;
        decoded.protocol = "SCSI";

        const auto& scsi = event.Detail.Scsi;
        
        std::string opName = GetOpcodeName(scsi.Cdb[0]);
        decoded.commandName = opName;
        decoded.summary = fmt::format("SCSI {} (0x{:02X}) CDB[{}]", 
            opName, scsi.Cdb[0], scsi.CdbLength);

        // CDB fields
        decoded.fields.push_back({"Opcode", fmt::format("0x{:02X} ({})", scsi.Cdb[0], opName), "", 0, 1});
        decoded.fields.push_back({"CDB Length", fmt::format("{}", scsi.CdbLength), "", 0, 0});
        
        // Format full CDB
        std::string cdbHex;
        for (int i = 0; i < scsi.CdbLength && i < 16; i++) {
            if (i > 0) cdbHex += " ";
            cdbHex += fmt::format("{:02X}", scsi.Cdb[i]);
        }
        decoded.fields.push_back({"CDB", cdbHex, "", 0, scsi.CdbLength});

        // SCSI Status
        std::string statusName;
        switch (scsi.ScsiStatus) {
            case 0x00: statusName = "GOOD"; break;
            case 0x02: statusName = "CHECK CONDITION"; break;
            case 0x08: statusName = "BUSY"; break;
            default: statusName = fmt::format("0x{:02X}", scsi.ScsiStatus); break;
        }
        decoded.fields.push_back({"SCSI Status", statusName, "", 0, 0});

        return decoded;
    }

private:
    static std::string GetOpcodeName(uint8_t opcode) {
        switch (opcode) {
            case BHPLUS_SCSI_TEST_UNIT_READY:   return "TEST UNIT READY";
            case BHPLUS_SCSI_REQUEST_SENSE:     return "REQUEST SENSE";
            case BHPLUS_SCSI_INQUIRY:           return "INQUIRY";
            case BHPLUS_SCSI_MODE_SELECT_6:     return "MODE SELECT(6)";
            case BHPLUS_SCSI_MODE_SENSE_6:      return "MODE SENSE(6)";
            case BHPLUS_SCSI_READ_CAPACITY_10:  return "READ CAPACITY(10)";
            case BHPLUS_SCSI_READ_10:           return "READ(10)";
            case BHPLUS_SCSI_WRITE_10:          return "WRITE(10)";
            case BHPLUS_SCSI_READ_16:           return "READ(16)";
            case BHPLUS_SCSI_WRITE_16:          return "WRITE(16)";
            case BHPLUS_SCSI_READ_CAPACITY_16:  return "READ CAPACITY(16)";
            default: return fmt::format("OP(0x{:02X})", opcode);
        }
    }
};

static struct ScsiParserRegistrar {
    ScsiParserRegistrar() {
        ParserRegistry::Instance().Register(std::make_unique<ScsiParser>());
    }
} s_scsiParserRegistrar;

} // namespace bhplus
