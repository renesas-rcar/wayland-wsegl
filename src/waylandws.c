/*
 * @File           waylandws.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "waylandws.h"
#include "waylandws_server.h"
#include "waylandws_client.h"

#include "gbm_kmsint.h"
#include "wayland-client.h"

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif
#if !defined(PACKAGE_STRING)
#define PACKAGE_STRING "Unknown Version"
#endif
const char *const __waylandwsegl_version = PACKAGE_STRING;

/*
 * Configs available on the null window system
 */
WSEGLConfig __attribute__((visibility("internal"))) WLWSEGL_Configs[] =
{
        /* WINDOW & PIXMAP ARGB 32 */
	{
		.ui32DrawableType       = WSEGL_DRAWABLE_WINDOW | WSEGL_DRAWABLE_PIXMAP,
		.ePixelFormat           = IMG_PIXFMT_B8G8R8A8_UNORM,
		.bNativeRenderable      = false,     // FIXME
		.iFrameBufferLevel      = 0,
		.iNativeVisualID        = GBM_FORMAT_ARGB8888,
		.eTransparentType       = WSEGL_OPAQUE,
		.ulTransparentColor     = 0,
		.bFramebufferTarget     = false,     // FIXME
		.bConformant		= true,
	},
        /* WINDOW & PIXMAP XRGB 32 */
	{
		.ui32DrawableType       = WSEGL_DRAWABLE_WINDOW | WSEGL_DRAWABLE_PIXMAP,
		.ePixelFormat           = IMG_PIXFMT_B8G8R8X8_UNORM,
		.bNativeRenderable      = false,     // FIXME
		.iFrameBufferLevel      = 0,
		.iNativeVisualID        = GBM_FORMAT_XRGB8888,
		.eTransparentType       = WSEGL_OPAQUE,
		.ulTransparentColor     = 0,
		.bFramebufferTarget     = false,     // FIXME
		.bConformant		= true,
	},
        /* PIXMAP RGB565 */
        {
		.ui32DrawableType       = WSEGL_DRAWABLE_PIXMAP,
		.ePixelFormat           = IMG_PIXFMT_B5G6R5_UNORM,
		.bNativeRenderable      = false,     // FIXME
		.iFrameBufferLevel      = 0,
		.iNativeVisualID        = GBM_FORMAT_RGB565,
		.eTransparentType       = WSEGL_OPAQUE,
		.ulTransparentColor     = 0,
		.bFramebufferTarget     = false,     // FIXME
		.bConformant		= true,
	},
        /* PIXMAP ARGB1555 */
        {
		.ui32DrawableType       = WSEGL_DRAWABLE_PIXMAP,
		.ePixelFormat           = IMG_PIXFMT_B5G5R5A1_UNORM,
		.bNativeRenderable      = false,     // FIXME
		.iFrameBufferLevel      = 0,
		.iNativeVisualID        = GBM_FORMAT_ARGB1555,
		.eTransparentType       = WSEGL_OPAQUE,
		.ulTransparentColor     = 0,
		.bFramebufferTarget     = false,     // FIXME
		.bConformant		= true,
	},
        /* PIXMAP ARGB4444 */
        {
		.ui32DrawableType       = WSEGL_DRAWABLE_PIXMAP,
		.ePixelFormat           = IMG_PIXFMT_B4G4R4A4_UNORM,
		.bNativeRenderable      = false,     // FIXME
		.iFrameBufferLevel      = 0,
		.iNativeVisualID        = GBM_FORMAT_ARGB4444,
		.eTransparentType       = WSEGL_OPAQUE,
		.ulTransparentColor     = 0,
		.bFramebufferTarget     = false,     // FIXME
		.bConformant		= false,
	},
	{
		.ui32DrawableType	= WSEGL_NO_DRAWABLE
	}
};

/*
 * Private window system display information
 */
typedef struct WaylandWS_Display_TAG
{
	/*
	 * Holds private handle for each backend.
	 */
	WSEGLDisplayHandle	display;

	/*
	 * Function table. We call different WSEGL functions
	 * for a server and clients.
	 */
	const WSEGL_FunctionTable	*func;
} WLWSDisplay;

/*
 * Private drawable information
 */
