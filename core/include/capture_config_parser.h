#pragma once
/*
 * USBPcapGUI - Capture Config Parser
 *
 * Free function that maps a capture.start JSON params string to a
 * BHPLUS_CAPTURE_CONFIG struct.  Kept separate from IpcServer so that
 * the unit-test binary can exercise the mapping without any Win32 IPC
 * dependencies.
 *
 * JSON keys accepted (all optional, defaults shown):
 *
 *   snapshotLen        uint32  65535     Max bytes per packet
 *   maxDataPerEvent    uint32  –         Legacy alias for snapshotLen
 *   bufferLen          uint32  1048576   Kernel ring-buffer bytes
 *   maxEvents          uint32  100000    Ring-buffer event count
 *   captureData        bool    true      Capture payload data
 *   captureAll         bool    true      Capture from all connected devices
 *   captureNew         bool    true      Capture from newly connected devices
 *   injectDescriptors  bool    true      Inject device descriptors
 *   filterBus          uint16  0         Hub index filter (0 = all hubs)
 *   deviceIds          array   []        USB device address filter list
 *   deviceId           uint32  –         Single-device shorthand
 */

#include "bhplus_types.h"
#include <string>

namespace bhplus {

/// Parse a JSON object string (capture.start params) into BHPLUS_CAPTURE_CONFIG.
/// Throws nlohmann::json::exception on malformed JSON.
BHPLUS_CAPTURE_CONFIG ParseCaptureStartParams(const std::string& paramsJson);

} // namespace bhplus
