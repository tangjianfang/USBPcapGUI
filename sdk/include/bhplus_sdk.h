#pragma once

/*
 * USBPcapGUI - Public SDK API
 * C API for automation and integration.
 */

#include "bhplus_types.h"

#ifdef BHPLUS_SDK_EXPORTS
#define BHPLUS_API __declspec(dllexport)
#else
#define BHPLUS_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────── Session Management ──────────── */

typedef void* BHPLUS_HANDLE;

BHPLUS_API BHPLUS_HANDLE BHPlus_Open(void);
BHPLUS_API void          BHPlus_Close(BHPLUS_HANDLE handle);
BHPLUS_API int           BHPlus_IsDriverLoaded(BHPLUS_HANDLE handle);

/* ──────────── Capture Control ──────────── */

BHPLUS_API int  BHPlus_StartCapture(BHPLUS_HANDLE handle, const BHPLUS_CAPTURE_CONFIG* config);
BHPLUS_API int  BHPlus_StopCapture(BHPLUS_HANDLE handle);
BHPLUS_API int  BHPlus_IsCapturing(BHPLUS_HANDLE handle);

/* ──────────── Event Reading ──────────── */

typedef void (*BHPLUS_EVENT_CALLBACK)(const BHPLUS_CAPTURE_EVENT* event,
                                      const unsigned char* data,
                                      void* userData);

BHPLUS_API void BHPlus_SetEventCallback(BHPLUS_HANDLE handle,
                                         BHPLUS_EVENT_CALLBACK callback,
                                         void* userData);

/* ──────────── Device Enumeration ──────────── */

BHPLUS_API int BHPlus_EnumerateDevices(BHPLUS_HANDLE handle,
                                        BHPLUS_DEVICE_INFO* devices,
                                        int maxDevices);

/* ──────────── Command Sending ──────────── */

BHPLUS_API int BHPlus_SendCommand(BHPLUS_HANDLE handle,
                                   unsigned int deviceId,
                                   const unsigned char* command,
                                   unsigned int commandLength,
                                   unsigned char* response,
                                   unsigned int* responseLength,
                                   unsigned int timeoutMs);

/* ──────────── Device Reset ──────────── */

BHPLUS_API int BHPlus_ResetDevice(BHPLUS_HANDLE handle, unsigned int deviceId);

/* ──────────── Statistics ──────────── */

BHPLUS_API int BHPlus_GetStatistics(BHPLUS_HANDLE handle, BHPLUS_STATS* stats);

#ifdef __cplusplus
}
#endif
