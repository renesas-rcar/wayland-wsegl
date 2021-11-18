/*
 * @File           waylandws_server.c
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

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <xf86drm.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "waylandws.h"
#include "waylandws_server.h"
#include "waylandws_pvr.h"

#include "wayland-server.h"
#include "wayland-kms-server-protocol.h"
#include "wayland-kms.h" 

#include "gbm_kmsint.h"

#include "EGL/egl.h"
#include "EGL/eglext_REL.h"

/*
 * Capabilities of the wayland window system
 */
static const WSEGLCaps WLWSEGL_Caps[] =
{
	{ WSEGL_CAP_WINDOWS_USE_HW_SYNC, 0 },
	{ WSEGL_CAP_PIXMAPS_USE_HW_SYNC, 1 },
	{ WSEGL_CAP_IMAGE_EXTERNAL_SUPPORT, 1 },
	{ WSEGL_NO_CAPS, 0 }
};

/*
 * Private window system display information
 */
typedef struct WaylandWS_Server_Display_TAG
{
	/* For gbm display */
	struct gbm_device	*gbm;
	int			fd;

	/* PVR context */
	struct pvr_context	*context;
} WLWSServerDisplay;

/*
 * N.B. The code is optimized for double buffer.
 * V4l2-renderer w/ gl-renderer in weston also consider
 * double buffer only.
 */
#define MAX_BACK_BUFFERS 2

struct gbm_buffer {
	struct gbm_kms_bo	*bo;		/* GBM BO (for window only) */

	int			locked;

	/* PVR memory map */
	struct pvr_map		*map;
	int			dmafd;

	int			buffer_age;
	bool			allocated;
};

/*
 * Private window system drawable information
 */
typedef struct WaylandWS_Server_Drawable_TAG
{
	struct wl_egl_window	*window;
	struct gbm_kms_surface	*surface;

	WLWSDrawableInfo	info;

	/* for Wayland window resize */
	int			dx;
	int			dy;

	struct gbm_buffer	buffers[MAX_BACK_BUFFERS];
	struct gbm_buffer	*current;
	struct gbm_buffer	*source;
	int			count;
	int			num_bufs;

	WLWSServerDisplay	*display;

	int			ref_count;
	int			pixmap_kms_buffer_in_use;
	struct wl_listener	kms_buffer_destroy_listener;
} WLWSServerDrawable;

/***********************************************************************************
 Function Name      : WSEGL_InitialiseDisplay
 Inputs             : hNativeDisplay
 Outputs            : phDisplay, psCapabilities, psConfigs
 Returns            : Error code
 Description        : Initialises a display
************************************************************************************/
static WSEGLError WSEGLs_InitialiseDisplay(NativeDisplayType hNativeDisplay,
					  WSEGLDisplayHandle *phDisplay,
					  const WSEGLCaps **psCapabilities,
					  WSEGLConfig **psConfigs,
					  PVRSRV_DEV_CONNECTION **ppsDevConnection)
{
	WLWSServerDisplay *display;

	if (!(display = calloc(1, sizeof(WLWSServerDisplay))))
		return WSEGL_OUT_OF_MEMORY;

	/*
	 * Server side initialization of Wayland KMS is done in eglBindWaylandDisplayWL().
	 * We cannot handle wl_buffer, until it's been initialized anyway.
	 */

	/* we are the compositor */
	display->gbm = (struct gbm_device*)hNativeDisplay; 
	display->fd = gbm_device_get_fd(display->gbm);
	
	/* Create a PVR2D context w/ invalid device index. */
	if (!(display->context = pvr_connect(ppsDevConnection))) {
		free(display);
		return WSEGL_CANNOT_INITIALISE;
	}

	/* TODO: check supported pixelformat and set it in the capability list */

	/* TODO: ref counter? */

	/* return the pointers to the caps, configs, and the display handle */
	*psCapabilities	= WLWSEGL_Caps;
	*psConfigs	= WLWSEGL_Configs;
	*phDisplay	= (WSEGLDisplayHandle)display;

	WSEGL_DEBUG("%s: %s: returning %p\n", __FILE__, __func__, display);

	return WSEGL_SUCCESS;
}


