#pragma once

/*
 * USBPcapGUI - Common Type Definitions
 * [Updated] USB capture is based on USBPcap (LINKTYPE_USBPCAP = 249).
 *           Event structure mirrors USBPCAP_BUFFER_PACKET_HEADER fields.
 */

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <stdint.h>
#include <windows.h>
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
#endif

/* ──────────── USB Transfer Types (match USBPcap values) ──────────── */
#define BHPLUS_USB_TRANSFER_ISOCHRONOUS  0
#define BHPLUS_USB_TRANSFER_INTERRUPT    1
#define BHPLUS_USB_TRANSFER_CONTROL      2
#define BHPLUS_USB_TRANSFER_BULK         3

/* ──────────── USB Control Stages (match USBPcap values) ──────────── */
#define BHPLUS_USB_CONTROL_STAGE_SETUP    0
#define BHPLUS_USB_CONTROL_STAGE_DATA     1
#define BHPLUS_USB_CONTROL_STAGE_STATUS   2
#define BHPLUS_USB_CONTROL_STAGE_COMPLETE 3

/* Direction decoded from endpoint / USBPcap info field */
#define BHPLUS_DIR_DOWN  0   /* FDO → PDO (Host → Device)  */
#define BHPLUS_DIR_UP    1   /* PDO → FDO (Device → Host)  */

/* USB device speed values for BHPLUS_USB_DEVICE_INFO.Speed */
#define BHPLUS_USB_SPEED_UNKNOWN  0
#define BHPLUS_USB_SPEED_LOW      1   /* USB 1.1 Low-Speed  1.5 Mb/s  */
#define BHPLUS_USB_SPEED_FULL     2   /* USB 1.1 Full-Speed  12 Mb/s  */
#define BHPLUS_USB_SPEED_HIGH     3   /* USB 2.0 High-Speed 480 Mb/s  */
#define BHPLUS_USB_SPEED_SUPER    4   /* USB 3.x SuperSpeed   5 Gb/s+ */

/* ──────────── Event Source ──────────── */
typedef enum _BHPLUS_EVENT_SOURCE {
    BHPLUS_SOURCE_USBPCAP = 0,  /* Read from \\.\USBPcap[N] pcap stream */
    BHPLUS_SOURCE_DRIVER,       /* Read from BHPlus extension driver     */
} BHPLUS_EVENT_SOURCE;

/* ──────────── Event Types ──────────── */
typedef enum _BHPLUS_EVENT_TYPE {
    BHPLUS_EVENT_NONE = 0,

    /* USB events (from USBPcap) */
    BHPLUS_EVENT_URB_DOWN,          /* URB heading down (Host→Device)    */
    BHPLUS_EVENT_URB_UP,            /* URB completion   (Device→Host)    */

    /* Storage events (Phase 3, from BHPlus extension driver) */
    BHPLUS_EVENT_NVME_ADMIN,        /* NVMe Admin command                */
    BHPLUS_EVENT_NVME_IO,           /* NVMe I/O command                  */
    BHPLUS_EVENT_SCSI_CDB,          /* SCSI CDB                          */
    BHPLUS_EVENT_ATA_CMD,           /* ATA command                       */

    /* Serial events (Phase 3) */
    BHPLUS_EVENT_SERIAL_TX,         /* Serial port transmit              */
    BHPLUS_EVENT_SERIAL_RX,         /* Serial port receive               */

    BHPLUS_EVENT_MAX
} BHPLUS_EVENT_TYPE;

/* ──────────── Main Capture Event Structure ──────────── */
#pragma pack(push, 1)

