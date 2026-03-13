/*
 * USBPcapGUI - Core utility unit tests
 */

#include <gtest/gtest.h>
#include "export_engine.h"
#include "filter_engine.h"
#include "trigger_engine.h"

using namespace bhplus;

namespace {

ExportRecord MakeRecord() {
    ExportRecord record;
    record.event.SequenceNumber = 42;
    record.event.Timestamp = 1'710'000'000'123'456ULL;
    record.event.EventType = BHPLUS_EVENT_URB_DOWN;
    record.event.Bus = 2;
    record.event.Device = 5;
    record.event.Endpoint = 1;
    record.event.TransferType = BHPLUS_USB_TRANSFER_CONTROL;
    record.event.Direction = BHPLUS_DIR_DOWN;
    record.event.UrbFunction = 0x0008;
    record.event.Status = 0;
    record.event.DataLength = 4;
    record.event.Detail.Control.Stage = BHPLUS_USB_CONTROL_STAGE_SETUP;
    record.event.IrpId = 0x1234;
    record.payload = { 0xde, 0xad, 0xbe, 0xef };
    record.decoded.protocol = "USB";
    record.decoded.commandName = "GET_DESCRIPTOR";
    record.decoded.summary = "CTRL GET_DESCRIPTOR";
    return record;
}

} // namespace

TEST(FilterEngine, ParsesFilterExpression) {
    const auto conditions = FilterEngine::Parse("protocol:usb len:>2 status:!stall \"descriptor\"");
    ASSERT_EQ(conditions.size(), 4u);
    EXPECT_EQ(conditions[0].key, "protocol");
    EXPECT_EQ(conditions[1].op, '>');
    EXPECT_TRUE(conditions[2].negate);
    EXPECT_EQ(conditions[3].key, "_text");
}

TEST(FilterEngine, MatchesDecodedEventAndPayload) {
    const auto record = MakeRecord();
    const auto conditions = FilterEngine::Parse("protocol:usb cmd:descriptor data:dead len:4 dir:out");
    EXPECT_TRUE(FilterEngine::Matches(
        record.event,
        record.decoded,
        ExportEngine::ToHex(record.payload.data(), record.payload.size()),
        conditions));
}

TEST(TriggerEngine, ReturnsActionForFirstMatchingRule) {
    TriggerEngine engine;
    TriggerRule rule;
    rule.name = "Stop on setup";
    rule.conditions = FilterEngine::Parse("phase:setup");
    rule.action = TriggerAction::StopCapture;
    engine.SetRules({rule});

    const auto record = MakeRecord();
    const auto action = engine.Evaluate(
        record.event,
        record.decoded,
        ExportEngine::ToHex(record.payload.data(), record.payload.size()));

    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(*action, TriggerAction::StopCapture);
}

TEST(ExportEngine, WritesJsonAndPcap) {
    const auto record = MakeRecord();
    const std::vector<ExportRecord> records{record};

    std::ostringstream jsonOut;
    ASSERT_TRUE(ExportEngine::Write(jsonOut, ExportFormat::Json, records));
    EXPECT_NE(jsonOut.str().find("GET_DESCRIPTOR"), std::string::npos);

    std::ostringstream pcapOut(std::ios::binary);
    ASSERT_TRUE(ExportEngine::Write(pcapOut, ExportFormat::Pcap, records));
    const auto bytes = pcapOut.str();
    ASSERT_GE(bytes.size(), 24u);
    EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0xd4u);
    EXPECT_EQ(static_cast<unsigned char>(bytes[1]), 0xc3u);
    EXPECT_EQ(static_cast<unsigned char>(bytes[2]), 0xb2u);
    EXPECT_EQ(static_cast<unsigned char>(bytes[3]), 0xa1u);
}