/***********************************************************************************
 Function Name      : WSEGL_CloseDisplay
 Inputs             : hDisplay
 Outputs            : None
 Returns            : Error code
 Description        : Closes a display
************************************************************************************/
static WSEGLError WSEGLs_CloseDisplay(WSEGLDisplayHandle hDisplay)
{
	WLWSServerDisplay *display = (WLWSServerDisplay*)hDisplay;
	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	/* TODO: Ref Counter? */

	pvr_disconnect(display->context);

	free(display);

	return WSEGL_SUCCESS;
}

static void _gbm_destroy_drawable(WLWSServerDrawable *drawable)
{
	int i;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);
	if (!drawable)
		return;

	for (i = 0; i < drawable->num_bufs; i++) {
		WSEGL_DEBUG("%s: %s: %d: buffers[%d].meminfo=%p\n", __FILE__, __func__, __LINE__, i, drawable->buffers[i].map);
		if (drawable->buffers[i].map) {
			pvr_unmap_memory(drawable->display->context, drawable->buffers[i].map);
		}

		if (drawable->buffers[i].bo) {
			if (drawable->buffers[i].allocated)
				gbm_bo_destroy((struct gbm_bo*)drawable->buffers[i].bo);
			drawable->buffers[i].bo = NULL;
		}
		if (drawable->buffers[i].dmafd)
			close(drawable->buffers[i].dmafd);
	}

	free(drawable);
}

/***********************************************************************************
 Function Name      : WSEGL_CreateWindowDrawable
 Inputs             : hDisplay, psConfig, hNativeWindow
 Outputs            : phDrawable, eRotationAngle
 Returns            : Error code
 Description        : Create a window drawable for a native window
************************************************************************************/
static WSEGLError WSEGLs_CreateWindowDrawable(WSEGLDisplayHandle hDisplay,
					     WSEGLConfig *psConfig,
					     WSEGLDrawableHandle *phDrawable,
					     NativeWindowType hNativeWindow,
					     WLWSEGL_ROTATION *eRotationAngle,
					     WLWSEGL_COLOURSPACE_FORMAT eColorSpace,
					     bool bIsProtected)
{
	WLWSServerDisplay *display = (WLWSServerDisplay*)hDisplay;
	WLWSServerDrawable *drawable;
	struct gbm_kms_surface *surface;
	int i;
	WSEGL_UNREFERENCED_PARAMETER(psConfig);
	WSEGL_UNREFERENCED_PARAMETER(eColorSpace);
	WSEGL_UNREFERENCED_PARAMETER(bIsProtected);

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	if (!hNativeWindow)
		return WSEGL_BAD_NATIVE_WINDOW;

	if (!(drawable = calloc(sizeof(WLWSServerDrawable), 1)))
		return WSEGL_OUT_OF_MEMORY;

	/*
	 * For GBM, we get gbm_surface.
         *
	 * For gbm_surface, we have to wrap the BO with PVR2DMemWrap, so that we can
	 * render into BO allocated by gbm_create_surface().
	 */
	surface = gbm_kms_surface((struct gbm_surface*)hNativeWindow);
	drawable->info.ui32DrawableType = WSEGL_DRAWABLE_WINDOW;
	drawable->surface	   = surface;
	drawable->info.width	   = surface->base.width;
	drawable->info.height      = surface->base.height;
	drawable->info.pixelformat = WLWSEGL_PIXFMT_ARGB8888;
	drawable->display = display;

	WSEGL_DEBUG("%s: %s: %d: %dx%d\n", __FILE__, __func__, __LINE__, drawable->info.width, drawable->info.height);
	for (i = 0; i < MAX_BACK_BUFFERS; i++) {
		drawable->buffers[i].allocated = false;
		if (surface->bo[i]) {
			drawable->buffers[i].bo = surface->bo[i];
		} else {
			drawable->buffers[i].bo = surface->bo[i] = (struct gbm_kms_bo*)
				gbm_bo_create(surface->base.gbm,
					      surface->base.width, surface->base.height,
					      surface->base.format,
					      surface->base.flags | GBM_BO_USE_WRITE);
			if (!drawable->buffers[i].bo)
				goto error;
			drawable->buffers[i].allocated = true;
		}

		if (!drawable->info.pitch) {
			// XXX: need to do this better. fixed for 32bpp for now.
			drawable->info.pitch = drawable->buffers[i].bo->base.stride;
			drawable->info.stride = drawable->buffers[i].bo->base.stride / 4;
		}
		WSEGL_DEBUG("%s: %s: %d: %p (size=%d)\n", __FILE__, __func__, __LINE__, drawable->buffers[i].bo->addr, drawable->buffers[i].bo->size);

		if (!(drawable->buffers[i].map = pvr_map_dmabuf(display->context, drawable->buffers[i].bo->fd, SERVER_PVR_MAP_NAME))) {
			WSEGL_DEBUG("%s: %s: pvr_map_dmabuf() failed.\n", __FILE__, __func__);
			goto error;
		}
	}
	drawable->num_bufs = MAX_BACK_BUFFERS;
	drawable->current = &drawable->buffers[0];

	drawable->ref_count = 1;

	/*
	 * XXX: nothing to do here anymore?
	 *
	 * We coulda get more details of BO, and map all BO to PVR2D now rather than later.
	 */

	// No rotation
	*eRotationAngle = WLWSEGL_ROTATE_0;

	*phDrawable = (WSEGLDrawableHandle)drawable;

	return WSEGL_SUCCESS;

error:
	_gbm_destroy_drawable(drawable);
	return WSEGL_CANNOT_INITIALISE;
}

