/*
 * USBPcapGUI - capture.start param mapping unit tests
 *
 * Validates every field in BHPLUS_CAPTURE_CONFIG is correctly populated
 * by ParseCaptureStartParams() from a JSON params string, covering:
 *   - defaults when keys are absent
 *   - explicit values for all scalar fields
 *   - captureAll / captureNew / injectDescriptors bool flags
 *   - deviceIds array → FilterDeviceCount + FilterDeviceAddresses
 *   - deviceId single-device shorthand
 *   - filterBus hub selection
 *   - legacy maxDataPerEvent alias
 *   - BHPLUS_MAX_FILTER_DEVICES clamp
 *   - malformed JSON throws
 */

#include <gtest/gtest.h>
#include "capture_config_parser.h"
#include "bhplus_types.h"
#include <nlohmann/json.hpp>

using namespace bhplus;

// ── helpers ─────────────────────────────────────────────────────────────────

static BHPLUS_CAPTURE_CONFIG parse(const char* json) {
    return ParseCaptureStartParams(json);
}

// ── default values ────────────────────────────────────────────────────────────

TEST(CaptureConfigParser, EmptyJsonYieldsAllDefaults) {
    auto c = parse("{}");
    EXPECT_EQ(c.SnapshotLength,    65535u);
    EXPECT_EQ(c.BufferLength,      1048576u);
    EXPECT_EQ(c.MaxEvents,         100000u);
    EXPECT_EQ(c.CaptureData,       1);
    EXPECT_EQ(c.CaptureAllDevices, 1);
    EXPECT_EQ(c.CaptureNewDevices, 1);
    EXPECT_EQ(c.InjectDescriptors, 1);
    EXPECT_EQ(c.FilterBus,         0);
    EXPECT_EQ(c.FilterDeviceCount, 0u);
}

TEST(CaptureConfigParser, ZeroSnapshotLenFallsBackToDefault) {
    auto c = parse(R"({"snapshotLen": 0})");
    EXPECT_EQ(c.SnapshotLength, 65535u);
}

// ── explicit scalar fields ────────────────────────────────────────────────────

TEST(CaptureConfigParser, ExplicitSnapshotAndBufferLen) {
    auto c = parse(R"({"snapshotLen": 512, "bufferLen": 2097152})");
    EXPECT_EQ(c.SnapshotLength, 512u);
    EXPECT_EQ(c.BufferLength,   2097152u);
}

TEST(CaptureConfigParser, ExplicitMaxEvents) {
    auto c = parse(R"({"maxEvents": 5000})");
    EXPECT_EQ(c.MaxEvents, 5000u);
}

TEST(CaptureConfigParser, ExplicitFilterBus) {
    auto c = parse(R"({"filterBus": 3})");
    EXPECT_EQ(c.FilterBus, 3);
}

// ── bool flags ────────────────────────────────────────────────────────────────

TEST(CaptureConfigParser, CaptureAllFalse) {
    auto c = parse(R"({"captureAll": false})");
    EXPECT_EQ(c.CaptureAllDevices, 0);
}

TEST(CaptureConfigParser, CaptureAllTrue) {
    auto c = parse(R"({"captureAll": true})");
    EXPECT_EQ(c.CaptureAllDevices, 1);
}

TEST(CaptureConfigParser, CaptureNewFalse) {
    auto c = parse(R"({"captureNew": false})");
    EXPECT_EQ(c.CaptureNewDevices, 0);
}

TEST(CaptureConfigParser, CaptureNewTrue) {
    auto c = parse(R"({"captureNew": true})");
    EXPECT_EQ(c.CaptureNewDevices, 1);
}

TEST(CaptureConfigParser, InjectDescriptorsFalse) {
    auto c = parse(R"({"injectDescriptors": false})");
    EXPECT_EQ(c.InjectDescriptors, 0);
}

TEST(CaptureConfigParser, InjectDescriptorsTrue) {
    auto c = parse(R"({"injectDescriptors": true})");
    EXPECT_EQ(c.InjectDescriptors, 1);
}

TEST(CaptureConfigParser, CaptureDataFalse) {
    auto c = parse(R"({"captureData": false})");
    EXPECT_EQ(c.CaptureData, 0);
}

// ── device filter: deviceIds array ────────────────────────────────────────────

TEST(CaptureConfigParser, DeviceIdsPopulatesFilter) {
    auto c = parse(R"({"deviceIds": [3, 7, 15]})");
    EXPECT_EQ(c.FilterDeviceCount, 3u);
    EXPECT_EQ(c.FilterDeviceAddresses[0], 3);
    EXPECT_EQ(c.FilterDeviceAddresses[1], 7);
    EXPECT_EQ(c.FilterDeviceAddresses[2], 15);
}

