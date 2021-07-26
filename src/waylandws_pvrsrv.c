/*
 * @File           waylandws_pvrsrv.c
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

/*
 * PVR functions for PVRSRV
 */

#include <stdlib.h>

#include "waylandws_pvr.h"

/* for PVRSRV context */
struct pvr_map {
	PVRSRV_MEMDESC		memdesc;
	IMG_DEV_VIRTADDR	vaddr;
};

struct pvr_context __attribute__((visibility("internal")))
*pvr_connect(PVRSRV_DEV_CONNECTION **ppsDeviceConnection)
{
	static struct pvr_context context = {
		.status  = PVR_STATUS_NOTREADY,
		.count	 = 0,
	};

	if (context.status == PVR_STATUS_ERROR)
		return NULL;

	/* initialize */
	if (context.status == PVR_STATUS_NOTREADY) {
		if (!PVRSRVConnectExt(&context.connection))
			goto error;

		if (!PVRSRVCreateDeviceMemContextExt(context.connection, &context.rgx_devmem_context, &context.devmem_context))

			goto error;

		if (!PVRSRVFindHeapExt(context.devmem_context, &context.heap))
			goto error;

		if (!PVRSRVAcquireGlobalEventHandleExt(context.connection, &context.event))
			goto error;

		context.status = PVR_STATUS_READY;
	}

	*ppsDeviceConnection = context.connection;
	context.count++;
	return &context;

error:
	context.status = PVR_STATUS_ERROR;
	return NULL;
}

void __attribute__((visibility("internal"))) pvr_disconnect(struct pvr_context *context)
{
	if (context && context->count > 0) {
		context->count--;

		if (context->count == 0) {
			PVRSRVReleaseGlobalEventHandleExt(context->connection, context->event);
			PVRSRVReleaseDeviceMemContextExt(context->rgx_devmem_context ,context->devmem_context);
			PVRSRVDisconnectExt(context->connection);
			context->connection = NULL;
			context->status = PVR_STATUS_NOTREADY;;
		}
	}
}

static struct pvr_map *pvr_map_to_device(struct pvr_context *context,
					 PVRSRV_MEMDESC memdesc, IMG_DEVMEM_SIZE_T size)
{
	struct pvr_map *map;
	WSEGL_UNREFERENCED_PARAMETER(size);

	if (!(map = calloc(1, sizeof(struct pvr_map))))
		return NULL;

	/* Map it into Rogue address space */
	if (!PVRSRVMapToDeviceExt(memdesc, context->heap, &map->vaddr)) {
		WSEGL_DEBUG("%s: %s: PVRSRVMapToDeviceExt() failed\n", __FILE__, __func__);
		goto error;
	}

	map->memdesc = memdesc;

	return map;

error:
	if (map->memdesc) {
		PVRSRVReleaseDeviceMappingExt(map->memdesc);
	}

	free(map);

	return NULL;
}

struct pvr_map __attribute__((visibility("internal"))) *pvr_map_memory(struct pvr_context *context, void *addr, int size)
{
	struct pvr_map *map;
	PVRSRV_MEMDESC memdesc;

	/* wrap external memroy... */
	if (!PVRSRVWrapExtMemExt(context->devmem_context, size, addr, 4096, "", &memdesc)) {
		WSEGL_DEBUG("%s: %s: PVRSRVWrapExtMemExt() failed\n", __FILE__, __func__);
		return NULL;
	}

	map = pvr_map_to_device(context, memdesc, size);
	if (!map) {
		PVRSRVFreeDeviceMemExt(context->connection ,memdesc);
		return NULL;
	}

	return map;
}

struct pvr_map __attribute__((visibility("internal"))) *pvr_map_dmabuf(struct pvr_context *context, int fd, const char *name)
{
	struct pvr_map *map;
	PVRSRV_MEMDESC memdesc;
	IMG_DEVMEM_SIZE_T size;

	if (!PVRSRVDmaBufImportDevMemExt(context->connection, fd, &memdesc, &size, name)) {
		WSEGL_DEBUG("%s: %s: PVRSRVDmaBufImportDevMemExt() failed\n",
			    __FILE__, __func__);
		return NULL;
	}

	map = pvr_map_to_device(context, memdesc, size);
	if (!map) {
		PVRSRVFreeDeviceMemExt(context->connection ,memdesc);
		return NULL;
	}

	return map;
}

void __attribute__((visibility("internal"))) pvr_unmap_memory(struct pvr_context *context, struct pvr_map *map)
{
	WSEGL_UNREFERENCED_PARAMETER(context);

	if (!map)
		return;

	if (map->memdesc) {
		PVRSRVReleaseDeviceMappingExt(map->memdesc);
		PVRSRVFreeDeviceMemExt(context->connection ,map->memdesc);
	}
	free(map);
}

void __attribute__((visibility("internal"))) pvr_get_params(struct pvr_map *map, WLWSDrawableInfo *info, WSEGLDrawableParams *params)
{
	params->sBase.iWidth            = info->width;
	params->sBase.iHeight           = info->height;
	params->sBase.ePixelFormat      = info->pixelformat;
	params->sBase.eFBCompression    = IMG_FB_COMPRESSION_NONE;
	params->sBase.eMemLayout        = IMG_MEMLAYOUT_STRIDED;
	params->sBase.ui32StrideInBytes = info->pitch;
	params->sBase.asHWAddress[0]	= map->vaddr;
	params->sBase.ahMemDesc[0]	= map->memdesc;
	params->eRotationAngle          = WLWSEGL_ROTATE_0;
	/* Don't set sync object to psServerSync if buffer sync is used
	   (use WSEGL_FLAGS_DRAWABLE_BUFFER_SYNC flag). */
	if (info->ui32DrawableType == WSEGL_DRAWABLE_WINDOW)
		params->sBase.ui32Flags = WSEGL_FLAGS_DRAWABLE_BUFFER_SYNC;
	params->sBase.hFence            = PVRSRV_NO_FENCE;
}

void __attribute__((visibility("internal"))) pvr_get_image_params(struct pvr_map *map, WLWSDrawableInfo *info, WSEGLImageParams *params)
{
	params->sBase.iWidth            = info->width;
	params->sBase.iHeight           = info->height;
	params->sBase.ePixelFormat      = info->pixelformat;
	params->sBase.eFBCompression    = IMG_FB_COMPRESSION_NONE;
	params->sBase.eMemLayout        = IMG_MEMLAYOUT_STRIDED;
	params->sBase.ui32StrideInBytes = info->pitch;

	params->sBase.asHWAddress[0]    = map->vaddr;
	params->sBase.ahMemDesc[0]      = map->memdesc;
	params->sBase.auiAllocSize[0]   = info->size;

	params->sBase.hFence            = PVRSRV_NO_FENCE;

	/* [RELCOMMENT P-0149] Set YUV attributes.*/
	/* begin */
	params->sBase.eYUVColorspace = info->eColorSpace;
	params->eChromaUInterp = info->eChromaUInterp;
	params->eChromaVInterp = info->eChromaVInterp;
	/* end */
}

int __attribute__((visibility("internal"))) pvr_acquire_cpu_mapping(PVRSRV_MEMDESC hMemDesc, void **ppvCpuVirtAddr)
{
	return PVRSRVAcquireCPUMappingExt(hMemDesc, ppvCpuVirtAddr);
}

void __attribute__((visibility("internal"))) pvr_release_cpu_mapping(PVRSRV_MEMDESC hMemDesc)
{
	PVRSRVReleaseCPUMappingExt(hMemDesc);
}
