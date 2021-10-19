/*************************************************************************/ /*!
@File
@Title          WSEGL interface definition
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

#if !defined(__WSEGL_H__)
#define __WSEGL_H__

#include <stdint.h>
#include <stdbool.h>

#include <EGL/eglplatform.h>

#include <powervr/mem_types.h>
#include <powervr/services_ext.h>
#include <powervr/buffer_attribs.h>
#include <powervr/imgyuv.h>
#include <powervr/imgpixfmts.h>
#include <powervr/pvrsrv_sync_ext.h>

#ifndef REL_STANDALONE_BUILD
#include "yuvinfo.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
// WSEGL Platform-specific definitions
*/
#if defined(EGL_WSEGL_DIRECTLY_LINKED)
#define WSEGL_EXPORT IMG_INTERNAL
#define WSEGL_IMPORT
#define WSEGL_INTERNAL IMG_INTERNAL
#else /* defined(EGL_WSEGL_DIRECTLY_LINKED) */
#if defined(__linux__)
#define WSEGL_EXPORT __attribute__((visibility("default")))
#define WSEGL_IMPORT
#define WSEGL_INTERNAL __attribute__((visibility("hidden")))
#elif defined(WINDOWS_WDF)
#define WSEGL_EXPORT __declspec(dllexport)
#define WSEGL_IMPORT __declspec(dllimport)
#define WSEGL_INTERNAL
#else
#define WSEGL_EXPORT
#define WSEGL_IMPORT
#define WSEGL_INTERNAL
#endif
#endif /* defined(EGL_WSEGL_DIRECTLY_LINKED) */

/*
// WSEGL API Version Number
*/
/*
 * The concept of binary compatibility checking has not been correctly thought through
 * Disabling for Rogue until we have an opportunity to understand the requirements
 *
 * Version set to 0xFFFFFFFF to indicate this
 */
#define WSEGL_VERSION					0xFFFFFFFF
#define WSEGL_DEFAULT_DISPLAY			0
#define WSEGL_DEFAULT_NATIVE_ENGINE		0

#define	WSEGL_UNREFERENCED_PARAMETER(param) (param) = (param)

/* Maximum number of allocations. This is to support multi planar YUV */
#define WSEGL_MAX_PLANES 3 /* This needs to always be equal to EGL_MAX_PLANES in eglapi.h */
/*
// WSEGL handles
*/
typedef void *WSEGLDisplayHandle;
typedef void *WSEGLDrawableHandle;

/*
// Display colorspace capability type
*/
typedef enum
{
	WSEGL_COLORSPACE_NONE       = 0,
	WSEGL_COLORSPACE            = 1U << 0,
	WSEGL_COLORSPACE_SCRGB      = 1U << 1,
	WSEGL_COLORSPACE_DISPLAY_P3 = 1U << 2,
	WSEGL_COLORSPACE_BT2020     = 1U << 3,
} WSEGLColorspaceType;

/*
// Display capability type
*/
typedef enum
{
	WSEGL_NO_CAPS = 0,
	WSEGL_CAP_MIN_SWAP_INTERVAL = 1, /* System default value = 1 */
	WSEGL_CAP_MAX_SWAP_INTERVAL = 2, /* System default value = 1 */
	WSEGL_CAP_WINDOWS_USE_HW_SYNC = 3, /* System default value = 0 (FALSE) */
	WSEGL_CAP_PIXMAPS_USE_HW_SYNC = 4, /* System default value = 0 (FALSE) */
	WSEGL_CAP_IMAGE_EXTERNAL_SUPPORT = 5, /* System default value = 0 */
	WSEGL_CAP_NATIVE_SYNC_SUPPORT = 6, /* System default value = 0 */
	WSEGL_CAP_COLORSPACE = 7, /* System default value = 0 */
	WSEGL_CAP_IMAGE_COLORSPACE = 8, /* System default value = 0 */

} WSEGLCapsType;

#ifdef REL_STANDALONE_BUILD
typedef struct YUV_INFO_TAG
{
	bool	bValid;
	uint32_t	ui32Plane0StrideInTexels;
	uint32_t	ui32Plane0StrideInBytes;
	/* Address which hardware needs - will be either start of header section or data section depending on HW */
	uint32_t	ui32HWPlaneAddressInBytes[3];
	/* Size of header section */
	uint32_t	ui32PlaneHeaderSizeInBytes[3];
} YUV_INFO;
#endif

/*
// Display capability
*/
typedef struct
{
	WSEGLCapsType eCapsType;
	uint32_t      ui32CapsValue;

} WSEGLCaps;

/*
// Drawable type
*/
#define WSEGL_NO_DRAWABLE			0x0
#define WSEGL_DRAWABLE_WINDOW		0x1
#define WSEGL_DRAWABLE_PIXMAP		0x2


/*
// Drawable parameter flags
*/