typedef struct WaylandWS_Drawable_TAG
{
	/*
	 * Holds private handle for each backend.
	 */
	WSEGLDrawableHandle	drawable;

	/*
	 * Function table. We call different WSEGL functions
	 * for a server and clients.
	 */
	WSEGL_FunctionTable	func;
} WLWSDrawable;

/***********************************************************************************
 Function Name      : WSEGL_IsDisplayValid
 Inputs             : hNativeDisplay
 Outputs            : None
 Returns            : Error code
 Description        : Validates a native display
************************************************************************************/
static WSEGLError WSEGL_IsDisplayValid(NativeDisplayType hNativeDisplay)
{
	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	// hNativeDisplay is either 1) gbm_create_device(), or 2) wl_display_connect().
	if ((void*)hNativeDisplay == NULL)
		return WSEGL_SUCCESS;

	// idenitify which one we've got.
	void *head = *(void**)hNativeDisplay;

	if (head == gbm_create_device || head == &wl_display_interface)
		return WSEGL_SUCCESS;

	return WSEGL_BAD_NATIVE_DISPLAY;
}


/***********************************************************************************
 Function Name      : WSEGL_InitialiseDisplay
 Inputs             : hNativeDisplay
 Outputs            : phDisplay, psCapabilities, psConfigs
 Returns            : Error code
 Description        : Initialises a display
************************************************************************************/
static WSEGLError WSEGL_InitialiseDisplay(NativeDisplayType hNativeDisplay,
					  WSEGLDisplayHandle *phDisplay,
					  const WSEGLCaps **psCapabilities,
					  WSEGLConfig **psConfigs,
					  PVRSRV_DEV_CONNECTION **ppsDevConnection)
{
	WLWSDisplay *display;
	WSEGLError ret;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	if (!(display = calloc(1, sizeof(WLWSDisplay))))
		return WSEGL_OUT_OF_MEMORY;

	/*
	 * Extract display handles from hNativeDisplay
	 */
	if ((void*)hNativeDisplay == NULL) {
		WSEGL_DEBUG("%s: Display is NULL.\n", __func__);
		display->func = WSEGLc_getFunctionTable();
	} else {
		void *head = *(void**)hNativeDisplay;
		if (head == gbm_create_device) {
			WSEGL_DEBUG("%s: GBM\n", __func__);
			display->func = WSEGLs_getFunctionTable();
		} else if (head == &wl_display_interface) {
			WSEGL_DEBUG("%s: WL_DISPLAY\n", __func__);
			display->func = WSEGLc_getFunctionTable();
		} else {
			free(display);
			return WSEGL_BAD_NATIVE_DISPLAY;
		}
	}

	ret = display->func->pfnWSEGL_InitialiseDisplay(hNativeDisplay, &display->display,
							psCapabilities, psConfigs, ppsDevConnection);

	if (ret == WSEGL_SUCCESS)
		*phDisplay = (WSEGLDisplayHandle)display;
	else
		free(display);

	return ret;
}


/***********************************************************************************
 Function Name      : WSEGL_CloseDisplay
 Inputs             : hDisplay
 Outputs            : None
 Returns            : Error code
 Description        : Closes a display
************************************************************************************/
static WSEGLError WSEGL_CloseDisplay(WSEGLDisplayHandle hDisplay)
{
	WSEGLError ret;
	WLWSDisplay *display = (WLWSDisplay*)hDisplay;

	ret = display->func->pfnWSEGL_CloseDisplay(display->display);
	
	free(display);

	return ret;
}

/***********************************************************************************
 Function Name      : WSEGL_CreateWindowDrawable
 Inputs             : hDisplay, psConfig, hNativeWindow
 Outputs            : phDrawable, eRotationAngle
 Returns            : Error code
 Description        : Create a window drawable for a native window
************************************************************************************/
static WSEGLError WSEGL_CreateWindowDrawable(WSEGLDisplayHandle hDisplay,
					     WSEGLConfig *psConfig,
					     WSEGLDrawableHandle *phDrawable,
					     NativeWindowType hNativeWindow,
					     WLWSEGL_ROTATION *eRotationAngle,
					     WLWSEGL_COLOURSPACE_FORMAT eColorSpace,
					     bool bIsProtected)
{
	WLWSDisplay *display = (WLWSDisplay*)hDisplay;
	WLWSDrawable *drawable;
	WSEGLError ret;

	if (!(drawable = calloc(sizeof(WLWSDrawable), 1)))
		return WSEGL_OUT_OF_MEMORY;

	drawable->func = *display->func;

	ret = display->func->pfnWSEGL_CreateWindowDrawable(display->display,
							    psConfig, &drawable->drawable,
							    hNativeWindow, eRotationAngle,
							    eColorSpace, bIsProtected);

	if (ret == WSEGL_SUCCESS)
		*phDrawable = (WSEGLDrawableHandle)drawable;
	else
		free(drawable);

	return ret;
}


