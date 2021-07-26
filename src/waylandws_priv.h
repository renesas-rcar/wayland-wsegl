/*
 * @File           waylandws_priv.h
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

#ifndef __WAYLANDWS_PRIV_H__
#define __WAYLANDWS_PRIV_H__

#define EGL_NATIVE_PIXFORMAT_YUYV_REL		6
#define EGL_NATIVE_PIXFORMAT_VYUY_REL		7
#define EGL_NATIVE_PIXFORMAT_YVYU_REL		8
#define EGL_NATIVE_PIXFORMAT_NV21_REL		9
#define EGL_NATIVE_PIXFORMAT_I420_REL		10
#define EGL_NATIVE_PIXFORMAT_YV12_REL		11
#define EGL_NATIVE_PIXFORMAT_NV16_REL		12

#define D_MASK_FORMAT				0x000000FF
#define D_MASK_YUV_COLORSPACE			0x0000FF00

#define EGL_CHROMA_INTERP_U_ZERO_REL		0x00010000
#define EGL_CHROMA_INTERP_U_QUATER_REL		0x00020000
#define EGL_CHROMA_INTERP_U_HALF_REL		0x00030000
#define EGL_CHROMA_INTERP_U_THREEQUARTERS_REL	0x00040000
#define EGL_CHROMA_INTERP_V_ZERO_REL		0x01000000
#define EGL_CHROMA_INTERP_V_QUATER_REL		0x02000000
#define EGL_CHROMA_INTERP_V_HALF_REL		0x03000000
#define EGL_CHROMA_INTERP_V_THREEQUARTERS_REL	0x04000000

#define D_MASK_YUV_CHROMA_INTERP_U		0x00FF0000
#define D_MASK_YUV_CHROMA_INTERP_V		0xFF000000

#endif /* !__WAYLANDWS_H__ */