/*
 * Indicates that client drivers should perform implicit buffer synchronisation
 * when accessing memory represented by the drawable memory descriptor.
 */
#define WSEGL_FLAGS_DRAWABLE_BUFFER_SYNC        (1 << 0)

/*
// Image parameter flags
*/
#define WSEGL_FLAGS_EGLIMAGE_COMPOSITION_SYNC   (1 << 0)

/*
// Transparent of display/drawable
*/
typedef enum
{
	WSEGL_OPAQUE = 0,
	WSEGL_COLOR_KEY = 1,

} WSEGLTransparentType;

/*
// Display/drawable configuration
*/
typedef struct
{
	/*
	// Type of drawables this configuration applies to -
	// OR'd values of drawable types.
	*/
	uint32_t             ui32DrawableType;

	/* Pixel format */
	IMG_PIXFMT           ePixelFormat;

	/* Native Renderable  - set to true if native renderable */
	bool                 bNativeRenderable;

	/* FrameBuffer Level Parameter */
	int32_t              iFrameBufferLevel;

	/* Native Visual ID */
	int32_t              iNativeVisualID;

	/* Native Visual Type */
	int32_t              iNativeVisualType;

	/* Transparent Type */
	WSEGLTransparentType eTransparentType;

	/* Transparent Color - only used if transparent type is COLOR_KEY */
	uint32_t             ulTransparentColor; /* packed as 0x00RRGGBB */

	/* Framebuffer Target - set to IMG_TRUE if config compatible with framebuffer */
	bool                 bFramebufferTarget;

	/* Whether the configuration is conformant (ie EGL_CONFORMANT is non-zero) */
	bool                 bConformant;

	/* YUV colorspace */
	IMG_YUV_COLORSPACE   eYUVColorspace;

	uint32_t             ui32AntiAliasMode;
} WSEGLConfig;

/*
// WSEGL errors
*/
typedef enum
{
	WSEGL_SUCCESS = 0,
	WSEGL_CANNOT_INITIALISE = 1,
	WSEGL_BAD_NATIVE_DISPLAY = 2,
	WSEGL_BAD_NATIVE_WINDOW = 3,
	WSEGL_BAD_NATIVE_PIXMAP = 4,
	WSEGL_BAD_NATIVE_ENGINE = 5,
	WSEGL_BAD_DRAWABLE = 6,
	WSEGL_BAD_MATCH = 7,
	WSEGL_OUT_OF_MEMORY = 8,
	WSEGL_RETRY = 9,
	WSEGL_BAD_ACCESS = 10,
	WSEGL_UNTRUSTED_APP = 11,

} WSEGLError;

/*
// Base information required by OpenGL-ES driver
*/
typedef struct
{
	/* Width in pixels of the drawable */
	int32_t                  iWidth;

	/* Height in pixels of the drawable */
	int32_t                  iHeight;

	/* Stride in bytes of the drawable */
	uint32_t                 ui32StrideInBytes;

	/* YUV only */
	uint32_t                 ui32YPlaneStrideInTexels;

	/* Pixel format of the drawable */
	IMG_PIXFMT               ePixelFormat;

	/* HW address of the drawable */
	IMG_DEV_VIRTADDR         asHWAddress[WSEGL_MAX_PLANES];

	/* Memory descriptor for the drawable */
	PVRSRV_MEMDESC           ahMemDesc[WSEGL_MAX_PLANES];

	/* Size of memory indicated by descriptor */
	IMG_DEVMEM_SIZE_T        auiAllocSize[WSEGL_MAX_PLANES];

	PVRSRV_MEMDESC           hMetaDataMemDesc;
	IMG_DEVMEM_SIZE_T        uiMetaDataAllocSize;
	uint32_t                 ui32OffsetFBCType;

#if defined(GTRACE_TOOL)
	/* Allocation ID */
	uint64_t				 ui64AllocationID;
	/* Allocation context */
	const char           	 *pszAllocationContext;
#endif


	/* Colorspace */
	IMG_YUV_COLORSPACE       eYUVColorspace;

	/* Memory layout */
	IMG_MEMLAYOUT            eMemLayout;

	/* FB compression mode */
	IMG_FB_COMPRESSION       eFBCompression;

	/* FBC Data offset */
	int32_t 				ui32FBCDataOffset;

	/* Dependency fence */
	PVRSRV_FENCE             hFence;

	/* When was this buffer last used. */
	int32_t                  i32BufferAge;

	/* Number of layers in this buffer */
	uint32_t                 ui32Layers;

	/* Size of mip chain when layers are used */
	uint32_t                 ui32LayerMipChainSize;

	/* Flags */
	uint32_t                 ui32Flags;

	uint32_t                 ui32NumLevels;

} WSEGLBaseParams;