static void _kms_buffer_destroy_callback(struct wl_listener *listener, void *data)
{
	WLWSServerDrawable *drawable;
	WSEGL_UNREFERENCED_PARAMETER(data);

	drawable = wl_container_of(listener, drawable,
				   kms_buffer_destroy_listener);

	/* If DeleteDrawable() is called before this callback,
	   drawable should be destroyed. */
	if (drawable->ref_count == 0) {
		_gbm_destroy_drawable(drawable);
	} else {
		drawable->pixmap_kms_buffer_in_use = 0;
		drawable->kms_buffer_destroy_listener.notify = NULL;
	}
}

static inline IMG_YUV_COLORSPACE convert_format_to_color_space(uint32_t format)
{
	switch (format & D_MASK_YUV_COLORSPACE) {
	case EGL_YUV_COLORSPACE_BT601_CONFORMANT_RANGE_REL:
		return WLWSEGL_YUV_COLORSPACE_CONFORMANT_BT601;
	case EGL_YUV_COLORSPACE_BT709_CONFORMANT_RANGE_REL:
		return WLWSEGL_YUV_COLORSPACE_CONFORMANT_BT709;
	case EGL_YUV_COLORSPACE_BT709_FULL_RANGE_REL:
		return WLWSEGL_YUV_COLORSPACE_FULL_BT709;
	default:
		return WLWSEGL_YUV_COLORSPACE_FULL_BT601;
	}
}

