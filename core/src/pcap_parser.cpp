/*
 * USBPcapGUI - pcap / LINKTYPE_USBPCAP Parser Implementation
 */

#include "pcap_parser.h"
#include <spdlog/spdlog.h>
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cstring>

namespace bhplus {

/* ──────────── String Tables ──────────── */

const char* UsbStandardRequestName(uint8_t bRequest) {
    switch (bRequest) {
        case 0x00: return "GET_STATUS";
        case 0x01: return "CLEAR_FEATURE";
        case 0x03: return "SET_FEATURE";
        case 0x05: return "SET_ADDRESS";
        case 0x06: return "GET_DESCRIPTOR";
        case 0x07: return "SET_DESCRIPTOR";
        case 0x08: return "GET_CONFIGURATION";
        case 0x09: return "SET_CONFIGURATION";
        case 0x0A: return "GET_INTERFACE";
        case 0x0B: return "SET_INTERFACE";
        case 0x0C: return "SYNCH_FRAME";
        default:   return "VENDOR/CLASS";
    }
}

const char* UrbFunctionName(uint16_t function) {
    // URB_FUNCTION_* values from wdmspec (partial)
    switch (function) {
        case 0x0000: return "SELECT_CONFIGURATION";
        case 0x0001: return "SELECT_INTERFACE";
        case 0x0002: return "ABORT_PIPE";
        case 0x0003: return "TAKE_FRAME_LENGTH_CONTROL";
        case 0x0004: return "RELEASE_FRAME_LENGTH_CONTROL";
        case 0x0005: return "GET_FRAME_LENGTH";
        case 0x0006: return "SET_FRAME_LENGTH";
        case 0x0007: return "GET_CURRENT_FRAME_NUMBER";
        case 0x0008: return "CONTROL_TRANSFER";
        case 0x0009: return "BULK_OR_INTERRUPT_TRANSFER";
        case 0x000A: return "ISOCH_TRANSFER";
        case 0x000B: return "GET_DESCRIPTOR_FROM_DEVICE";
        case 0x000C: return "SET_DESCRIPTOR_TO_DEVICE";
        case 0x000D: return "SET_FEATURE_TO_DEVICE";
        case 0x000E: return "SET_FEATURE_TO_INTERFACE";
        case 0x000F: return "SET_FEATURE_TO_ENDPOINT";
        case 0x0010: return "CLEAR_FEATURE_TO_DEVICE";
        case 0x0011: return "CLEAR_FEATURE_TO_INTERFACE";
        case 0x0012: return "CLEAR_FEATURE_TO_ENDPOINT";
        case 0x0013: return "GET_STATUS_FROM_DEVICE";
        case 0x0014: return "GET_STATUS_FROM_INTERFACE";
        case 0x0015: return "GET_STATUS_FROM_ENDPOINT";
        case 0x0017: return "VENDOR_DEVICE";
        case 0x0018: return "VENDOR_INTERFACE";
        case 0x0019: return "VENDOR_ENDPOINT";
        case 0x001A: return "CLASS_DEVICE";
        case 0x001B: return "CLASS_INTERFACE";
        case 0x001C: return "CLASS_ENDPOINT";
        case 0x001F: return "CLASS_OTHER";
        case 0x0020: return "VENDOR_OTHER";
        case 0x0021: return "GET_STATUS_FROM_OTHER";
        case 0x0022: return "CLEAR_FEATURE_TO_OTHER";
        case 0x0023: return "SET_FEATURE_TO_OTHER";
        case 0x0024: return "GET_DESCRIPTOR_FROM_ENDPOINT";
        case 0x0025: return "SET_DESCRIPTOR_TO_ENDPOINT";
        case 0x0026: return "GET_CONFIGURATION";
        case 0x0027: return "GET_INTERFACE";
        case 0x0028: return "GET_DESCRIPTOR_FROM_INTERFACE";
        case 0x0029: return "SET_DESCRIPTOR_TO_INTERFACE";
        case 0x002A: return "GET_MS_FEATURE_DESCRIPTOR";
        case 0x0030: return "SYNC_RESET_PIPE_AND_CLEAR_STALL";
        case 0x0035: return "CONTROL_TRANSFER_EX";
        case 0x0037: return "OPEN_STATIC_STREAMS";
        case 0x0039: return "BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL";
        default:     return "UNKNOWN_FUNCTION";
    }
}

const char* UsbTransferTypeName(uint8_t transferType) {
    switch (transferType) {
        case BHPLUS_USB_TRANSFER_ISOCHRONOUS: return "ISO";
        case BHPLUS_USB_TRANSFER_INTERRUPT:   return "INT";
        case BHPLUS_USB_TRANSFER_CONTROL:     return "CTRL";
        case BHPLUS_USB_TRANSFER_BULK:        return "BULK";
        default:                              return "???";
    }
}

/* ──────────── PcapStream ──────────── */

PcapStream::PcapStream(HANDLE handle) : m_handle(handle) {}

bool PcapStream::ReadExact(void* buf, DWORD bytes) {
    BYTE* p = static_cast<BYTE*>(buf);
    DWORD remaining = bytes;
    while (remaining > 0) {
        DWORD read = 0;
        if (!ReadFile(m_handle, p, remaining, &read, nullptr)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
                m_lastError = "Pipe closed";
            } else {
                m_lastError = "ReadFile error: " + std::to_string(err);
            }
            return false;
        }
        if (read == 0) {
            m_lastError = "EOF";
            return false;
        }
        p         += read;
        remaining -= read;
    }
    return true;
}

