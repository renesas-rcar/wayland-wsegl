/*************************************************************************/ /*!
@File
@Title          YUV defines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#if !defined(IMGYUV_H)
#define IMGYUV_H

typedef enum
{
	IMG_COLORSPACE_UNDEFINED = 0,
	IMG_COLORSPACE_BT601_CONFORMANT_RANGE = 1,
	IMG_COLORSPACE_BT601_FULL_RANGE = 2,
	IMG_COLORSPACE_BT709_CONFORMANT_RANGE = 3,
	IMG_COLORSPACE_BT709_FULL_RANGE = 4,
	IMG_COLORSPACE_BT2020_CONFORMANT_RANGE = 5,
	IMG_COLORSPACE_BT2020_FULL_RANGE = 6,
	IMG_COLORSPACE_BT601_CONFORMANT_RANGE_INVERSE = 7,
	IMG_COLORSPACE_BT601_FULL_RANGE_INVERSE = 8,
	IMG_COLORSPACE_BT709_CONFORMANT_RANGE_INVERSE = 9,
	IMG_COLORSPACE_BT709_FULL_RANGE_INVERSE = 10,
	IMG_COLORSPACE_BT2020_CONFORMANT_RANGE_INVERSE = 11,
	IMG_COLORSPACE_BT2020_FULL_RANGE_INVERSE = 12
} IMG_YUV_COLORSPACE;

typedef enum
{
	IMG_CHROMA_INTERP_UNDEFINED = 0,
	IMG_CHROMA_INTERP_ZERO = 1,
	IMG_CHROMA_INTERP_QUARTER = 2,
	IMG_CHROMA_INTERP_HALF = 3,
	IMG_CHROMA_INTERP_THREEQUARTERS = 4
} IMG_YUV_CHROMA_INTERP;


#endif /* IMGYUV_H */
