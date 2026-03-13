/*
 * USBPcapGUI - USB Protocol Parser
 */

#include "parser_interface.h"
#include "bhplus_protocol.h"
#include <fmt/format.h>

namespace bhplus {

class UsbParser : public IProtocolParser {
public:
    bool CanParse(BHPLUS_EVENT_TYPE eventType) const override {
        return eventType == BHPLUS_EVENT_URB_DOWN || eventType == BHPLUS_EVENT_URB_UP;
    }

    std::string GetName() const override { return "USB"; }

    DecodedEvent Decode(const BHPLUS_CAPTURE_EVENT& event,
                        const uint8_t* data,
                        uint32_t dataLength) const override {
        DecodedEvent decoded;
        decoded.protocol = "USB";

        // Use top-level event fields (Detail.Urb was removed in struct refactor)
        bool isDown = (event.EventType == BHPLUS_EVENT_URB_DOWN);
        std::string direction = isDown ? ">>>" : "<<<";

        // Transfer type
        std::string transferType;
        switch (event.TransferType) {
            case BHPLUS_USB_TRANSFER_CONTROL:     transferType = "CTRL"; break;
            case BHPLUS_USB_TRANSFER_BULK:        transferType = "BULK"; break;
            case BHPLUS_USB_TRANSFER_INTERRUPT:   transferType = "INT";  break;
            case BHPLUS_USB_TRANSFER_ISOCHRONOUS: transferType = "ISO";  break;
            default: transferType = fmt::format("?{}", event.TransferType); break;
        }

        // Endpoint
        uint8_t epNum = event.Endpoint & 0x0F;
        bool epIn = (event.Endpoint & 0x80) != 0;
        std::string endpoint = fmt::format("EP{} {}", epNum, epIn ? "IN" : "OUT");

        // Summary
        decoded.summary = fmt::format("{} {} {} Len={}",
            direction, transferType, endpoint, event.DataLength);

        // Decode fields
        decoded.fields.push_back({"Direction", direction, "", 0, 0});
        decoded.fields.push_back({"Transfer Type", transferType, "", 0, 0});
        decoded.fields.push_back({"Endpoint", endpoint, "", 0, 0});
        decoded.fields.push_back({"URB Function", fmt::format("0x{:04X}", event.UrbFunction), "", 0, 0});
        decoded.fields.push_back({"Transfer Length", fmt::format("{}", event.DataLength), "", 0, 0});

        // For control transfers, decode setup packet
        if (event.TransferType == BHPLUS_USB_TRANSFER_CONTROL) {
            // Use setup packet from Detail.Control if in SETUP stage
            if (event.Detail.Control.Stage == BHPLUS_USB_CONTROL_STAGE_SETUP) {
                DecodeSetupPacket(decoded,
                    event.Detail.Control.SetupPacket, 8);
            } else if (data && dataLength >= 8) {
                DecodeSetupPacket(decoded, data, dataLength);
            }
        }

        // Status for completions
        if (!isDown) {
            decoded.fields.push_back({"Status", fmt::format("0x{:08X}", event.Status), 
                event.Status == 0 ? "Success" : "Error", 0, 0});
        }

        return decoded;
    }

private:
    void DecodeSetupPacket(DecodedEvent& decoded, const uint8_t* data, uint32_t dataLen) const {
        if (!data || dataLen < 8) return;
        uint8_t bmRequestType = data[0];
        uint8_t bRequest = data[1];
        uint16_t wValue = data[2] | (data[3] << 8);
        uint16_t wIndex = data[4] | (data[5] << 8);
        uint16_t wLength = data[6] | (data[7] << 8);

        // Request type direction
        std::string reqDir = (bmRequestType & 0x80) ? "Device-to-Host" : "Host-to-Device";
        
        // Request type
        std::string reqType;
        switch ((bmRequestType >> 5) & 0x03) {
            case 0: reqType = "Standard"; break;
            case 1: reqType = "Class"; break;
            case 2: reqType = "Vendor"; break;
            default: reqType = "Reserved"; break;
        }

        // Standard request name
        std::string reqName;
        if (((bmRequestType >> 5) & 0x03) == 0) { // Standard
            switch (bRequest) {
                case BHPLUS_USB_REQ_GET_STATUS:        reqName = "GET_STATUS"; break;
                case BHPLUS_USB_REQ_CLEAR_FEATURE:     reqName = "CLEAR_FEATURE"; break;
                case BHPLUS_USB_REQ_SET_FEATURE:       reqName = "SET_FEATURE"; break;
                case BHPLUS_USB_REQ_SET_ADDRESS:       reqName = "SET_ADDRESS"; break;
                case BHPLUS_USB_REQ_GET_DESCRIPTOR:    reqName = "GET_DESCRIPTOR"; break;
                case BHPLUS_USB_REQ_SET_DESCRIPTOR:    reqName = "SET_DESCRIPTOR"; break;
                case BHPLUS_USB_REQ_GET_CONFIGURATION: reqName = "GET_CONFIGURATION"; break;
                case BHPLUS_USB_REQ_SET_CONFIGURATION: reqName = "SET_CONFIGURATION"; break;
                case BHPLUS_USB_REQ_GET_INTERFACE:     reqName = "GET_INTERFACE"; break;
                case BHPLUS_USB_REQ_SET_INTERFACE:     reqName = "SET_INTERFACE"; break;
                default: reqName = fmt::format("Request(0x{:02X})", bRequest); break;
            }
        } else {
            reqName = fmt::format("Request(0x{:02X})", bRequest);
        }

        decoded.commandName = reqName;
        decoded.summary += " " + reqName;

        decoded.fields.push_back({"bmRequestType", fmt::format("0x{:02X}", bmRequestType),
            reqDir + " / " + reqType, 0, 1});
        decoded.fields.push_back({"bRequest", fmt::format("0x{:02X} ({})", bRequest, reqName), "", 1, 1});
        decoded.fields.push_back({"wValue", fmt::format("0x{:04X}", wValue), "", 2, 2});
        decoded.fields.push_back({"wIndex", fmt::format("0x{:04X}", wIndex), "", 4, 2});
        decoded.fields.push_back({"wLength", fmt::format("{}", wLength), "", 6, 2});

        // Decode descriptor type for GET_DESCRIPTOR
        if (bRequest == BHPLUS_USB_REQ_GET_DESCRIPTOR) {
            uint8_t descType = (wValue >> 8) & 0xFF;
            std::string descName;
            switch (descType) {
                case BHPLUS_USB_DESC_DEVICE:        descName = "DEVICE"; break;
                case BHPLUS_USB_DESC_CONFIGURATION: descName = "CONFIGURATION"; break;
                case BHPLUS_USB_DESC_STRING:        descName = "STRING"; break;
                case BHPLUS_USB_DESC_INTERFACE:     descName = "INTERFACE"; break;
                case BHPLUS_USB_DESC_ENDPOINT:      descName = "ENDPOINT"; break;
                case BHPLUS_USB_DESC_HID:           descName = "HID"; break;
                case BHPLUS_USB_DESC_HID_REPORT:    descName = "HID_REPORT"; break;
                default: descName = fmt::format("Type(0x{:02X})", descType); break;
            }
            decoded.fields.push_back({"Descriptor Type", descName, "", 3, 1});
        }
    }
};

// Auto-register
static struct UsbParserRegistrar {
    UsbParserRegistrar() {
        ParserRegistry::Instance().Register(std::make_unique<UsbParser>());
    }
} s_usbParserRegistrar;

} // namespace bhplus
