/*
 * USBPcapGUI - ATA Protocol Parser (stub)
 */

#include "parser_interface.h"
#include "bhplus_protocol.h"
#include <fmt/format.h>

namespace bhplus {

class AtaParser : public IProtocolParser {
public:
    bool CanParse(BHPLUS_EVENT_TYPE eventType) const override {
        return eventType == BHPLUS_EVENT_ATA_CMD;
    }
    std::string GetName() const override { return "ATA"; }

    DecodedEvent Decode(const BHPLUS_CAPTURE_EVENT& event,
                        const uint8_t* data, uint32_t dataLength) const override {
        DecodedEvent decoded;
        decoded.protocol = "ATA";
        const auto& ata = event.Detail.Ata;

        std::string cmdName;
        switch (ata.Command) {
            case BHPLUS_ATA_IDENTIFY_DEVICE: cmdName = "IDENTIFY DEVICE"; break;
            case BHPLUS_ATA_READ_DMA_EXT:    cmdName = "READ DMA EXT"; break;
            case BHPLUS_ATA_WRITE_DMA_EXT:   cmdName = "WRITE DMA EXT"; break;
            case BHPLUS_ATA_READ_FPDMA:      cmdName = "READ FPDMA QUEUED"; break;
            case BHPLUS_ATA_WRITE_FPDMA:     cmdName = "WRITE FPDMA QUEUED"; break;
            case BHPLUS_ATA_SMART:           cmdName = "SMART"; break;
            case BHPLUS_ATA_SET_FEATURES:    cmdName = "SET FEATURES"; break;
            default: cmdName = fmt::format("CMD(0x{:02X})", ata.Command); break;
        }

        decoded.commandName = cmdName;
        decoded.summary = fmt::format("ATA {} LBA={} Count={}", cmdName, ata.Lba, ata.SectorCount);
        decoded.fields.push_back({"Command", fmt::format("0x{:02X} ({})", ata.Command, cmdName), "", 0, 1});
        decoded.fields.push_back({"LBA", fmt::format("{}", ata.Lba), "", 0, 8});
        decoded.fields.push_back({"Sector Count", fmt::format("{}", ata.SectorCount), "", 0, 2});
        return decoded;
    }
};

static struct AtaParserRegistrar {
    AtaParserRegistrar() {
        ParserRegistry::Instance().Register(std::make_unique<AtaParser>());
    }
} s_ataParserRegistrar;

} // namespace bhplus
