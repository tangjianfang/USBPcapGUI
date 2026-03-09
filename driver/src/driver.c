/*
 * USBPcapGUI - Kernel Filter Driver
 * DriverEntry and initialization.
 */

#include <ntddk.h>
#include <wdf.h>
#include "bhplus_common.h"

/* Forward declarations */
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD BHPlusDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL BHPlusIoDeviceControl;

/* Global state */
typedef struct _BHPLUS_DRIVER_CONTEXT {
    WDFDEVICE   ControlDevice;
    BOOLEAN     Capturing;
    UINT64      SequenceCounter;
    /* Ring buffer pointers */
    PVOID       EventBuffer;
    ULONG       EventBufferSize;
    ULONG       EventBufferHead;
    ULONG       EventBufferTail;
    KSPIN_LOCK  BufferLock;
} BHPLUS_DRIVER_CONTEXT, *PBHPLUS_DRIVER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BHPLUS_DRIVER_CONTEXT, BHPlusGetDriverContext)

/*
 * DriverEntry - Driver initialization
 */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "BHPlus: DriverEntry\n"));

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_DRIVER_CONFIG_INIT(&config, BHPlusDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "BHPlus: WdfDriverCreate failed 0x%x\n", status));
        return status;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "BHPlus: Driver initialized successfully\n"));

    return STATUS_SUCCESS;
}

/*
 * BHPlusDeviceAdd - Create the control device for user-mode communication
 */
NTSTATUS
BHPlusDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;
    PBHPLUS_DRIVER_CONTEXT context;

    UNREFERENCED_PARAMETER(Driver);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "BHPlus: DeviceAdd\n"));

    /* Set as filter driver */
    WdfFdoInitSetFilter(DeviceInit);

    /* Device attributes with context */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, BHPLUS_DRIVER_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "BHPlus: WdfDeviceCreate failed 0x%x\n", status));
        return status;
    }

    /* Initialize context */
    context = BHPlusGetDriverContext(device);
    context->ControlDevice = device;
    context->Capturing = FALSE;
    context->SequenceCounter = 0;
    context->EventBuffer = NULL;
    context->EventBufferSize = 0;
    context->EventBufferHead = 0;
    context->EventBufferTail = 0;
    KeInitializeSpinLock(&context->BufferLock);

    /* Create default I/O queue */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = BHPlusIoDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "BHPlus: WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "BHPlus: Device created successfully\n"));

    return STATUS_SUCCESS;
}

/*
 * BHPlusIoDeviceControl - Handle IOCTL requests from user mode
 */
VOID
BHPlusIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PBHPLUS_DRIVER_CONTEXT context = BHPlusGetDriverContext(device);
    size_t bytesReturned = 0;

    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
        "BHPlus: IOCTL 0x%x\n", IoControlCode));

    switch (IoControlCode) {
    case IOCTL_BHPLUS_START_CAPTURE:
        context->Capturing = TRUE;
        context->SequenceCounter = 0;
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "BHPlus: Capture started\n"));
        break;

    case IOCTL_BHPLUS_STOP_CAPTURE:
        context->Capturing = FALSE;
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "BHPlus: Capture stopped\n"));
        break;

    case IOCTL_BHPLUS_READ_EVENTS:
        /* TODO: Copy events from ring buffer to output */
        status = STATUS_NOT_IMPLEMENTED;
        break;

    case IOCTL_BHPLUS_SET_FILTER:
        /* TODO: Set filter parameters */
        status = STATUS_NOT_IMPLEMENTED;
        break;

    case IOCTL_BHPLUS_SEND_COMMAND:
        /* TODO: Build and forward pass-through command */
        status = STATUS_NOT_IMPLEMENTED;
        break;

    case IOCTL_BHPLUS_RESET_DEVICE:
        /* TODO: Issue bus/device reset */
        status = STATUS_NOT_IMPLEMENTED;
        break;

    case IOCTL_BHPLUS_ENUM_DEVICES:
        /* TODO: Return list of filtered devices */
        status = STATUS_NOT_IMPLEMENTED;
        break;

    case IOCTL_BHPLUS_GET_STATS:
        /* TODO: Return statistics */
        status = STATUS_NOT_IMPLEMENTED;
        break;

    case IOCTL_BHPLUS_SET_CONFIG:
        /* TODO: Allocate/resize ring buffer */
        status = STATUS_NOT_IMPLEMENTED;
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