TEST(CaptureConfigParser, EmptyDeviceIdsArrayLeavesCountZero) {
    auto c = parse(R"({"deviceIds": []})");
    EXPECT_EQ(c.FilterDeviceCount, 0u);
}

TEST(CaptureConfigParser, DeviceIdsSingleEntry) {
    auto c = parse(R"({"deviceIds": [42]})");
    EXPECT_EQ(c.FilterDeviceCount, 1u);
    EXPECT_EQ(c.FilterDeviceAddresses[0], 42);
}

// ── device filter: deviceId single shorthand ──────────────────────────────────

TEST(CaptureConfigParser, SingleDeviceIdShorthand) {
    auto c = parse(R"({"deviceId": 9})");
    EXPECT_EQ(c.FilterDeviceCount, 1u);
    EXPECT_EQ(c.FilterDeviceAddresses[0], 9);
}

TEST(CaptureConfigParser, DeviceIdsPreferredOverDeviceId) {
    // When both are present, deviceIds takes precedence (it is checked first)
    auto c = parse(R"({"deviceIds": [1, 2], "deviceId": 99})");
    EXPECT_EQ(c.FilterDeviceCount, 2u);
    EXPECT_EQ(c.FilterDeviceAddresses[0], 1);
    EXPECT_EQ(c.FilterDeviceAddresses[1], 2);
}

// ── BHPLUS_MAX_FILTER_DEVICES clamp ───────────────────────────────────────────

TEST(CaptureConfigParser, DeviceIdsClampedToMax) {
    // Build a JSON array larger than BHPLUS_MAX_FILTER_DEVICES (64)
    std::string json = R"({"deviceIds": [)";
    for (int i = 1; i <= 80; ++i) {
        if (i > 1) json += ',';
        json += std::to_string(i);
    }
    json += "]}";

    auto c = parse(json.c_str());
    EXPECT_EQ(c.FilterDeviceCount, static_cast<uint32_t>(BHPLUS_MAX_FILTER_DEVICES));
    // First and last-kept entry
    EXPECT_EQ(c.FilterDeviceAddresses[0],  1);
    EXPECT_EQ(c.FilterDeviceAddresses[BHPLUS_MAX_FILTER_DEVICES - 1],
              static_cast<UINT16>(BHPLUS_MAX_FILTER_DEVICES));
}

// ── legacy alias ──────────────────────────────────────────────────────────────

TEST(CaptureConfigParser, LegacyMaxDataPerEventAliasUsedWhenSnapshotLenAbsent) {
    auto c = parse(R"({"maxDataPerEvent": 1024})");
    EXPECT_EQ(c.SnapshotLength, 1024u);
}

TEST(CaptureConfigParser, SnapshotLenTakesPrecedenceOverLegacyAlias) {
    // snapshotLen > 0 means the legacy key is ignored
    auto c = parse(R"({"snapshotLen": 256, "maxDataPerEvent": 99999})");
    EXPECT_EQ(c.SnapshotLength, 256u);
}

// ── combined realistic payload ────────────────────────────────────────────────

TEST(CaptureConfigParser, FullPayloadFromInterfaceOptionsDialog) {
    const char* payload = R"({
        "snapshotLen": 65535,
        "bufferLen": 1048576,
        "captureAll": true,
        "captureNew": false,
        "injectDescriptors": false,
        "captureData": true,
        "filterBus": 1,
        "deviceIds": [4, 8]
    })";

    auto c = parse(payload);
    EXPECT_EQ(c.SnapshotLength,    65535u);
    EXPECT_EQ(c.BufferLength,      1048576u);
    EXPECT_EQ(c.CaptureAllDevices, 1);
    EXPECT_EQ(c.CaptureNewDevices, 0);
    EXPECT_EQ(c.InjectDescriptors, 0);
    EXPECT_EQ(c.CaptureData,       1);
    EXPECT_EQ(c.FilterBus,         1);
    EXPECT_EQ(c.FilterDeviceCount, 2u);
    EXPECT_EQ(c.FilterDeviceAddresses[0], 4);
    EXPECT_EQ(c.FilterDeviceAddresses[1], 8);
}

// ── error handling ────────────────────────────────────────────────────────────

TEST(CaptureConfigParser, MalformedJsonThrows) {
    EXPECT_THROW(parse("{not valid json"), nlohmann::json::exception);
}

TEST(CaptureConfigParser, EmptyStringThrows) {
    EXPECT_THROW(parse(""), nlohmann::json::exception);
}
