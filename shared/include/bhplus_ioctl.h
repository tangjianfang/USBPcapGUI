#pragma once

/*
 * USBPcapGUI - IOCTL Definitions
 * Shared between kernel driver and user-mode components.
 */

#include "bhplus_types.h"

#define BHPLUS_DEVICE_TYPE          0x8000
#define BHPLUS_DEVICE_NAME_USER     L"\\\\.\\BHPlus"
#define BHPLUS_DEVICE_NAME_KERNEL   L"\\Device\\BHPlus"
#define BHPLUS_SYMLINK_NAME         L"\\DosDevices\\BHPlus"

/* ──────────── IOCTL Codes ──────────── */

/* Start capturing events on selected devices */
#define IOCTL_BHPLUS_START_CAPTURE      \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Stop capturing */
#define IOCTL_BHPLUS_STOP_CAPTURE       \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Read captured events from ring buffer */
#define IOCTL_BHPLUS_READ_EVENTS        \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x802, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)

/* Set capture filter configuration */
#define IOCTL_BHPLUS_SET_FILTER         \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Send a pass-through command to a device */
#define IOCTL_BHPLUS_SEND_COMMAND       \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Reset a device or bus */
#define IOCTL_BHPLUS_RESET_DEVICE       \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Enumerate available devices */
#define IOCTL_BHPLUS_ENUM_DEVICES       \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Get capture statistics */
#define IOCTL_BHPLUS_GET_STATS          \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Set capture configuration */
#define IOCTL_BHPLUS_SET_CONFIG         \
    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* ──────────── IOCTL Structures ──────────── */

typedef struct _BHPLUS_READ_EVENTS_REQUEST {
    UINT32  MaxEvents;              /* Max events to read           */
    UINT64  StartSequenceNumber;    /* Read from this sequence      */
} BHPLUS_READ_EVENTS_REQUEST;

typedef struct _BHPLUS_READ_EVENTS_RESPONSE {
    UINT32  EventCount;             /* Actual events returned       */
    UINT64  NextSequenceNumber;     /* Next sequence to request     */
    /* Followed by EventCount * BHPLUS_CAPTURE_EVENT structures */
    /* Followed by associated data blocks                       */
} BHPLUS_READ_EVENTS_RESPONSE;

typedef struct _BHPLUS_SEND_COMMAND_REQUEST {
    UINT32  DeviceId;
    UINT32  CommandType;            /* BHPLUS_BUS_TYPE              */
    UINT32  CommandLength;
    UINT32  DataLength;
    UINT32  TimeoutMs;
    /* Followed by command bytes and data bytes                 */
} BHPLUS_SEND_COMMAND_REQUEST;

typedef struct _BHPLUS_ENUM_DEVICES_RESPONSE {
    UINT32  DeviceCount;
    /* Followed by DeviceCount * BHPLUS_DEVICE_INFO structures  */
} BHPLUS_ENUM_DEVICES_RESPONSE;