/***********************************************************************************
 Function Name      : WSEGL_CreatePixmapDrawable
 Inputs             : hDisplay, psConfig, hNativePixmap
 Outputs            : phDrawable, eRotationAngle
 Returns            : Error code
 Description        : Create a pixmap drawable for a native pixmap
************************************************************************************/
static WSEGLError WSEGL_CreatePixmapDrawable(WSEGLDisplayHandle hDisplay,
						 WSEGLConfig *psConfig,
						 WSEGLDrawableHandle *phDrawable,
						 NativePixmapType hNativePixmap,
						 WLWSEGL_ROTATION *eRotationAngle,
						 WLWSEGL_COLOURSPACE_FORMAT eColorSpace,
						 bool bIsProtected)
{
	WLWSDisplay *display = (WLWSDisplay*)hDisplay;
	WLWSDrawable *drawable;
	WSEGLError ret;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	if (!(drawable = calloc(sizeof(WLWSDrawable), 1)))
		return WSEGL_OUT_OF_MEMORY;

	drawable->func = *display->func;

	ret = display->func->pfnWSEGL_CreatePixmapDrawable(display->display, psConfig,
							 &drawable->drawable, hNativePixmap,
							 eRotationAngle, eColorSpace, bIsProtected);

	if (ret == WSEGL_SUCCESS)
		*phDrawable = (WSEGLDrawableHandle)drawable;
	else
		free(drawable);

	WSEGL_DEBUG("%s: %s: %d (%d)\n", __FILE__, __func__, __LINE__, ret);
	return ret;
}


