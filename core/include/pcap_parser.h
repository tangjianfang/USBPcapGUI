#pragma once
/*
 * USBPcapGUI - pcap / LINKTYPE_USBPCAP Parser
 *
 * Parses the standard pcap binary format emitted by USBPcap.
 * No external library dependency - pure Win32 C++.
 *
 * Reference:
 *   https://desowin.org/usbpcap/captureformat.html
 *   LINKTYPE_USBPCAP = 249
 */

#include "bhplus_types.h"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace bhplus {

/* ──────────── pcap On-Disk Structures ──────────── */

#pragma pack(push, 1)

/// Standard pcap global file header (24 bytes)
struct PcapGlobalHeader {
    uint32_t magic_number;   ///< 0xa1b2c3d4 (native) or 0xd4c3b2a1 (swapped)
    uint16_t version_major;  ///< 2
    uint16_t version_minor;  ///< 4
    int32_t  thiszone;       ///< GMT offset (usually 0)
    uint32_t sigfigs;        ///< Accuracy of timestamps (usually 0)
    uint32_t snaplen;        ///< Max bytes captured per packet
    uint32_t network;        ///< Link type (249 = LINKTYPE_USBPCAP)
};

/// Standard pcap per-packet header (16 bytes)
struct PcapPacketHeader {
    uint32_t ts_sec;   ///< Seconds since epoch
    uint32_t ts_usec;  ///< Microseconds
    uint32_t incl_len; ///< Bytes saved in file
    uint32_t orig_len; ///< Original packet length
};

/// USBPcap base packet header (27 bytes, #pragma pack(1))
/// Present at offset 0 of every pcap packet payload
struct UsbPcapPacketHeader {
    uint16_t headerLen;   ///< Total header length (incl. transfer-specific part)
    uint64_t irpId;       ///< IRP pointer (for req/resp matching)
    uint32_t status;      ///< USBD_STATUS (valid on completion)
    uint16_t function;    ///< URB Function code
    uint8_t  info;        ///< bit0=direction (0=down/FDO→PDO, 1=up/PDO→FDO)
    uint16_t bus;         ///< Root Hub number
    uint16_t device;      ///< USB device address
    uint8_t  endpoint;    ///< Endpoint (bit7=direction: 0=OUT, 1=IN)
    uint8_t  transfer;    ///< Transfer type (0=Iso,1=Int,2=Ctrl,3=Bulk)
    uint32_t dataLength;  ///< Data bytes following the header
};
static_assert(sizeof(UsbPcapPacketHeader) == 27, "UsbPcapPacketHeader must be 27 bytes");

/// Control transfer extension header (1 byte after base header)
struct UsbPcapControlHeader {
    UsbPcapPacketHeader base;
    uint8_t stage; ///< BHPLUS_USB_CONTROL_STAGE_*
};

/// Isochronous transfer per-packet descriptor
struct UsbPcapIsoPacket {
    uint32_t offset;
    uint32_t length;
    uint32_t status;
};

/// Isochronous transfer extension header
struct UsbPcapIsochHeader {
    UsbPcapPacketHeader base;
    uint32_t startFrame;
    uint32_t numberOfPackets;
    uint32_t errorCount;
    UsbPcapIsoPacket packets[1]; ///< packets[numberOfPackets]
};

#pragma pack(pop)

constexpr uint32_t PCAP_MAGIC_NATIVE   = 0xa1b2c3d4u;
constexpr uint32_t PCAP_MAGIC_SWAPPED  = 0xd4c3b2a1u;
constexpr uint32_t LINKTYPE_USBPCAP    = 249u;

/* ──────────── USB Standard Request Helper ──────────── */

/// Decoded standard USB bmRequestType
struct UsbSetupPacket {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;

    bool isStandardRequest() const { return (bmRequestType & 0x60) == 0x00; }
    bool isHostToDevice()    const { return (bmRequestType & 0x80) == 0x00; }
    uint8_t recipient()      const { return bmRequestType & 0x1f; }
};

