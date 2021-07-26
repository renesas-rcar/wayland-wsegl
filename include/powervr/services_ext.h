/*************************************************************************/ /*!
@File
@Title          Public services interfaces
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
                Copyright (c) 2021 Renesas Electronics Corporation. All rights reserved.
@License        MIT

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/ /**************************************************************************/

#ifndef POWERVR_SERVICES_H
#define POWERVR_SERVICES_H

#if defined (__cplusplus)
extern "C" {
#endif

#include <stdbool.h>
#include <powervr/mem_types.h>
#include <powervr/pvrsrv_sync_ext.h>
#include <powervr/buffer_attribs.h>
#include <powervr/imgpixfmts.h>

#ifndef PVRSRV_DEV_CONNECTION_TYPEDEF
#define PVRSRV_DEV_CONNECTION_TYPEDEF
typedef struct PVRSRV_DEV_CONNECTION_TAG PVRSRV_DEV_CONNECTION;
#endif
#ifndef PVRSRV_DEV_MEM_TYPEDEFS
#define PVRSRV_DEV_MEM_TYPEDEFS
typedef struct PVRSRV_DEVMEMCTX_TAG *PVRSRV_DEVMEMCTX;
typedef struct DEVMEM_HEAP_TAG *PVRSRV_HEAP;
typedef struct DEVMEM_MEMDESC_TAG *PVRSRV_MEMDESC;
#endif
#ifndef RGX_DEV_MEM_TYPEDEFS
#define RGX_DEV_MEM_TYPEDEFS
typedef struct RGX_DEVMEMCONTEXT_TAG *PRGX_DEVMEMCONTEXT;
#endif
/* SERVICES ******************************************************************/
bool PVRSRVConnectExt(PVRSRV_DEV_CONNECTION **ppsDevConnection);

bool PVRSRVDisconnectExt(PVRSRV_DEV_CONNECTION *psDevConnection);

bool PVRSRVAcquireGlobalEventHandleExt(const PVRSRV_DEV_CONNECTION *psDevConnection,
                                       void **ppvEvent);

bool PVRSRVReleaseGlobalEventHandleExt(const PVRSRV_DEV_CONNECTION *psDevConnection,
                                       void *pvEvent);

bool PVRSRVEventObjectWaitExt(const PVRSRV_DEV_CONNECTION *psDevConnection,
                              void *pvEvent,
                              bool *pbSignalled);

bool PVRSRVEventObjectWaitTimeoutExt(const PVRSRV_DEV_CONNECTION *psDevConnection,
                                     void *pvEvent,
                                     uint64_t ui64Timeoutus,
                                     bool *pbSignalled);


/* PVRSRV_SYNC_UM *************************************************************/
bool PVRSRVFenceWaitExt(PVRSRV_DEV_CONNECTION *psDevConnection,
                        PVRSRV_FENCE hFence,
                        uint32_t ui32TimeoutInMs,
                        bool *pbFenceMet);

bool PVRSRVFenceDupExt(PVRSRV_DEV_CONNECTION *psDevConnection,
                       PVRSRV_FENCE hSourceFence,
                       PVRSRV_FENCE *phOutputFence);

bool PVRSRVFenceDestroyExt(PVRSRV_DEV_CONNECTION *psDevConnection,
                           PVRSRV_FENCE hFence);

/* PVRSRV_DEVMEM *************************************************************/

bool PVRSRVCreateDeviceMemContextExt(
		PVRSRV_DEV_CONNECTION *psDevConnection,
		PRGX_DEVMEMCONTEXT *phRGXDevMemCtxOut,
		PVRSRV_DEVMEMCTX *phDevMemCtxOut);

void PVRSRVReleaseDeviceMemContextExt(
		PRGX_DEVMEMCONTEXT hRGXDevMemCtx,
		PVRSRV_DEVMEMCTX hDevMemCtx);

bool PVRSRVFindHeapExt(
		PVRSRV_DEVMEMCTX hCtx,
		PVRSRV_HEAP *phHeapOut);


/* PVRSRV_DEVMEM_EXTMEM ******************************************************/

/* Imports any dynamically allocated memory associated with user mode CPU
 * virtual address mapping to the GPU domain.
 * If CPU mapping to the memory is obtained using Services API on a hMemDesc
 * a pointer to an uncached write-combine mapping will be returned. */
bool PVRSRVWrapExtMemExt(const PVRSRV_DEVMEMCTX psDevMemCtx,
                         IMG_DEVMEM_SIZE_T uiSize,
                         IMG_CPU_VIRTADDR pvCpuVAddr,
                         IMG_DEVMEM_ALIGN_T uiAlign,
                         char *pszText,
                         PVRSRV_MEMDESC *hMemDesc);


/* PVRSRV_DEVMEM_DMABUF ******************************************************/
#if defined(__linux__)

bool PVRSRVDmaBufExportDevMemExt(PVRSRV_MEMDESC hMemDesc,
                                 int *piFd);

/* Import a DMA-BUF allocation.
 * If CPU mapping to the memory is obtained using Services API on a phMemDescPtr
 * a pointer to an uncached write-combine mapping will be returned. */
bool PVRSRVDmaBufImportDevMemExt(const PVRSRV_DEV_CONNECTION *psDevConnection,
                                 int fd,
                                 PVRSRV_MEMDESC *phMemDescPtr,
                                 IMG_DEVMEM_SIZE_T *puiSizePtr,
                                 const char *pszName);

bool PVRSRVFreeDeviceMemExt(const PVRSRV_DEV_CONNECTION *psDevConnection,
                                 PVRSRV_MEMDESC hMemDesc);

/* Allocate a DMABuf by doing a Services allocation and export it
 * to become a DMABuf or on Android do a gralloc and import the
 * result into services.
 * If CPU mapping to the memory is obtained using Services API on a phMemDescPtr
 * a pointer to an uncached write-combine mapping will be returned. */
bool PVRSRVDMABufAllocDevMemExt(const PVRSRV_DEV_CONNECTION *psDevConnection,
                                IMG_DEVMEM_SIZE_T uiSize,
                                IMG_DEVMEM_LOG2ALIGN_T uiLog2Align,
                                char *pszName,
                                int *fd,
                                PVRSRV_MEMDESC *phMemDescPtr);

bool PVRSRVDMABufReleaseDevMemExt(const PVRSRV_DEV_CONNECTION *psDevConnection,
                                  PVRSRV_MEMDESC hMemDesc,
                                  int fd);

#endif

bool PVRSRVAcquireCPUMappingExt(PVRSRV_MEMDESC hMemDesc,
                                void **ppvCpuVirtAddrOut);

void PVRSRVReleaseCPUMappingExt(PVRSRV_MEMDESC hMemDesc);

bool PVRSRVMapToDeviceExt(PVRSRV_MEMDESC hMemDesc,
                          PVRSRV_HEAP hHeap,
                          IMG_DEV_VIRTADDR *psDevVirtAddrOut);

void PVRSRVReleaseDeviceMappingExt(PVRSRV_MEMDESC hMemDesc);

void PVRSRVCreateAppHintStateExt(const char *pszAppName,
								 void **ppvState);

void PVRSRVFreeAppHintStateExt(void *pvHintState);


bool PVRSRVGetAppHintUintExt(void *pvHintState,
						 const char *pszHintName,
						 const void *pvDefault,
						 void *pvReturn);

bool PVRSRVGetAppHintStringExt(void *pvHintState,
						 const char *pszHintName,
						 const void *pvDefault,
						 void *pvReturn);

bool PVRSRVCreateTransferContextExt(PVRSRV_DEVMEMCTX hDevMemContext,
									void *hTransferContext);

bool PVRSRVDestroyTransferContextExt(void *hTransferContext);

#if defined (__cplusplus)
}
#endif


#endif
