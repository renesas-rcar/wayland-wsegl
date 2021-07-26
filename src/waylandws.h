/*
 * @File           waylandws.h
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

#ifndef __WAYLANDWS_H__
#define __WAYLANDWS_H__

#include <stdio.h>

#include <EGL/eglplatform.h>
#include "powervr/wsegl.h"

#include "waylandws_priv.h"

#if defined(DEBUG)
#	define WSEGL_DEBUG(s, x...)	{ printf(s, ## x); }
#else
#	define WSEGL_DEBUG(s, x...)	{ }
#endif

#define SERVER_PVR_MAP_NAME	"wayland_wsegl_s"
#define CLIENT_PVR_MAP_NAME	"wayland_wsegl_c"


/*
 * WSEGLCaps and WSEGLConfigs
 */
extern WSEGLConfig WLWSEGL_Configs[];

/*
 * frequently used types used in WSEGL
 */

#define	WLWSEGL_ROTATE_0	IMG_ROTATION_0DEG

#define WLWSEGL_PIXFMT_RGB565	IMG_PIXFMT_B5G6R5_UNORM
#define WLWSEGL_PIXFMT_ARGB1555	IMG_PIXFMT_B5G5R5A1_UNORM
#define WLWSEGL_PIXFMT_ARGB4444	IMG_PIXFMT_B4G4R4A4_UNORM
#define WLWSEGL_PIXFMT_ARGB8888	IMG_PIXFMT_B8G8R8A8_UNORM
#define WLWSEGL_PIXFMT_XRGB8888	IMG_PIXFMT_B8G8R8X8_UNORM
#define WLWSEGL_PIXFMT_NV12	IMG_PIXFMT_YUV420_2PLANE
#define WLWSEGL_PIXFMT_NV21	IMG_PIXFMT_YVU420_2PLANE
#define WLWSEGL_PIXFMT_UYVY	IMG_PIXFMT_UYVY
#define WLWSEGL_PIXFMT_YUYV	IMG_PIXFMT_YUYV
#define WLWSEGL_PIXFMT_VYUY	IMG_PIXFMT_VYUY
#define WLWSEGL_PIXFMT_YVYU	IMG_PIXFMT_YVYU
#define WLWSEGL_PIXFMT_I420	IMG_PIXFMT_YUV420_3PLANE
#define WLWSEGL_PIXFMT_YV12	IMG_PIXFMT_YVU420_3PLANE
#define WLWSEGL_PIXFMT_NV16	IMG_PIXFMT_YUV8_422_2PLANE_PACK8

#define WLWSEGL_YUV_COLORSPACE_CONFORMANT_BT601 IMG_COLORSPACE_BT601_CONFORMANT_RANGE
#define WLWSEGL_YUV_COLORSPACE_FULL_BT601 IMG_COLORSPACE_BT601_FULL_RANGE
#define WLWSEGL_YUV_COLORSPACE_CONFORMANT_BT709 IMG_COLORSPACE_BT709_CONFORMANT_RANGE
#define WLWSEGL_YUV_COLORSPACE_FULL_BT709 IMG_COLORSPACE_BT709_FULL_RANGE

typedef IMG_PIXFMT		WLWSEGL_PIXFMT;
typedef IMG_ROTATION		WLWSEGL_ROTATION;
typedef IMG_COLOURSPACE_FORMAT	WLWSEGL_COLOURSPACE_FORMAT;

/*
 * Common window system drawable info
 */
typedef struct WaylandWS_Drawable_Info {
	unsigned long ui32DrawableType;
	int			width;
	int			height;
	int			stride;	// in pixels
	int			pitch;	// in bytes
	int			size;
	WLWSEGL_PIXFMT		pixelformat;
	/* [RELCOMMENT P-0149] Add variable for yuv texture.*/
	IMG_YUV_COLORSPACE    eColorSpace;
	IMG_YUV_CHROMA_INTERP eChromaUInterp;
	IMG_YUV_CHROMA_INTERP eChromaVInterp;

} WLWSDrawableInfo;

#endif /* !__WAYLANDWS_H__ */
