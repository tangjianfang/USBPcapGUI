/*
 * USBPcapGUI - Export Engine
 */

#include "export_engine.h"
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace bhplus {

namespace {

using json = nlohmann::json;

std::string EscapeCsv(std::string value) {
	const bool needsQuotes = value.find_first_of(",\"\n\r") != std::string::npos;
	size_t pos = 0;
	while ((pos = value.find('"', pos)) != std::string::npos) {
		value.insert(pos, 1, '"');
		pos += 2;
	}
	return needsQuotes ? ('"' + value + '"') : value;
}

std::string StatusText(const BHPLUS_CAPTURE_EVENT& event) {
	return event.Status == 0 ? "OK" : fmt::format("0x{:08X}", event.Status);
}

std::string DirectionText(const BHPLUS_CAPTURE_EVENT& event) {
	return event.Direction == BHPLUS_DIR_DOWN ? ">>>" : "<<<";
}

std::string TransferText(const BHPLUS_CAPTURE_EVENT& event) {
	switch (event.TransferType) {
		case BHPLUS_USB_TRANSFER_ISOCHRONOUS: return "ISO";
		case BHPLUS_USB_TRANSFER_INTERRUPT: return "INT";
		case BHPLUS_USB_TRANSFER_CONTROL: return "CTRL";
		case BHPLUS_USB_TRANSFER_BULK: return "BULK";
		default: return "UNKNOWN";
	}
}

std::string PhaseText(const BHPLUS_CAPTURE_EVENT& event) {
	if (event.TransferType == BHPLUS_USB_TRANSFER_CONTROL) {
		switch (event.Detail.Control.Stage) {
			case BHPLUS_USB_CONTROL_STAGE_SETUP: return "SETUP";
			case BHPLUS_USB_CONTROL_STAGE_DATA: return "DATA";
			case BHPLUS_USB_CONTROL_STAGE_STATUS: return "STATUS";
			case BHPLUS_USB_CONTROL_STAGE_COMPLETE: return "COMPLETE";
			default: return "CONTROL";
		}
	}
	return event.Direction == BHPLUS_DIR_DOWN ? "REQUEST" : "COMPLETE";
}

uint8_t MapTransferType(const BHPLUS_CAPTURE_EVENT& event) {
	switch (event.TransferType) {
		case BHPLUS_USB_TRANSFER_ISOCHRONOUS: return 0;
		case BHPLUS_USB_TRANSFER_INTERRUPT: return 1;
		case BHPLUS_USB_TRANSFER_CONTROL: return 2;
		case BHPLUS_USB_TRANSFER_BULK: return 3;
		default: return 3;
	}
}

void AppendLe16(std::vector<uint8_t>& out, uint16_t value) {
	out.push_back(static_cast<uint8_t>(value & 0xff));
	out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void AppendLe32(std::vector<uint8_t>& out, uint32_t value) {
	out.push_back(static_cast<uint8_t>(value & 0xff));
	out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
	out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
	out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void AppendLe64(std::vector<uint8_t>& out, uint64_t value) {
	for (int shift = 0; shift < 64; shift += 8) {
		out.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
	}
}

std::vector<uint8_t> BuildUsbPcapPayload(const ExportRecord& record) {
	const auto transfer = MapTransferType(record.event);
	const uint16_t headerLen = transfer == 2 ? 28 : (transfer == 0 ? 39 : 27);

	std::vector<uint8_t> out;
	out.reserve(headerLen + record.payload.size());
	AppendLe16(out, headerLen);
	AppendLe64(out, record.event.IrpId);
	AppendLe32(out, record.event.Status);
	AppendLe16(out, record.event.UrbFunction);
	out.push_back(record.event.Direction == BHPLUS_DIR_UP ? 1u : 0u);
	AppendLe16(out, record.event.Bus);
	AppendLe16(out, record.event.Device);
	out.push_back(record.event.Endpoint & 0xffu);
	out.push_back(transfer);
	AppendLe32(out, static_cast<uint32_t>(record.payload.size()));

	if (transfer == 2) {
		out.push_back(record.event.Detail.Control.Stage);
	} else if (transfer == 0) {
		AppendLe32(out, record.event.Detail.Isoch.StartFrame);
		AppendLe32(out, record.event.Detail.Isoch.NumberOfPackets);
		AppendLe32(out, record.event.Detail.Isoch.ErrorCount);
	}

	out.insert(out.end(), record.payload.begin(), record.payload.end());
	return out;
}

json ToJson(const ExportRecord& record) {
	return {
		{"seq", record.event.SequenceNumber},
		{"timestamp", record.event.Timestamp},
		{"direction", DirectionText(record.event)},
		{"protocol", record.decoded.protocol},
		{"transferType", TransferText(record.event)},
		{"phase", PhaseText(record.event)},
		{"command", record.decoded.commandName},
		{"summary", record.decoded.summary},
		{"status", StatusText(record.event)},
		{"statusCode", record.event.Status},
		{"bus", record.event.Bus},
		{"device", record.event.Device},
		{"endpoint", record.event.Endpoint},
		{"deviceId", (static_cast<uint32_t>(record.event.Bus) << 16) | record.event.Device},
		{"dataLength", record.event.DataLength},
		{"duration", record.event.Duration},
		{"irpId", record.event.IrpId},
		{"data", ExportEngine::ToHex(record.payload.data(), record.payload.size())}
	};
}

} // namespace

ExportFormat ExportEngine::ParseFormat(const std::string& format) {
	std::string normalized = format;
	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	if (normalized == "csv") return ExportFormat::Csv;
	if (normalized == "json") return ExportFormat::Json;
	if (normalized == "pcap") return ExportFormat::Pcap;
	return ExportFormat::Text;
}

const char* ExportEngine::FormatName(ExportFormat format) {
	switch (format) {
		case ExportFormat::Csv: return "csv";
		case ExportFormat::Json: return "json";
		case ExportFormat::Pcap: return "pcap";
		case ExportFormat::Text:
		default:
			return "txt";
	}
}

bool ExportEngine::Write(std::ostream& out,
						 ExportFormat format,
						 const std::vector<ExportRecord>& records) {
	switch (format) {
		case ExportFormat::Csv: {
			out << "Seq,Timestamp,Direction,Protocol,Transfer,Phase,Command,Status,Bus,Device,Endpoint,Length,Duration,Summary\n";
			for (const auto& record : records) {
				out << record.event.SequenceNumber << ','
					<< EscapeCsv(FormatTimestamp(record.event.Timestamp)) << ','
					<< EscapeCsv(DirectionText(record.event)) << ','
					<< EscapeCsv(record.decoded.protocol) << ','
					<< EscapeCsv(TransferText(record.event)) << ','
					<< EscapeCsv(PhaseText(record.event)) << ','
					<< EscapeCsv(record.decoded.commandName) << ','
					<< EscapeCsv(StatusText(record.event)) << ','
					<< record.event.Bus << ','
					<< record.event.Device << ','
					<< static_cast<uint32_t>(record.event.Endpoint) << ','
					<< record.event.DataLength << ','
					<< record.event.Duration << ','
					<< EscapeCsv(record.decoded.summary) << '\n';
			}
			return out.good();
		}
		case ExportFormat::Json: {
			json items = json::array();
			for (const auto& record : records) {
				items.push_back(ToJson(record));
			}
			out << items.dump(2);
			return out.good();
		}
		case ExportFormat::Pcap: {
			std::vector<uint8_t> bytes;
			bytes.reserve(24 + records.size() * 64);

			AppendLe32(bytes, 0xa1b2c3d4u);
			AppendLe16(bytes, 2);
			AppendLe16(bytes, 4);
			AppendLe32(bytes, 0);
			AppendLe32(bytes, 0);
			AppendLe32(bytes, 65535u);
			AppendLe32(bytes, 249u);

			for (const auto& record : records) {
				auto payload = BuildUsbPcapPayload(record);
				const uint32_t tsSec = static_cast<uint32_t>(record.event.Timestamp / 1'000'000ULL);
				const uint32_t tsUsec = static_cast<uint32_t>(record.event.Timestamp % 1'000'000ULL);
				AppendLe32(bytes, tsSec);
				AppendLe32(bytes, tsUsec);
				AppendLe32(bytes, static_cast<uint32_t>(payload.size()));
				AppendLe32(bytes, static_cast<uint32_t>(payload.size()));
				bytes.insert(bytes.end(), payload.begin(), payload.end());
			}

			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			return out.good();
		}
		case ExportFormat::Text:
		default:
			for (const auto& record : records) {
				out << BuildTextLine(record) << '\n';
			}
			return out.good();
	}
}

std::string ExportEngine::FormatTimestamp(uint64_t timestamp) {
	const std::time_t seconds = static_cast<std::time_t>(timestamp / 1'000'000ULL);
	const uint32_t micros = static_cast<uint32_t>(timestamp % 1'000'000ULL);
	std::tm tmUtc{};
#ifdef _WIN32
	gmtime_s(&tmUtc, &seconds);
#else
	gmtime_r(&seconds, &tmUtc);
#endif
	std::ostringstream oss;
	oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(6) << std::setfill('0') << micros << 'Z';
	return oss.str();
}

std::string ExportEngine::ToHex(const uint8_t* data, size_t size) {
	if (!data || size == 0) return {};
	static constexpr char digits[] = "0123456789abcdef";
	std::string out;
	out.reserve(size * 2);
	for (size_t i = 0; i < size; ++i) {
		out.push_back(digits[(data[i] >> 4) & 0x0f]);
		out.push_back(digits[data[i] & 0x0f]);
	}
	return out;
}

std::string ExportEngine::BuildTextLine(const ExportRecord& record) {
	return fmt::format(
		"{:>8}  {}  {:<3}  {:<5}  {:<8}  {:<32}  {:<10}  {:>6}B",
		record.event.SequenceNumber,
		FormatTimestamp(record.event.Timestamp),
		DirectionText(record.event),
		record.decoded.protocol.empty() ? "USB" : record.decoded.protocol,
		TransferText(record.event),
		record.decoded.commandName.empty() ? record.decoded.summary : record.decoded.commandName,
		StatusText(record.event),
		record.event.DataLength);
}

} // namespace bhplus
