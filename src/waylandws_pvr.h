/*
 * @File           waylandws_pvr.h
 * @Copyright      Copyright (C) 2021 Renesas Electronics Corporation. All rights reserved.
 * @License        MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef __waylandws_pvr_h__
#define __waylandws_pvr_h__

#include "waylandws.h"

typedef enum {
        PVR_STATUS_ERROR = -1,
        PVR_STATUS_NOTREADY = 0,
        PVR_STATUS_READY = 1,
} pvr_status;

/* for PVRSRV context */
struct pvr_context {
        PVRSRV_DEV_CONNECTION   *connection;
        PVRSRV_HEAP             heap;
        PVRSRV_DEVMEMCTX        devmem_context;
        PRGX_DEVMEMCONTEXT      rgx_devmem_context;

        void                    *event;

        pvr_status              status;
        int                     count;
};

struct pvr_map;

/**
 * Connect to the PVR service.
 */
extern struct pvr_context *pvr_connect(PVRSRV_DEV_CONNECTION **ppsDeviceConnection);

/**
 * Disconnect from the PVR service.
 */
extern void pvr_disconnect(struct pvr_context *ctx);

/**
 * Map memory to the PVR context.
 */
extern struct pvr_map *pvr_map_memory(struct pvr_context *ctx, void *addr, int size);

/**
 * Map a dmabuf fd to the PVR context.
 */
extern struct pvr_map *pvr_map_dmabuf(struct pvr_context *context, int fd, const char *name);

/**
 * Unmap memory from the PVR context.
 */
extern void pvr_unmap_memory(struct pvr_context *ctx, struct pvr_map *map);

/**
 * Fill in details required for drawable params
 */
extern void pvr_get_params(struct pvr_map *map, WLWSDrawableInfo *info, WSEGLDrawableParams *params);

/**
 * Sync with GPU
 */
extern void pvr_sync(struct pvr_context *ctx, struct pvr_map *map);

/**
 * Fill in details required for image params
 */
extern void pvr_get_image_params(struct pvr_map *map, WLWSDrawableInfo *info, WSEGLImageParams *params);

/**
 * Request the CPU virtual address
 */
extern int pvr_acquire_cpu_mapping(PVRSRV_MEMDESC hMemDesc, void **ppvCpuVirtAddr);

/**
 * Release the CPU virtual address
 */
extern void pvr_release_cpu_mapping(PVRSRV_MEMDESC hMemDesc);

/**
 * Get settings value from powervr.ini
 */
static inline int pvr_get_config_value(const char *key)
{
	void *pvHintState;
	int ret, value, def_val = 0;

	PVRSRVCreateAppHintStateExt(NULL, &pvHintState);
	if (!pvHintState)
		return -1;

	ret = PVRSRVGetAppHintUintExt(pvHintState, key, &def_val, &value);
	PVRSRVFreeAppHintStateExt(pvHintState);

	/* key is not found */
	if (ret == false)
		return -1;

	return value;
}

#endif /*! __waylandws_pvr_h__ */
