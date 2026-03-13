/*
 * USBPcapGUI - NVMe Protocol Parser
 */

#include "parser_interface.h"
#include "bhplus_protocol.h"
#include <fmt/format.h>

namespace bhplus {

class NvmeParser : public IProtocolParser {
public:
    bool CanParse(BHPLUS_EVENT_TYPE eventType) const override {
        return eventType == BHPLUS_EVENT_NVME_ADMIN || eventType == BHPLUS_EVENT_NVME_IO;
    }

    std::string GetName() const override { return "NVMe"; }

    DecodedEvent Decode(const BHPLUS_CAPTURE_EVENT& event,
                        const uint8_t* data,
                        uint32_t dataLength) const override {
        DecodedEvent decoded;
        decoded.protocol = "NVMe";

        const auto& nvme = event.Detail.Nvme;
        bool isAdmin = (event.EventType == BHPLUS_EVENT_NVME_ADMIN);
        
        std::string opName = isAdmin ? GetAdminOpName(nvme.Opcode) : GetIoOpName(nvme.Opcode);
        decoded.commandName = opName;
        decoded.summary = fmt::format("NVMe {} {} NSID={}", 
            isAdmin ? "Admin" : "IO", opName, nvme.NSID);

        decoded.fields.push_back({"Command Set", isAdmin ? "Admin" : "I/O", "", 0, 0});
        decoded.fields.push_back({"Opcode", fmt::format("0x{:02X} ({})", nvme.Opcode, opName), "", 0, 1});
        decoded.fields.push_back({"NSID", fmt::format("{}", nvme.NSID), "", 0, 4});
        decoded.fields.push_back({"Status", fmt::format("0x{:08X}", event.Status), 
            event.Status == 0 ? "Success" : "Error", 0, 0});

        return decoded;
    }

private:
    static std::string GetAdminOpName(uint8_t opcode) {
        switch (opcode) {
            case BHPLUS_NVME_ADMIN_DELETE_SQ:    return "Delete SQ";
            case BHPLUS_NVME_ADMIN_CREATE_SQ:    return "Create SQ";
            case BHPLUS_NVME_ADMIN_GET_LOG_PAGE: return "Get Log Page";
            case BHPLUS_NVME_ADMIN_DELETE_CQ:    return "Delete CQ";
            case BHPLUS_NVME_ADMIN_CREATE_CQ:    return "Create CQ";
            case BHPLUS_NVME_ADMIN_IDENTIFY:     return "Identify";
            case BHPLUS_NVME_ADMIN_ABORT:        return "Abort";
            case BHPLUS_NVME_ADMIN_SET_FEATURES: return "Set Features";
            case BHPLUS_NVME_ADMIN_GET_FEATURES: return "Get Features";
            case BHPLUS_NVME_ADMIN_ASYNC_EVENT:  return "Async Event";
            case BHPLUS_NVME_ADMIN_FW_COMMIT:    return "FW Commit";
            case BHPLUS_NVME_ADMIN_FW_DOWNLOAD:  return "FW Download";
            default: return fmt::format("Admin(0x{:02X})", opcode);
        }
    }

    static std::string GetIoOpName(uint8_t opcode) {
        switch (opcode) {
            case BHPLUS_NVME_IO_FLUSH:        return "Flush";
            case BHPLUS_NVME_IO_WRITE:        return "Write";
            case BHPLUS_NVME_IO_READ:         return "Read";
            case BHPLUS_NVME_IO_COMPARE:      return "Compare";
            case BHPLUS_NVME_IO_DATASET_MGMT: return "Dataset Mgmt";
            default: return fmt::format("IO(0x{:02X})", opcode);
        }
    }
};

static struct NvmeParserRegistrar {
    NvmeParserRegistrar() {
        ParserRegistry::Instance().Register(std::make_unique<NvmeParser>());
    }
} s_nvmeParserRegistrar;

} // namespace bhplus