typedef struct _BHPLUS_CAPTURE_EVENT {
    /* Common fields */
    UINT64              Timestamp;       /* Microseconds since epoch (pcap ts) */
    UINT64              SequenceNumber;  /* Monotonic counter                  */
    UINT64              IrpId;           /* IRP pointer (for req/resp pairing) */
    BHPLUS_EVENT_TYPE   EventType;
    BHPLUS_EVENT_SOURCE Source;

    /* USB identity (from USBPCAP_BUFFER_PACKET_HEADER) */
    UINT16              Bus;             /* Root Hub number                    */
    UINT16              Device;          /* USB device address (1–127)         */
    UINT8               Endpoint;        /* Endpoint addr (bit7 = IN direction)*/
    UINT8               TransferType;    /* BHPLUS_USB_TRANSFER_*              */
    UINT8               Direction;       /* BHPLUS_DIR_DOWN / _UP              */
    UINT16              UrbFunction;     /* URB Function code                  */

    /* Status & timing */
    UINT32              Status;          /* USBD_STATUS (valid on completion)  */
    UINT32              Duration;        /* Req/resp latency in µs (post-pair) */

    /* Data */
    UINT32              DataLength;      /* Bytes of captured payload          */

    /* Transfer-specific details */
    union {
        struct {
            UINT8       Stage;               /* BHPLUS_USB_CONTROL_STAGE_*     */
            UINT8       SetupPacket[8];      /* 8-byte USB SETUP packet        */
        } Control;
        struct {
            UINT32      StartFrame;
            UINT32      NumberOfPackets;
            UINT32      ErrorCount;
        } Isoch;
        struct {
            UINT8       Opcode;
            UINT32      NSID;
        } Nvme;
        struct {
            UINT8       Cdb[16];
            UINT8       CdbLength;
            UINT8       ScsiStatus;
        } Scsi;
        struct {
            UINT8       Command;
            UINT64      Lba;
            UINT16      SectorCount;
        } Ata;
        struct {
            UINT32      BaudRate;
            UINT8       DataBits;
        } Serial;
    } Detail;
} BHPLUS_CAPTURE_EVENT;

#pragma pack(pop)

/* ──────────── USB Device Info ──────────── */
#define BHPLUS_MAX_DEVICE_NAME  256

typedef struct _BHPLUS_USB_DEVICE_INFO {
    UINT16  Bus;                            /* Root Hub number            */
    UINT16  DeviceAddress;                  /* USB device address         */
    UINT16  VendorId;
    UINT16  ProductId;
    UINT8   DeviceClass;
    UINT8   DeviceSubClass;
    UINT8   DeviceProtocol;
    UINT8   Speed;                          /* 0=Low, 1=Full, 2=High, 3=Super */
    WCHAR   DeviceName[BHPLUS_MAX_DEVICE_NAME];
    WCHAR   SerialNumber[64];
    WCHAR   RootHubSymLink[128];            /* e.g. \\.\USBPcap1          */
    UINT8   IsHub;
} BHPLUS_USB_DEVICE_INFO;

/* Legacy alias for IPC server compatibility */
typedef struct _BHPLUS_DEVICE_INFO {
    UINT32  DeviceId;
    UINT32  BusType;
    UINT16  VendorId;
    UINT16  ProductId;
    WCHAR   DeviceName[BHPLUS_MAX_DEVICE_NAME];
    WCHAR   InstanceId[BHPLUS_MAX_DEVICE_NAME];
} BHPLUS_DEVICE_INFO;

typedef enum _BHPLUS_BUS_TYPE {
    BHPLUS_BUS_UNKNOWN = 0,
    BHPLUS_BUS_USB,
    BHPLUS_BUS_NVME,
    BHPLUS_BUS_SATA,
    BHPLUS_BUS_SCSI,
    BHPLUS_BUS_SERIAL,
    BHPLUS_BUS_BLUETOOTH,
    BHPLUS_BUS_FIREWIRE,
    BHPLUS_BUS_MAX
} BHPLUS_BUS_TYPE;

/* ──────────── Capture Configuration ──────────── */
#define BHPLUS_MAX_FILTER_DEVICES  64

typedef struct _BHPLUS_CAPTURE_CONFIG {
    UINT32  SnapshotLength;             /* Max bytes captured per packet  */
    UINT32  BufferLength;               /* Kernel ring-buffer size (bytes)*/
    UINT32  MaxEvents;                  /* Max events in memory           */
    UINT32  FilterDeviceCount;          /* 0 = capture all devices        */
    UINT16  FilterDeviceAddresses[BHPLUS_MAX_FILTER_DEVICES];
    UINT16  FilterBus;                  /* 0xFFFF = all Root Hubs         */
    UINT8   CaptureData;                /* 1 = capture payload bytes      */
    UINT8   CaptureAllDevices;          /* 1 = capture all connected devs */
    UINT8   CaptureNewDevices;          /* 1 = capture newly connected    */
    UINT8   InjectDescriptors;          /* 1 = inject device descriptors  */
} BHPLUS_CAPTURE_CONFIG;

/* ──────────── Statistics ──────────── */
typedef struct _BHPLUS_STATS {
    UINT64  TotalEventsCaptured;
    UINT64  TotalBytesCaptured;
    UINT64  EventsDropped;
    UINT64  UptimeMs;
    UINT32  ActiveRootHubs;
    UINT32  ActiveDeviceCount;
} BHPLUS_STATS;
