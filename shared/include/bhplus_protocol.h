#pragma once

/*
 * USBPcapGUI - Protocol Constants
 * Common protocol definitions for decoders.
 */

#include "bhplus_types.h"

/* ──────────── USB Constants ──────────── */

/* USB Transfer Types (defined in bhplus_types.h — imported above)
 * BHPLUS_USB_TRANSFER_ISOCHRONOUS = 0  (USBPcap/Windows USB mapping)
 * BHPLUS_USB_TRANSFER_INTERRUPT   = 1
 * BHPLUS_USB_TRANSFER_CONTROL     = 2
 * BHPLUS_USB_TRANSFER_BULK        = 3
 */

/* USB Endpoint Direction */
#define BHPLUS_USB_DIR_OUT               0x00
#define BHPLUS_USB_DIR_IN                0x80

/* USB Standard Request Codes */
#define BHPLUS_USB_REQ_GET_STATUS        0x00
#define BHPLUS_USB_REQ_CLEAR_FEATURE     0x01
#define BHPLUS_USB_REQ_SET_FEATURE       0x03
#define BHPLUS_USB_REQ_SET_ADDRESS       0x05
#define BHPLUS_USB_REQ_GET_DESCRIPTOR    0x06
#define BHPLUS_USB_REQ_SET_DESCRIPTOR    0x07
#define BHPLUS_USB_REQ_GET_CONFIGURATION 0x08
#define BHPLUS_USB_REQ_SET_CONFIGURATION 0x09
#define BHPLUS_USB_REQ_GET_INTERFACE     0x0A
#define BHPLUS_USB_REQ_SET_INTERFACE     0x0B

/* USB Descriptor Types */
#define BHPLUS_USB_DESC_DEVICE           0x01
#define BHPLUS_USB_DESC_CONFIGURATION    0x02
#define BHPLUS_USB_DESC_STRING           0x03
#define BHPLUS_USB_DESC_INTERFACE        0x04
#define BHPLUS_USB_DESC_ENDPOINT         0x05
#define BHPLUS_USB_DESC_HID              0x21
#define BHPLUS_USB_DESC_HID_REPORT       0x22

/* ──────────── SCSI Constants ──────────── */

#define BHPLUS_SCSI_TEST_UNIT_READY      0x00
#define BHPLUS_SCSI_REQUEST_SENSE        0x03
#define BHPLUS_SCSI_INQUIRY              0x12
#define BHPLUS_SCSI_MODE_SELECT_6        0x15
#define BHPLUS_SCSI_MODE_SENSE_6         0x1A
#define BHPLUS_SCSI_READ_CAPACITY_10     0x25
#define BHPLUS_SCSI_READ_10              0x28
#define BHPLUS_SCSI_WRITE_10             0x2A
#define BHPLUS_SCSI_READ_16              0x88
#define BHPLUS_SCSI_WRITE_16             0x8A
#define BHPLUS_SCSI_READ_CAPACITY_16     0x9E

/* ──────────── NVMe Constants ──────────── */

/* NVMe Admin Commands */
#define BHPLUS_NVME_ADMIN_DELETE_SQ       0x00
#define BHPLUS_NVME_ADMIN_CREATE_SQ       0x01
#define BHPLUS_NVME_ADMIN_GET_LOG_PAGE    0x02
#define BHPLUS_NVME_ADMIN_DELETE_CQ       0x04
#define BHPLUS_NVME_ADMIN_CREATE_CQ       0x05
#define BHPLUS_NVME_ADMIN_IDENTIFY        0x06
#define BHPLUS_NVME_ADMIN_ABORT           0x08
#define BHPLUS_NVME_ADMIN_SET_FEATURES    0x09
#define BHPLUS_NVME_ADMIN_GET_FEATURES    0x0A
#define BHPLUS_NVME_ADMIN_ASYNC_EVENT     0x0C
#define BHPLUS_NVME_ADMIN_FW_COMMIT       0x10
#define BHPLUS_NVME_ADMIN_FW_DOWNLOAD     0x11

/* NVMe I/O Commands */
#define BHPLUS_NVME_IO_FLUSH              0x00
#define BHPLUS_NVME_IO_WRITE              0x01
#define BHPLUS_NVME_IO_READ               0x02
#define BHPLUS_NVME_IO_COMPARE            0x05
#define BHPLUS_NVME_IO_DATASET_MGMT       0x09

/* ──────────── ATA Constants ──────────── */

#define BHPLUS_ATA_IDENTIFY_DEVICE        0xEC
#define BHPLUS_ATA_READ_DMA_EXT           0x25
#define BHPLUS_ATA_WRITE_DMA_EXT          0x35
#define BHPLUS_ATA_READ_FPDMA             0x60
#define BHPLUS_ATA_WRITE_FPDMA            0x61
#define BHPLUS_ATA_SMART                  0xB0
#define BHPLUS_ATA_SET_FEATURES           0xEF