bool PcapStream::ReadGlobalHeader() {
    PcapGlobalHeader hdr{};
    if (!ReadExact(&hdr, sizeof(hdr))) return false;

    if (hdr.magic_number == PCAP_MAGIC_NATIVE) {
        m_byteSwapped = false;
    } else if (hdr.magic_number == PCAP_MAGIC_SWAPPED) {
        m_byteSwapped = true;
        // Byte-swap fields for consistent use
        hdr.version_major = _byteswap_ushort(hdr.version_major);
        hdr.version_minor = _byteswap_ushort(hdr.version_minor);
        hdr.snaplen        = _byteswap_ulong(hdr.snaplen);
        hdr.network        = _byteswap_ulong(hdr.network);
    } else {
        m_lastError = "Invalid pcap magic: " + std::to_string(hdr.magic_number);
        spdlog::error("[pcap] {}", m_lastError);
        return false;
    }

    m_snaplen  = hdr.snaplen;
    m_linkType = hdr.network;

    if (m_linkType != LINKTYPE_USBPCAP) {
        m_lastError = "Expected LINKTYPE_USBPCAP(249), got " + std::to_string(m_linkType);
        spdlog::error("[pcap] {}", m_lastError);
        return false;
    }

    m_headerRead = true;
    spdlog::debug("[pcap] GlobalHeader OK: snaplen={}, linktype={}", m_snaplen, m_linkType);
    return true;
}

bool PcapStream::ReadNextPacket(UsbPcapRecord& out) {
    if (!m_headerRead) {
        m_lastError = "GlobalHeader not read";
        return false;
    }

    // ---- 1. Read per-packet header ----
    if (!ReadExact(&out.pcapHeader, sizeof(PcapPacketHeader))) return false;
    if (m_byteSwapped) {
        out.pcapHeader.ts_sec   = _byteswap_ulong(out.pcapHeader.ts_sec);
        out.pcapHeader.ts_usec  = _byteswap_ulong(out.pcapHeader.ts_usec);
        out.pcapHeader.incl_len = _byteswap_ulong(out.pcapHeader.incl_len);
        out.pcapHeader.orig_len = _byteswap_ulong(out.pcapHeader.orig_len);
    }

    const uint32_t inclLen = out.pcapHeader.incl_len;
    if (inclLen < sizeof(UsbPcapPacketHeader)) {
        m_lastError = "Packet too small: " + std::to_string(inclLen);
        spdlog::warn("[pcap] {}", m_lastError);
        return false;
    }

    // ---- 2. Read entire payload into buffer ----
    std::vector<uint8_t> payload(inclLen);
    if (!ReadExact(payload.data(), inclLen)) return false;

    // ---- 3. Extract base USBPcap header ----
    std::memcpy(&out.usbHeader, payload.data(), sizeof(UsbPcapPacketHeader));
    if (m_byteSwapped) {
        out.usbHeader.headerLen  = _byteswap_ushort(out.usbHeader.headerLen);
        out.usbHeader.irpId      = _byteswap_uint64(out.usbHeader.irpId);
        out.usbHeader.status     = _byteswap_ulong(out.usbHeader.status);
        out.usbHeader.function   = _byteswap_ushort(out.usbHeader.function);
        out.usbHeader.bus        = _byteswap_ushort(out.usbHeader.bus);
        out.usbHeader.device     = _byteswap_ushort(out.usbHeader.device);
        out.usbHeader.dataLength = _byteswap_ulong(out.usbHeader.dataLength);
    }

    const uint16_t headerLen = out.usbHeader.headerLen;
    if (headerLen > inclLen) {
        m_lastError = "headerLen > inclLen";
        return false;
    }

    // ---- 4. Transfer-specific header ----
    out.controlStage = 0;
    std::memset(out.setupPacket, 0, 8);
    out.isochStartFrame  = 0;
    out.isochPacketCount = 0;
    out.isochErrorCount  = 0;

    const uint8_t transferType = out.usbHeader.transfer;
    const size_t  baseSize     = sizeof(UsbPcapPacketHeader);

    if (transferType == BHPLUS_USB_TRANSFER_CONTROL) {
        if (headerLen >= baseSize + 1) {
            out.controlStage = payload[baseSize]; // SETUP/DATA/COMPLETE
        }
        // Setup packet immediately follows data region when stage==SETUP
        if (out.controlStage == BHPLUS_USB_CONTROL_STAGE_SETUP) {
            const size_t dataStart = headerLen;
            if (inclLen >= dataStart + 8) {
                std::memcpy(out.setupPacket, payload.data() + dataStart, 8);
            }
        }
    } else if (transferType == BHPLUS_USB_TRANSFER_ISOCHRONOUS) {
        // Field offsets after base header: startFrame(4), numPackets(4), errorCount(4)
        const size_t minIso = baseSize + 12;
        if (headerLen >= minIso) {
            uint32_t sf = 0, np = 0, ec = 0;
            std::memcpy(&sf, payload.data() + baseSize + 0, 4);
            std::memcpy(&np, payload.data() + baseSize + 4, 4);
            std::memcpy(&ec, payload.data() + baseSize + 8, 4);
            if (m_byteSwapped) { sf = _byteswap_ulong(sf); np = _byteswap_ulong(np); ec = _byteswap_ulong(ec); }
            out.isochStartFrame  = sf;
            out.isochPacketCount = np;
            out.isochErrorCount  = ec;
        }
    }

    // ---- 5. Data payload ----
    const size_t dataStart = headerLen;
    const size_t dataLen   = (inclLen > dataStart) ? (inclLen - dataStart) : 0u;
    out.data.assign(payload.begin() + dataStart, payload.begin() + dataStart + dataLen);

    return true;
}