/***********************************************************************************
 Function Name      : WSEGL_DeleteDrawable
 Inputs             : hDrawable
 Outputs            : None
 Returns            : Error code
 Description        : Delete a drawable - Only a window drawable is supported
 					  in this implementation
************************************************************************************/
static WSEGLError WSEGL_DeleteDrawable(WSEGLDrawableHandle hDrawable)
{
	WSEGLError ret;
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;

	ret = drawable->func.pfnWSEGL_DeleteDrawable(drawable->drawable);
	free(drawable);

	return ret;
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
static WSEGLError WSEGL_SwapDrawableWithDamage(WSEGLDrawableHandle hDrawable, EGLint *pasDamageRect, EGLint uiNumDamageRect, PVRSRV_FENCE hFence)
{
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;

	return drawable->func.pfnWSEGL_SwapDrawableWithDamage(drawable->drawable, pasDamageRect, uiNumDamageRect, hFence);
}

/***********************************************************************************
 Function Name      : WSEGL_SwapControlInterval
 Inputs             : hDrawable, ui32Interval
 Outputs            : None
 Returns            : Error code
 Description        : Set the swap interval of a window drawable
************************************************************************************/
static WSEGLError WSEGL_SwapControlInterval(WSEGLDrawableHandle hDrawable, EGLint interval)
{
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;
	return drawable->func.pfnWSEGL_SwapControlInterval(drawable->drawable, interval);
}


/***********************************************************************************
 Function Name      : WSEGL_WaitNative
 Inputs             : hDrawable, ui32Engine
 Outputs            : None
 Returns            : Error code
 Description        : Flush any native rendering requests on a drawable
************************************************************************************/
static WSEGLError WSEGL_WaitNative(WSEGLDrawableHandle hDrawable, EGLint engine)
{
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;
	return drawable->func.pfnWSEGL_WaitNative(drawable->drawable, engine);
}


/***********************************************************************************
 Function Name      : WSEGL_CopyFromDrawable
 Inputs             : hDrawable, hNativePixmap
 Outputs            : None
 Returns            : Error code
 Description        : Copies colour buffer data from a drawable to a native Pixmap
************************************************************************************/
static WSEGLError WSEGL_CopyFromDrawable(WSEGLDrawableHandle hDrawable, NativePixmapType hNativePixmap)
{
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;
	return drawable->func.pfnWSEGL_CopyFromDrawable(drawable->drawable, hNativePixmap);
}


/***********************************************************************************
 Function Name      : WSEGL_CopyFromPBuffer
 Inputs             : pvAddress, ui32Width, ui32Height, ui32Stride,
 					  ePixelFormat, hNativePixmap
 Outputs            : None
 Returns            : Error code
 Description        : Copies colour buffer data from a PBuffer to a native Pixmap
************************************************************************************/
static WSEGLError WSEGL_CopyFromPBuffer(PVRSRV_MEMDESC hMemDesc,
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
	// XXX: This is required in cairo-egl. We need to think about how to support this.
	return WSEGL_BAD_MATCH;
}


/***********************************************************************************
 Function Name      : WSEGL_GetDrawableParameters
 Inputs             : hDrawable
 Outputs            : psSourceParams, psRenderParams
 Returns            : Error code
 Description        : Returns the parameters of a drawable that are needed
					  by the GL driver
************************************************************************************/
static WSEGLError WSEGL_GetDrawableParameters(WSEGLDrawableHandle hDrawable,
						  WSEGLDrawableParams *psSourceParams,
						  WSEGLDrawableParams *psRenderParams)
{
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;
	return drawable->func.pfnWSEGL_GetDrawableParameters(drawable->drawable,
							       psSourceParams, psRenderParams);
}

/***********************************************************************************
 Function Name      : WSEGL_GetImageParameters
 Inputs             : hDrawable
 Outputs            : psImageParams
 Returns            : Error code
 Description        : Returns the parameters of an image that are needed by the GL driver
************************************************************************************/
static WSEGLError WSEGL_GetImageParameters(WSEGLDrawableHandle hDrawable,
							   WSEGLImageParams *psImageParams,
							   unsigned long ulPlaneOffset)
{
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;
	return drawable->func.pfnWSEGL_GetImageParameters(drawable->drawable, psImageParams, ulPlaneOffset);
}

/***********************************************************************************
 Function Name      : WSEGL_ConnectDrawable
 Inputs             : hDrawable
 Outputs            : None
 Returns            : Error code
 Description        : Indicates that the specified Drawable is in use by EGL as a
			  read or draw surface (separately)
************************************************************************************/
static WSEGLError WSEGL_ConnectDrawable(WSEGLDrawableHandle hDrawable)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
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
static WSEGLError WSEGL_DisconnectDrawable(WSEGLDrawableHandle hDrawable)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	return WSEGL_SUCCESS;
}

/***********************************************************************************
 Function Name      : WSEGL_FlagStartFrame
 Inputs             : hDrawable
 Outputs            : None
 Returns            : Error code
 Description        : Indicates that there have been rendering commands submitted
					  by a client driver
************************************************************************************/
static WSEGLError WSEGL_FlagStartFrame(WSEGLDrawableHandle hDrawable)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	// FIXME: nothing I can do about it... unless we have WLWSDisplay as a global var.
	return WSEGL_SUCCESS;
}

/*******************************************************************************
****
 Function Name      : WSEGL_AcquireCPUMapping
 Inputs             : hDrawable, hMemDesc
 Outputs            : ppvCpuVirtAddr
 Returns            : Error code
 Description        : Request the CPU virtual address of (or a mapping to be
                      established for) a WSEGLDrawable.
********************************************************************************
****/
static WSEGLError WSEGL_AcquireCPUMapping(WSEGLDrawableHandle hDrawable,
                                          PVRSRV_MEMDESC hMemDesc,
                                          void **ppvCpuVirtAddr)
{
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;
	return drawable->func.pfnWSEGL_AcquireCPUMapping(drawable->drawable, hMemDesc, ppvCpuVirtAddr);
}

/*******************************************************************************
****
 Function Name      : WSEGL_ReleaseCPUMapping
 Inputs             : hDrawable, hMemDesc
 Outputs            : None
 Returns            : Error code
 Description        : Indicate that a WSEGLDrawable's CPU virtual address and/or
                      mapping is no longer required.
********************************************************************************
****/
static WSEGLError WSEGL_ReleaseCPUMapping(WSEGLDrawableHandle hDrawable,
                                          PVRSRV_MEMDESC hMemDesc)
{
	WLWSDrawable *drawable = (WLWSDrawable*)hDrawable;
	return drawable->func.pfnWSEGL_ReleaseCPUMapping(drawable->drawable, hMemDesc);
}