/***********************************************************************************
 Function Name      : WSEGL_CreatePixmapDrawable
 Inputs             : hDisplay, psConfig, hNativePixmap
 Outputs            : phDrawable, eRotationAngle
 Returns            : Error code
 Description        : Create a pixmap drawable for a native pixmap
************************************************************************************/
static WSEGLError WSEGLs_CreatePixmapDrawable(WSEGLDisplayHandle hDisplay, 
						 WSEGLConfig *psConfig, 
						 WSEGLDrawableHandle *phDrawable, 
						 NativePixmapType hNativePixmap, 
						 WLWSEGL_ROTATION *eRotationAngle,
						 WLWSEGL_COLOURSPACE_FORMAT eColorSpace,
						 bool bIsProtected)
{
	WLWSServerDisplay *display = (WLWSServerDisplay*)hDisplay;
	WLWSServerDrawable *drawable;
	struct wl_kms_buffer *buffer;
	int fd;
	WSEGL_UNREFERENCED_PARAMETER(psConfig);
	WSEGL_UNREFERENCED_PARAMETER(eRotationAngle);
	WSEGL_UNREFERENCED_PARAMETER(eColorSpace);
	WSEGL_UNREFERENCED_PARAMETER(bIsProtected);

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	buffer = wayland_kms_buffer_get((struct wl_resource*)hNativePixmap);

	if (buffer->private) {
		drawable = (WLWSServerDrawable*)buffer->private;
		drawable->ref_count++;
		*phDrawable = (WSEGLDrawableHandle)drawable;
		return WSEGL_SUCCESS;
	}

	if (!(drawable = calloc(sizeof(WLWSServerDrawable), 1)))
		return WSEGL_OUT_OF_MEMORY;

	drawable->info.ui32DrawableType = WSEGL_DRAWABLE_PIXMAP;
	drawable->current = drawable->source = &drawable->buffers[0];
	drawable->num_bufs = 1;
	drawable->display = display;

	/*
	 * XXX: Do we need to be able to handle non-Wayland Pixmap as well,
	 * i.e. something other than EGL_WAYLAND_BUFFER_WL?
	 */

	/*
	 * TODO: We have to be able to import wl_buffer passsed in hNativePixmap.
	 *
	 * The easiest is to import wl_buffer with gbm_bo_import(), and use the BO
	 * internally. We probably need to handle surfaces the same way in
	 * CreateNativeWidnow(), so that we can handle things pretty much the same way...
	 * maybe not. We'll see.
	 */

	drawable->info.width  = buffer->width;
	drawable->info.height = buffer->height;

	switch(buffer->format) {
	case WL_KMS_FORMAT_ARGB8888:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_ARGB8888;
		drawable->info.size = buffer->stride * buffer->height;
		drawable->info.stride = buffer->stride / 4;
		drawable->info.pitch  = buffer->stride;
		break;
	case WL_KMS_FORMAT_XRGB8888:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_XRGB8888;
		drawable->info.size = buffer->stride * buffer->height;
		drawable->info.stride = buffer->stride / 4;
		drawable->info.pitch  = buffer->stride;
		break;
	case WL_KMS_FORMAT_NV12:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_NV12;
		drawable->info.size = buffer->stride * buffer->height * 3 / 2;
		drawable->info.stride = buffer->stride;
		drawable->info.pitch  = buffer->stride;
		drawable->info.eColorSpace = convert_format_to_color_space(buffer->format);
		break;
	case WL_KMS_FORMAT_NV16:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_NV16;
		drawable->info.size = buffer->stride * buffer->height * 2;
		drawable->info.stride = buffer->stride;
		drawable->info.pitch  = buffer->stride * 2;
		drawable->info.eColorSpace = convert_format_to_color_space(buffer->format);
		break;
	default:
		goto error;
	}

	WSEGL_DEBUG("%s: %s: %d: buffer = %p (%dx%d, stride(pitch in wsegl)=%d, size=%d, format=%08x, handle=%d, eColorSpace=%d)\n",
			__FILE__, __func__, __LINE__, buffer, buffer->width, buffer->height, buffer->stride, drawable->info.size, buffer->format, buffer->handle, drawable->info.eColorSpace);

	/*
	 * import dmabuf
	 */
	if (buffer->fd > 0) {
		fd = buffer->fd;
	} else if (buffer->handle) {
		int kms_fd = wayland_kms_fd_get(buffer->kms);
		if (drmPrimeHandleToFD(kms_fd, buffer->handle, DRM_CLOEXEC, &drawable->current->dmafd)) {
			WSEGL_DEBUG("%s: %s: %d: drmPrimeHandleToFD failed\n", __FILE__, __func__, __LINE__);
			goto error;
		}
		fd = drawable->current->dmafd;
	} else {
		WSEGL_DEBUG("%s: %s: %d: invalid buffer = %p (.handle = %d, fd = %d)\n", __FILE__, __func__, __LINE__, buffer, buffer->handle, buffer->fd);
		goto error;
	}
	if (!(drawable->current->map = pvr_map_dmabuf(display->context, fd, SERVER_PVR_MAP_NAME))) {
		if (drawable->current->dmafd)
			close(drawable->current->dmafd);
		WSEGL_DEBUG("%s: %s: %d: import dmabuf failed\n", __FILE__, __func__, __LINE__);
		goto error;
	}

	drawable->ref_count = 1;
	buffer->private = drawable;
	drawable->kms_buffer_destroy_listener.notify
		= _kms_buffer_destroy_callback;
	wl_resource_add_destroy_listener(
		buffer->resource, &drawable->kms_buffer_destroy_listener);
	drawable->pixmap_kms_buffer_in_use = 1;

	*phDrawable = (WSEGLDrawableHandle)drawable;

	return WSEGL_SUCCESS;

error:
	free(drawable);
	return WSEGL_BAD_NATIVE_PIXMAP;
}