/*
// Drawable information required by OpenGL-ES driver
*/
typedef struct
{
	WSEGLBaseParams     sBase;

	/* This value can be set to control the maximum number of pending 3Ds in
	 * flight at once. Zero means use the API's default value.
	 */
	uint32_t            ui32MaxPending3D;

	/* Rotation angle of drawable (presently source only) */
	IMG_ROTATION        eRotationAngle;

} WSEGLDrawableParams;


/*
// Image information required by OpenGL-ES driver
*/
typedef struct
{
	WSEGLBaseParams       sBase;

	/* Chroma interpolation parameters */
	IMG_YUV_CHROMA_INTERP eChromaUInterp;
	IMG_YUV_CHROMA_INTERP eChromaVInterp;

	/* If YUV */
	YUV_INFO				sYUVInfo;

} WSEGLImageParams;


/*
// Table of function pointers that is returned by WSEGL_GetFunctionTablePointer()
//
// The first entry in the table is the version number of the wsegl.h header file that
// the module has been written against, and should therefore be set to WSEGL_VERSION
*/
typedef struct
{
	uint32_t ui32WSEGLVersion;

	WSEGLError (*pfnWSEGL_IsDisplayValid)(EGLNativeDisplayType);

	WSEGLError (*pfnWSEGL_InitialiseDisplay)(EGLNativeDisplayType, WSEGLDisplayHandle *, const WSEGLCaps **, WSEGLConfig **, PVRSRV_DEV_CONNECTION **);

	WSEGLError (*pfnWSEGL_CloseDisplay)(WSEGLDisplayHandle);

	WSEGLError (*pfnWSEGL_CreateWindowDrawable)(WSEGLDisplayHandle, WSEGLConfig *, WSEGLDrawableHandle *, EGLNativeWindowType, IMG_ROTATION *, IMG_COLOURSPACE_FORMAT, bool);

	WSEGLError (*pfnWSEGL_CreatePixmapDrawable)(WSEGLDisplayHandle, WSEGLConfig *, WSEGLDrawableHandle *, EGLNativePixmapType, IMG_ROTATION *, IMG_COLOURSPACE_FORMAT, bool);

	WSEGLError (*pfnWSEGL_DeleteDrawable)(WSEGLDrawableHandle);

	WSEGLError (*pfnWSEGL_SwapDrawableWithDamage)(WSEGLDrawableHandle, EGLint *, EGLint, PVRSRV_FENCE);

	WSEGLError (*pfnWSEGL_SwapControlInterval)(WSEGLDrawableHandle, int32_t);

	WSEGLError (*pfnWSEGL_WaitNative)(WSEGLDrawableHandle, int32_t);

	WSEGLError (*pfnWSEGL_CopyFromDrawable)(WSEGLDrawableHandle, EGLNativePixmapType);

	WSEGLError (*pfnWSEGL_CopyFromPBuffer)(PVRSRV_MEMDESC, int32_t, int32_t, uint32_t, IMG_PIXFMT, EGLNativePixmapType);

	WSEGLError (*pfnWSEGL_GetDrawableParameters)(WSEGLDrawableHandle, WSEGLDrawableParams *, WSEGLDrawableParams *);

	WSEGLError (*pfnWSEGL_GetImageParameters)(WSEGLDrawableHandle, WSEGLImageParams *, unsigned long);

	WSEGLError (*pfnWSEGL_ConnectDrawable)(WSEGLDrawableHandle);

	WSEGLError (*pfnWSEGL_DisconnectDrawable)(WSEGLDrawableHandle);

	WSEGLError (*pfnWSEGL_FlagStartFrame)(WSEGLDrawableHandle);

	WSEGLError (*pfnWSEGL_AcquireCPUMapping)(WSEGLDrawableHandle, PVRSRV_MEMDESC, void **);

	WSEGLError (*pfnWSEGL_ReleaseCPUMapping)(WSEGLDrawableHandle, PVRSRV_MEMDESC);

	WSEGLError (*pfnWSEGL_SetSwapBehaviour)(WSEGLDrawableHandle, int);

	WSEGLError (*pfnWSEGL_SetSingleBuffered)(WSEGLDrawableHandle, int);

	WSEGLError (*pfnWSEGL_FlagIntentToQuery)(WSEGLDrawableHandle);

#if defined(EGL_EXTENSION_NV_CONTEXT_PRIORITY_REALTIME)
	WSEGLError (*pfnWSEGL_IsTrustedAppForRealtimePriority)(WSEGLDisplayHandle *);
#endif /* defined(EGL_EXTENSION_NV_CONTEXT_PRIORITY_REALTIME) */

} WSEGL_FunctionTable;


WSEGL_IMPORT const WSEGL_FunctionTable *WSEGL_GetFunctionTablePointer(void);

#ifdef __cplusplus
}
#endif

#endif /* __WSEGL_H__ */

/******************************************************************************
 End of file (wsegl.h)
******************************************************************************/