/*******************************************************************************
****
 Function Name      : WSEGL_SetSwapBehaviour
 Inputs             : hDrawable
 Inputs             : iDestroyed
 Outputs            : None
 Returns            : Error code
 Description        : Indicates if the surface is using EGL_BUFFER_DESTROYED.
************************************************************************************/
static WSEGLError WSEGL_SetSwapBehaviour(WSEGLDrawableHandle hDrawable, int iDestroyed)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	WSEGL_UNREFERENCED_PARAMETER(iDestroyed);
	/*
	 * XXX:
	 * For now, we return success regardless of iDestroyed value as being
	 * done in IMG sample WSEGL. There's no documentation available
	 * on this API.
	 * In the future, we may need to do something different here.
	 */
        return WSEGL_SUCCESS;
}

static WSEGLError WSEGL_SetSingleBuffered(WSEGLDrawableHandle hDrawable, int bEnabled)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	WSEGL_UNREFERENCED_PARAMETER(bEnabled);
	return WSEGL_BAD_DRAWABLE;
}

/*******************************************************************************
****
Function    WSEGL_FlagIntentToQuery
Description Indicates if EGL is going to query information for a drawable
             without colour buffers.
Input       hDrawable        WSEGL drawable handle.
Return      A WSEGL error code
************************************************************************************/
static WSEGLError WSEGL_FlagIntentToQuery(WSEGLDrawableHandle hDrawable)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);

	return WSEGL_SUCCESS;
}

/**********************************************************************
 *
 *       WARNING: Do not modify any code below this point
 *
 ***********************************************************************/ 
static const WSEGL_FunctionTable sFunctionTable =
{
	.ui32WSEGLVersion = WSEGL_VERSION,
	.pfnWSEGL_IsDisplayValid = WSEGL_IsDisplayValid,
	.pfnWSEGL_InitialiseDisplay = WSEGL_InitialiseDisplay,
	.pfnWSEGL_CloseDisplay = WSEGL_CloseDisplay,
	.pfnWSEGL_CreateWindowDrawable = WSEGL_CreateWindowDrawable,
	.pfnWSEGL_CreatePixmapDrawable = WSEGL_CreatePixmapDrawable,
	.pfnWSEGL_DeleteDrawable = WSEGL_DeleteDrawable,
	.pfnWSEGL_SwapDrawableWithDamage = WSEGL_SwapDrawableWithDamage,
	.pfnWSEGL_SwapControlInterval = WSEGL_SwapControlInterval,
	.pfnWSEGL_WaitNative = WSEGL_WaitNative,
	.pfnWSEGL_CopyFromDrawable = WSEGL_CopyFromDrawable,
	.pfnWSEGL_CopyFromPBuffer = WSEGL_CopyFromPBuffer,
	.pfnWSEGL_GetDrawableParameters = WSEGL_GetDrawableParameters,
	.pfnWSEGL_ConnectDrawable = WSEGL_ConnectDrawable,
	.pfnWSEGL_DisconnectDrawable = WSEGL_DisconnectDrawable,
	.pfnWSEGL_FlagStartFrame = WSEGL_FlagStartFrame,
	.pfnWSEGL_GetImageParameters = WSEGL_GetImageParameters,
	.pfnWSEGL_AcquireCPUMapping = WSEGL_AcquireCPUMapping,
	.pfnWSEGL_ReleaseCPUMapping = WSEGL_ReleaseCPUMapping,
	.pfnWSEGL_SetSwapBehaviour = WSEGL_SetSwapBehaviour,
	.pfnWSEGL_SetSingleBuffered = WSEGL_SetSingleBuffered,
	.pfnWSEGL_FlagIntentToQuery = WSEGL_FlagIntentToQuery,
};

/***********************************************************************************
 Function Name      : WSEGL_GetFunctionTablePointer
 Inputs             : None
 Outputs            : None
 Returns            : Function table pointer
 Description        : Returns a pointer to the window system function pointer table
************************************************************************************/
WSEGL_EXPORT const WSEGL_FunctionTable *WSEGL_GetFunctionTablePointer(void)
{
	WSEGL_DEBUG("%s: %s\n", __FILE__, __func__);
	return &sFunctionTable;
}

/******************************************************************************
 End of file (wayland.c)
******************************************************************************/