/// Return a human-readable name for a standard USB bRequest value
const char* UsbStandardRequestName(uint8_t bRequest);

/// Return human-readable URB function name
const char* UrbFunctionName(uint16_t function);

/// Return transfer type name
const char* UsbTransferTypeName(uint8_t transferType);

/* ──────────── Parser Result ──────────── */

/// Fully decoded USB packet - one-to-one with a pcap packet record
struct UsbPcapRecord {
    PcapPacketHeader    pcapHeader;
    UsbPcapPacketHeader usbHeader;

    /// Transfer-specific decoded fields
    uint8_t  controlStage = 0;
    uint8_t  setupPacket[8] = {};  ///< Valid when transfer==CTRL, stage==SETUP

    uint32_t isochStartFrame = 0;
    uint32_t isochPacketCount = 0;
    uint32_t isochErrorCount = 0;

    /// Raw data payload (up to snapshotLength bytes)
    std::vector<uint8_t> data;
};

/* ──────────── PcapStream ──────────── */

/**
 * Stateful streaming parser for pcap data read from a HANDLE.
 *
 * Usage:
 *   PcapStream stream(hPipe);
 *   if (!stream.ReadGlobalHeader()) return;
 *   UsbPcapRecord rec;
 *   while (stream.ReadNextPacket(rec)) { ... }
 */
class PcapStream {
public:
    explicit PcapStream(HANDLE handle);

    /// Read the 24-byte pcap global header and validate magic / LINKTYPE_USBPCAP.
    bool ReadGlobalHeader();

    /// Read the next packet record. Blocks until data arrives or handle closes.
    /// Returns false on EOF or read error.
    bool ReadNextPacket(UsbPcapRecord& out);

    /// Convert a UsbPcapRecord to a BHPLUS_CAPTURE_EVENT.
    /// pendingMap is used to pair DOWN records with UP completions
    /// (fills Duration field when the UP is received).
    static void RecordToEvent(
        const UsbPcapRecord& rec,
        BHPLUS_CAPTURE_EVENT& event,
        uint64_t sequenceNumber);

    bool     byteSwapped() const { return m_byteSwapped; }
    uint32_t snaplen()     const { return m_snaplen; }
    uint32_t linkType()    const { return m_linkType; }

    std::string lastError() const { return m_lastError; }

private:
    bool ReadExact(void* buf, DWORD bytes);

    HANDLE   m_handle;
    bool     m_byteSwapped = false;
    uint32_t m_snaplen = 65535;
    uint32_t m_linkType = 0;
    bool     m_headerRead = false;
    std::string m_lastError;
};

/* ──────────── IRP Pairing Table ──────────── */

/**
 * Matches DOWN requests with UP completions using irpId.
 * Thread-safe for single reader + single filler pattern.
 */
class IrpPairingTable {
public:
    struct PendingEntry {
        uint64_t timestamp;    ///< µs timestamp of DOWN
        uint64_t sequenceNum;  ///< sequence number of DOWN event
    };

    void Insert(uint64_t irpId, const PendingEntry& entry);
    bool Consume(uint64_t irpId, PendingEntry& out);
    void Expire(uint64_t nowUsec, uint64_t maxAgeUsec = 5'000'000);
    size_t Size() const;

private:
    mutable CRITICAL_SECTION m_cs;
    bool m_initialized = false;
    std::unordered_map<uint64_t, PendingEntry> m_map;

    struct CsGuard {
        CsGuard(IrpPairingTable& t) : t(t) { EnterCriticalSection(&t.m_cs); }
        ~CsGuard() { LeaveCriticalSection(&t.m_cs); }
        IrpPairingTable& t;
    };
public:
    IrpPairingTable();
    ~IrpPairingTable();
    IrpPairingTable(const IrpPairingTable&) = delete;
    IrpPairingTable& operator=(const IrpPairingTable&) = delete;
};

} // namespace bhplus