/***********************************************************************************
 Function Name      : WSEGL_DeleteDrawable
 Inputs             : hDrawable
 Outputs            : None
 Returns            : Error code
 Description        : Delete a drawable - Only a window drawable is supported
 					  in this implementation
************************************************************************************/
static WSEGLError WSEGLs_DeleteDrawable(WSEGLDrawableHandle hDrawable)
{
	WLWSServerDrawable *drawable = (WLWSServerDrawable*)hDrawable;
	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	if (drawable->ref_count > 0)
		drawable->ref_count--;

	if (drawable->ref_count == 0) {
		if (!drawable->pixmap_kms_buffer_in_use)
			_gbm_destroy_drawable(drawable);
	}
	
	return WSEGL_SUCCESS;
}

static inline void gbm_kms_advance_buffer(WLWSServerDrawable *drawable)
{
	drawable->count ^= 1;	// optimization for double buffer only.
	drawable->source = drawable->current;
	drawable->current = &drawable->buffers[drawable->count];
}

/*******************************************************************************
****
 Function Name      : WSEGL_SwapDrawableWithDamage
 Inputs             : hDrawable, pasDamageRect, uiNumDamageRect, hFence
 Outputs            : None
 Returns            : Error code
 Description        : Post the colour buffer of a window drawable to a window
********************************************************************************
****/
static WSEGLError WSEGLs_SwapDrawableWithDamage(WSEGLDrawableHandle hDrawable, EGLint *pasDamageRect, EGLint uiNumDamageRect, PVRSRV_FENCE hFence)
{
	WLWSServerDrawable *drawable = (WLWSServerDrawable*)hDrawable;
	int i;
	WLWSServerDisplay *display = drawable->display;
	WSEGL_UNREFERENCED_PARAMETER(pasDamageRect);
	WSEGL_UNREFERENCED_PARAMETER(uiNumDamageRect);
	PVRSRVFenceDestroyExt(display->context->connection, hFence);

	for (i = 0; i < drawable->num_bufs; i++) {
		if (drawable->buffers[i].buffer_age > 0)
			drawable->buffers[i].buffer_age++;
	}
	drawable->current->buffer_age = 1;

	/*
	 * TODO:
	 * For gbm_surface, we should flush all rendering now. Later the compositor will
	 * gbm_surface_lock_front_buffer() and set the gbm_bo to drmModeSet().
	 */
	gbm_kms_set_front(drawable->surface, drawable->count);

	// get the next buffer
	gbm_kms_advance_buffer(drawable);

	// XXX: can we wait here until the previous front buffer is released???
	if (gbm_kms_is_bo_locked(drawable->current->bo)) {
		// just warning for now
		WSEGL_DEBUG("BO is still locked...\n");
	}

	return WSEGL_SUCCESS;
}


/***********************************************************************************
 Function Name      : WSEGL_SwapControlInterval
 Inputs             : hDrawable, ui32Interval
 Outputs            : None
 Returns            : Error code
 Description        : Set the swap interval of a window drawable
************************************************************************************/
static WSEGLError WSEGLs_SwapControlInterval(WSEGLDrawableHandle hDrawable, EGLint interval)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	WSEGL_UNREFERENCED_PARAMETER(interval);
	/*
	// This implementation does not support swap interval control
	*/
	return WSEGL_SUCCESS;
}


/***********************************************************************************
 Function Name      : WSEGL_WaitNative
 Inputs             : hDrawable, ui32Engine
 Outputs            : None
 Returns            : Error code
 Description        : Flush any native rendering requests on a drawable
************************************************************************************/
static WSEGLError WSEGLs_WaitNative(WSEGLDrawableHandle hDrawable, EGLint engine)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	/*
	// Just support the 'default engine'
	*/
	if (engine != WSEGL_DEFAULT_NATIVE_ENGINE)
	{
		return WSEGL_BAD_NATIVE_ENGINE;
	}

	return WSEGL_SUCCESS;
}


