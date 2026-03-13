/*
 * USBPcapGUI - Parser Unit Tests
 */

#include <gtest/gtest.h>
#include "parser_interface.h"
#include "bhplus_types.h"
#include "bhplus_protocol.h"
#include <cstring>

using namespace bhplus;

TEST(UsbParser, CanParseUrbEvents) {
    auto* parser = ParserRegistry::Instance().FindParser(BHPLUS_EVENT_URB_DOWN);
    ASSERT_NE(parser, nullptr);
    EXPECT_EQ(parser->GetName(), "USB");

    parser = ParserRegistry::Instance().FindParser(BHPLUS_EVENT_URB_UP);
    ASSERT_NE(parser, nullptr);
    EXPECT_EQ(parser->GetName(), "USB");
}

TEST(UsbParser, DecodeControlTransfer) {
    BHPLUS_CAPTURE_EVENT event = {};
    event.EventType    = BHPLUS_EVENT_URB_DOWN;
    event.UrbFunction  = 0x0008; // URB_FUNCTION_CONTROL_TRANSFER
    event.TransferType = BHPLUS_USB_TRANSFER_CONTROL;
    event.Endpoint     = 0x00;
    event.DataLength   = 18;
    event.Direction    = BHPLUS_DIR_DOWN;

    // GET_DESCRIPTOR (Device) setup packet
    uint8_t setupPacket[] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00 };
    event.Detail.Control.Stage = BHPLUS_USB_CONTROL_STAGE_SETUP;
    std::memcpy(event.Detail.Control.SetupPacket, setupPacket, 8);

    auto decoded = ParserRegistry::Instance().Decode(event, setupPacket, sizeof(setupPacket));
    EXPECT_EQ(decoded.protocol, "USB");
    EXPECT_FALSE(decoded.summary.empty());
    EXPECT_TRUE(decoded.summary.find("CTRL") != std::string::npos);
}

TEST(ScsiParser, CanParseScsiEvents) {
    auto* parser = ParserRegistry::Instance().FindParser(BHPLUS_EVENT_SCSI_CDB);
    ASSERT_NE(parser, nullptr);
    EXPECT_EQ(parser->GetName(), "SCSI");
}

TEST(ScsiParser, DecodeInquiry) {
    BHPLUS_CAPTURE_EVENT event = {};
    event.EventType = BHPLUS_EVENT_SCSI_CDB;
    event.Detail.Scsi.Cdb[0] = BHPLUS_SCSI_INQUIRY;
    event.Detail.Scsi.CdbLength = 6;
    event.Detail.Scsi.ScsiStatus = 0;

    auto decoded = ParserRegistry::Instance().Decode(event, nullptr, 0);
    EXPECT_EQ(decoded.protocol, "SCSI");
    EXPECT_EQ(decoded.commandName, "INQUIRY");
}

TEST(NvmeParser, CanParseNvmeEvents) {
    auto* parser = ParserRegistry::Instance().FindParser(BHPLUS_EVENT_NVME_ADMIN);
    ASSERT_NE(parser, nullptr);
    EXPECT_EQ(parser->GetName(), "NVMe");
}

TEST(NvmeParser, DecodeIdentify) {
    BHPLUS_CAPTURE_EVENT event = {};
    event.EventType = BHPLUS_EVENT_NVME_ADMIN;
    event.Detail.Nvme.Opcode = BHPLUS_NVME_ADMIN_IDENTIFY;
    event.Detail.Nvme.NSID = 1;

    auto decoded = ParserRegistry::Instance().Decode(event, nullptr, 0);
    EXPECT_EQ(decoded.protocol, "NVMe");
    EXPECT_EQ(decoded.commandName, "Identify");
}

TEST(ParserRegistry, UnknownEventReturnsGeneric) {
    BHPLUS_CAPTURE_EVENT event = {};
    event.EventType = BHPLUS_EVENT_NONE;

    auto decoded = ParserRegistry::Instance().Decode(event, nullptr, 0);
    EXPECT_EQ(decoded.protocol, "Unknown");
}
