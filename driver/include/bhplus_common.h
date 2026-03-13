#pragma once

/*
 * USBPcapGUI - Driver internal common header
 * Re-exports shared definitions for kernel use.
 */

/* In kernel mode, define _KERNEL_MODE before including shared headers */
#ifndef _KERNEL_MODE
#define _KERNEL_MODE
#endif

#include "../../shared/include/bhplus_types.h"
#include "../../shared/include/bhplus_ioctl.h"
#include "../../shared/include/bhplus_protocol.h"