/***********************************************************************************
 Function Name      : WSEGL_CopyFromDrawable
 Inputs             : hDrawable, hNativePixmap
 Outputs            : None
 Returns            : Error code
 Description        : Copies colour buffer data from a drawable to a native Pixmap
************************************************************************************/
static WSEGLError WSEGLs_CopyFromDrawable(WSEGLDrawableHandle hDrawable, NativePixmapType hNativePixmap)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	WSEGL_UNREFERENCED_PARAMETER(hNativePixmap);
	/*
	// No native pixmap for Null window system
	*/
	return WSEGL_BAD_NATIVE_PIXMAP;


/***********************************************************************************
							SIMPLE_DRAWABLE_COPY_EXAMPLE
************************************************************************************

	// The mapping of drawable colour data to colour data in the native pixmap is platform dependent.
	// This simple example describes the use of a 1->1 size mapping, with no pixel format conversion

	NullWSDrawable *psNWSDrawable;
	unsigned char *pSrc, *pDst;
	unsigned long ui32Count, ui32PixmapStrideInBytes, ui32DrawableStrideInBytes, ui32WidthInBytes, ui32Height;

	
	psNWSDrawable = (NullWSDrawable *)hDrawable;

	// Retrieve the parameters of the native pixmap that are necessary for the copy (in this case
	// it's just the stride and starting address)

	if (!GetPropertiesOfNativePixmap(&dst, &ui32PixmapStride, &ui32WidthInBytes))
	{
		return WSEGL_BAD_NATIVE_PIXMAP;
	}

	// Drawable source parameters

	pSrc						= (unsigned char *)psNWSDrawable->psMemInfo->pBase;
	ui32Height					= psNWSDrawable->ui32Height;
	ui32DrawableStrideInBytes	= psNWSDrawable->psMemInfo->ui32SurfaceStride;

	// Copy the colour data from the drawable to the pixmap (this copy assumes that
	// the source and destination are equal sizes and have the same pixel format)

	for (ui32Count=0; ui32Count<ui32Height; ui32Count++)
	{
		mempcy(pDst, pSrc, ui32WidthInBytes);
		pDst += ui32PixmapStrideInBytes;
		pSrc += ui32DrawableStrideInBytes;
	}

	return WSEGL_SUCCESS;

***********************************************************************************
							SIMPLE_DRAWABLE_COPY_EXAMPLE
***********************************************************************************/
}


/***********************************************************************************
 Function Name      : WSEGL_CopyFromPBuffer
 Inputs             : pvAddress, ui32Width, ui32Height, ui32Stride,
 					  ePixelFormat, hNativePixmap
 Outputs            : None
 Returns            : Error code
 Description        : Copies colour buffer data from a PBuffer to a native Pixmap
************************************************************************************/
static WSEGLError WSEGLs_CopyFromPBuffer(PVRSRV_MEMDESC hMemDesc,
					EGLint iWidth,
					EGLint iHeight,
					uint32_t ui32Stride,
					IMG_PIXFMT ePixelFormat,
					NativePixmapType hNativePixmap)
{
	WSEGL_UNREFERENCED_PARAMETER(hMemDesc);
	WSEGL_UNREFERENCED_PARAMETER(iWidth);
	WSEGL_UNREFERENCED_PARAMETER(iHeight);
	WSEGL_UNREFERENCED_PARAMETER(ui32Stride);
	WSEGL_UNREFERENCED_PARAMETER(ePixelFormat);
	WSEGL_UNREFERENCED_PARAMETER(hNativePixmap);
	/*
	// No native pixmap for Null window system
	*/ 
	return WSEGL_BAD_NATIVE_PIXMAP;


/***********************************************************************************
							SIMPLE_PBUFFER_COPY_EXAMPLE
************************************************************************************

	// The mapping of PBuffer colour data to colour data in the native pixmap is platform dependent.
	// This simple example describes the use of a 1->1 size mapping, with no pixel format conversion

	unsigned char *pSrc, *pDst;
	unsigned long ui32Count, ui32PixmapStrideInBytes, ui32StrideInBytes, ui32WidthInBytes;

	// Retrieve the parameters of the native pixmap that are necessary for the copy (in this case
	// it's just the stride and starting address)

	if (!GetPropertiesOfNativePixmap(&dst, &ui32WidthInBytes, &ui32PixmapStrideInBytes))
	{
		return WSEGL_BAD_NATIVE_PIXMAP;
	}

	ui32StrideInBytes = ui32Stride * WSEGL2BytesPerPixel[ePixelFormat];

	// Copy the colour data from the PBuffer to the pixmap (this copy assumes that
	// the source and destination are equal sizes and have the same pixel format)
	// NOTE: PBuffers are "upside-down" in Core GLES, so we copy from bottom
	// to top. Set the address to point to the bottom row and negate the stride 
	// so we step backwards.

	pSrc = (unsigned char *)pvAddress + ((ui32Height - 1) * ui32StrideInBytes);

	for (ui32Count=0; ui32Count<ui32Height; ui32Count++)
	{
		mempcy(pDst, pSrc, ui32WidthInBytes);
		pDst += ui32PixmapStride;
		pSrc -= ui32StrideInBytes;
	}

	return WSEGL_SUCCESS;

***********************************************************************************
							SIMPLE_PBUFFER_COPY_EXAMPLE
***********************************************************************************/
}


