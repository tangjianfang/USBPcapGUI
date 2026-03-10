/*
 * USBPcapGUI - USB action helper tests
 */

#include <gtest/gtest.h>
#include "usb_actions.h"

using namespace bhplus;

TEST(UsbActions, DecodeHexStringAcceptsWhitespaceAndDelimiters) {
    std::vector<uint8_t> bytes;
    std::string error;
    ASSERT_TRUE(DecodeHexString("de ad:be-ef", bytes, &error));
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(bytes[0], 0xde);
    EXPECT_EQ(bytes[1], 0xad);
    EXPECT_EQ(bytes[2], 0xbe);
    EXPECT_EQ(bytes[3], 0xef);
}

TEST(UsbActions, DecodeHexStringRejectsInvalidInput) {
    std::vector<uint8_t> bytes;
    std::string error;
    EXPECT_FALSE(DecodeHexString("zz", bytes, &error));
    EXPECT_FALSE(error.empty());
}

TEST(UsbActions, ControlTransferDirectionIsDetected) {
    UsbControlTransferSetup inSetup{};
    inSetup.requestType = 0x80;
    EXPECT_TRUE(IsUsbControlTransferIn(inSetup));

    UsbControlTransferSetup outSetup{};
    outSetup.requestType = 0x00;
    EXPECT_FALSE(IsUsbControlTransferIn(outSetup));
}