void PcapStream::RecordToEvent(
    const UsbPcapRecord&  rec,
    BHPLUS_CAPTURE_EVENT& evt,
    uint64_t              sequenceNumber)
{
    const auto& u = rec.usbHeader;
    const auto& p = rec.pcapHeader;

    evt = {};  // zero-init

    // Sequence number
    evt.SequenceNumber = sequenceNumber;

    // Timestamp: µs since epoch
    evt.Timestamp = static_cast<uint64_t>(p.ts_sec) * 1'000'000ULL + p.ts_usec;

    // IRP fields
    evt.IrpId        = u.irpId;
    evt.Bus          = u.bus;
    evt.Device       = u.device;
    evt.Endpoint     = u.endpoint & 0x7Fu; // strip direction bit
    evt.TransferType = u.transfer;
    evt.UrbFunction  = u.function;
    evt.Status       = u.status;
    evt.DataLength   = u.dataLength;

    // Direction: info bit0: 0=request (down), 1=completion (up)
    evt.Direction = (u.info & 0x01) ? BHPLUS_DIR_UP : BHPLUS_DIR_DOWN;
    // EventType based on direction — enables UsbParser::CanParse()
    evt.EventType = (evt.Direction == BHPLUS_DIR_DOWN)
                    ? BHPLUS_EVENT_URB_DOWN
                    : BHPLUS_EVENT_URB_UP;

    // Source
    evt.Source = BHPLUS_SOURCE_USBPCAP;

    // Transfer-specific detail
    if (u.transfer == BHPLUS_USB_TRANSFER_CONTROL) {
        evt.Detail.Control.Stage = rec.controlStage;
        std::memcpy(evt.Detail.Control.SetupPacket,
                    rec.setupPacket, 8);
    } else if (u.transfer == BHPLUS_USB_TRANSFER_ISOCHRONOUS) {
        evt.Detail.Isoch.StartFrame   = rec.isochStartFrame;
        evt.Detail.Isoch.NumberOfPackets = rec.isochPacketCount;
        evt.Detail.Isoch.ErrorCount   = rec.isochErrorCount;
    }

    // Duration: filled by IRP pairing in caller
    evt.Duration = 0;
}

/* ──────────── IrpPairingTable ──────────── */

IrpPairingTable::IrpPairingTable() {
    InitializeCriticalSection(&m_cs);
    m_initialized = true;
}

IrpPairingTable::~IrpPairingTable() {
    if (m_initialized) {
        DeleteCriticalSection(&m_cs);
    }
}

void IrpPairingTable::Insert(uint64_t irpId, const PendingEntry& entry) {
    CsGuard g(*this);
    m_map[irpId] = entry;
}

bool IrpPairingTable::Consume(uint64_t irpId, PendingEntry& out) {
    CsGuard g(*this);
    auto it = m_map.find(irpId);
    if (it == m_map.end()) return false;
    out = it->second;
    m_map.erase(it);
    return true;
}

void IrpPairingTable::Expire(uint64_t nowUsec, uint64_t maxAgeUsec) {
    CsGuard g(*this);
    for (auto it = m_map.begin(); it != m_map.end(); ) {
        if (nowUsec > it->second.timestamp &&
            (nowUsec - it->second.timestamp) > maxAgeUsec) {
            it = m_map.erase(it);
        } else {
            ++it;
        }
    }
}

size_t IrpPairingTable::Size() const {
    CsGuard g(const_cast<IrpPairingTable&>(*this));
    return m_map.size();
}

} // namespace bhplus