/***********************************************************************************
 Function Name      : WSEGL_GetDrawableParameters
 Inputs             : hDrawable
 Outputs            : psSourceParams, psRenderParams
 Returns            : Error code
 Description        : Returns the parameters of a drawable that are needed
					  by the GL driver
************************************************************************************/
static WSEGLError WSEGLs_GetDrawableParameters(WSEGLDrawableHandle hDrawable,
						  WSEGLDrawableParams *psSourceParams,
						  WSEGLDrawableParams *psRenderParams)
{
	WLWSServerDrawable *drawable = (WLWSServerDrawable*)hDrawable;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	/*
	 * Check if the front buffer is updated by someone else, e.g. v4l2-renderer
	 * in weston. We shall not render into the front buffer.
	 */
	if (drawable->surface && gbm_kms_get_front(drawable->surface) == drawable->count)
		gbm_kms_advance_buffer(drawable);

	memset(psRenderParams, 0, sizeof(*psRenderParams));
	pvr_get_params(drawable->current->map, &drawable->info, psRenderParams);
	psRenderParams->sBase.i32BufferAge = drawable->current->buffer_age;

	if (drawable->source) {
		memset(psSourceParams, 0, sizeof(*psSourceParams));
		pvr_get_params(drawable->source->map, &drawable->info, psSourceParams);
		psSourceParams->sBase.i32BufferAge = drawable->source->buffer_age;
	} else {
		memcpy(psSourceParams, psRenderParams, sizeof(*psSourceParams));
	}

	return WSEGL_SUCCESS;
}

/***********************************************************************************
 Function Name      : WSEGL_GetImageParameters
 Inputs             : hDrawable
 Outputs            : psImageParams
 Returns            : Error code
 Description        : Returns the parameters of an image that are needed by the GL driver
************************************************************************************/
static WSEGLError WSEGLs_GetImageParameters(WSEGLDrawableHandle hDrawable,
		                                           WSEGLImageParams *psImageParams,
							   unsigned long ulPlaneOffset)
{
	WLWSServerDrawable *drawable = (WLWSServerDrawable*)hDrawable;
	WSEGL_UNREFERENCED_PARAMETER(ulPlaneOffset);

	memset(psImageParams, 0, sizeof(*psImageParams));
	if (!pvr_get_image_params(drawable->current->map, &drawable->info, psImageParams)) {
		return WSEGL_BAD_NATIVE_PIXMAP;
	}

	return WSEGL_SUCCESS;
}

/***********************************************************************************
 Function Name      : WSEGL_ConnectDrawable
 Inputs             : hDrawable
 Outputs            : None
 Returns            : Error code
 Description        : Indicates that the specified Drawable is in use by EGL as a
			  read or draw surface (separately)
************************************************************************************/
static WSEGLError WSEGLs_ConnectDrawable(WSEGLDrawableHandle hDrawable)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	/*
	 * TODO: Should we lock the drawable? We may at least need to nail down
	 * the size of the window.
	 */

	return WSEGL_SUCCESS;
}

/***********************************************************************************
 Function Name      : WSEGL_DisconnectDrawable
 Inputs             : hDrawable
 Outputs            : None
 Returns            : Error code
 Description        : Indicates that the specified Drawable is no longer in use by
			  EGL as a read or draw surface (separately)
************************************************************************************/
static WSEGLError WSEGLs_DisconnectDrawable(WSEGLDrawableHandle hDrawable)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	// TODO: Should we release the drawable?

	return WSEGL_SUCCESS;
}

/***********************************************************************************
 Function Name      : WSEGL_AcquireCPUMapping
 Inputs             : hDrawable, hMemDesc
 Outputs            : ppvCpuVirtAddr
 Returns            : Error code
 Description        : Request the CPU virtual address of (or a mapping to be
                      established for) a WSEGLDrawable.
************************************************************************************/
static WSEGLError WSEGLs_AcquireCPUMapping(WSEGLDrawableHandle hDrawable,
					   PVRSRV_MEMDESC hMemDesc,
					   void **ppvCpuVirtAddr)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);

	if (!pvr_acquire_cpu_mapping(hMemDesc, ppvCpuVirtAddr))
		return WSEGL_BAD_DRAWABLE;

	return WSEGL_SUCCESS;
}

/***********************************************************************************
 Function Name      : WSEGL_ReleaseCPUMapping
 Inputs             : hDrawable, hMemDesc
 Outputs            : None
 Returns            : Error code
 Description        : Indicate that a WSEGLDrawable's CPU virtual address and/or
                      mapping is no longer required.
************************************************************************************/
static WSEGLError WSEGLs_ReleaseCPUMapping(WSEGLDrawableHandle hDrawable,
					   PVRSRV_MEMDESC hMemDesc)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);

	pvr_release_cpu_mapping(hMemDesc);

	return WSEGL_SUCCESS;
}

/**********************************************************************
 *
 *       WARNING: Do not modify any code below this point
 *
 ***********************************************************************/ 

const WSEGL_FunctionTable __attribute__((visibility("internal"))) *WSEGLs_getFunctionTable()
{
	static const WSEGL_FunctionTable server_func_table = {
		.pfnWSEGL_InitialiseDisplay = WSEGLs_InitialiseDisplay,
		.pfnWSEGL_CloseDisplay = WSEGLs_CloseDisplay,
		.pfnWSEGL_CreateWindowDrawable = WSEGLs_CreateWindowDrawable,
		.pfnWSEGL_CreatePixmapDrawable = WSEGLs_CreatePixmapDrawable,
		.pfnWSEGL_DeleteDrawable = WSEGLs_DeleteDrawable,
		.pfnWSEGL_SwapDrawableWithDamage = WSEGLs_SwapDrawableWithDamage,
		.pfnWSEGL_SwapControlInterval = WSEGLs_SwapControlInterval,
		.pfnWSEGL_WaitNative = WSEGLs_WaitNative,
		.pfnWSEGL_CopyFromDrawable = WSEGLs_CopyFromDrawable,
		.pfnWSEGL_CopyFromPBuffer = WSEGLs_CopyFromPBuffer,
		.pfnWSEGL_GetDrawableParameters = WSEGLs_GetDrawableParameters,
		.pfnWSEGL_GetImageParameters = WSEGLs_GetImageParameters,
		.pfnWSEGL_ConnectDrawable = WSEGLs_ConnectDrawable,
		.pfnWSEGL_DisconnectDrawable = WSEGLs_DisconnectDrawable,
		.pfnWSEGL_AcquireCPUMapping = WSEGLs_AcquireCPUMapping,
		.pfnWSEGL_ReleaseCPUMapping = WSEGLs_ReleaseCPUMapping,
	};
	return &server_func_table;
}

/******************************************************************************
 End of file (wawyland_server.c)
******************************************************************************/
