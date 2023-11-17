/*
 * @File           waylandws_client.c
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

#include "config.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <xf86drm.h>
#include <drm_fourcc.h>

#include <wayland-client.h>
#ifdef HAVE_WAYLAND_EGL_BACKEND_H
#include <wayland-egl-backend.h>
#else
#include <wayland-egl-priv.h>
#endif
#include "waylandws.h"
#include "waylandws_client.h"
#include "libkms.h"
#include "wayland-kms-client-protocol.h"
#include "wayland-kms.h"

#include "linux-dmabuf-unstable-v1-client-protocol.h"

#include "waylandws_pvr.h"

#include "EGL/egl.h"
#include "EGL/eglext_REL.h"

#define RENDER_NODE_MODULE "rcar-du"

/*
 * Environment variables to configure behaviors
 */

/* Enable set number of back buffers to 2 or more and MAX_BACK_BUFFERS or less. */
const char *ENV_NUM_BUFFERS = "WSEGL_NUM_BUFFERS";
const char *PVRCONF_NUM_BUFFERS = "WseglNumBuffers";

/*
 * Set to non-zero to eanble an aggressive sync mode. This must be enabled
 * when a fullscreen mode is used with gl-renderer with double buffering mode.
 */
const char *ENV_ENABLE_AGGRESSIVE_SYNC = "WSEGL_ENABLE_AGGRESSIVE_SYNC";
const char *PVRCONF_ENABLE_AGGRESSIVE_SYNC = "WseglEnableAggressiveSync";

/* enable formats */
enum {
	ENABLE_FORMAT_ARGB8888 = 1 << 0,
	ENABLE_FORMAT_XRGB8888 = 1 << 1
};

/*
 * Capabilities of the wayland window system
 */
static const WSEGLCaps WLWSEGL_Caps[] =
{
	{ WSEGL_CAP_WINDOWS_USE_HW_SYNC, 1 },
	{ WSEGL_CAP_PIXMAPS_USE_HW_SYNC, 1 },
	{ WSEGL_CAP_MIN_SWAP_INTERVAL, 0 },
	{ WSEGL_CAP_MAX_SWAP_INTERVAL, 1 },
        { WSEGL_CAP_IMAGE_EXTERNAL_SUPPORT, 1 },
	{ WSEGL_NO_CAPS, 0 }
};

/*
 * Private window system display information
 */

typedef struct WaylandWS_Client_Display_TAG
{
        /* For Wayland display */
        struct wl_display       *wl_display;
        struct wl_event_queue   *wl_queue;
        struct wl_registry      *wl_registry;
        struct wl_kms           *wl_kms;
        struct zwp_linux_dmabuf_v1      *zlinux_dmabuf;
	int			display_connected;

        /* for sync/frame events */
        struct wl_callback      *callback;

        /* For KMS used in the client */
        int                     fd;
        struct kms_driver       *kms;
        int                     authenticated;

        /* PVR context */
        struct pvr_context      *context;

        /* mode setting */
        int                     aggressive_sync;

	/* for check format */
	int			enable_formats;

	/* drm modifier */
	uint32_t		modifier_lo;
	uint32_t		modifier_hi;
} WLWSClientDisplay;

/* Do not change the following number. */
#define MAX_BACK_BUFFERS 4
#define MIN_BACK_BUFFERS 2
#define DEFAULT_BACK_BUFFERS 3

/* flags for kms_buffer.flag */
enum {
	KMS_BUFFER_FLAG_LOCKED	= 1,
	KMS_BUFFER_FLAG_TYPE_BO	= 2,
};

struct kms_buffer {
        int                     flag;

        struct kms_bo           *bo;
        void                    *addr;
        struct wl_buffer        *wl_buffer;
        int                     prime_fd;

        int                     buffer_age;

        /* PVR memory map */
        struct pvr_map          *map;

        /* pointing back to the drawable */
        void *drawable;
};

#define IS_KMS_BUFFER_LOCKED(b)	((b)->flag & KMS_BUFFER_FLAG_LOCKED)

#ifdef HAVE_WAYLAND_EGL_18_1_0
#define GET_EGL_WINDOW_PRIVATE(window)		window->driver_private
#define SET_EGL_WINDOW_PRIVATE(window, p)	window->driver_private = p
#else
#define GET_EGL_WINDOW_PRIVATE(window)		window->private
#define SET_EGL_WINDOW_PRIVATE(window, p)	window->private = p
#endif

typedef enum {
        WLWS_BUFFER_KMS_BO,
        WLWS_BUFFER_USER_MEMORY
} WLWSBufferType;

/*
 * Private window system drawable information
 */

typedef struct {
        int                     interval;
        struct wl_callback      *frame_sync;
} WLWSClientSurface;

struct queue {
	struct kms_buffer	*buffer;
	struct queue		*next;
};

typedef struct WaylandWS_Client_Drawable_TAG
{
        struct wl_egl_window    *window;
	bool			enable_damage_buffer;

        WLWSDrawableInfo        info;

        WLWSBufferType          buffer_type;

        struct kms_buffer       buffers[MAX_BACK_BUFFERS];
        struct kms_buffer       *current;       /* rendering buffer */
        struct kms_buffer       *source;        /* source buffer, i.e. previous one */
        int                     num_bufs;       /* number of used buffers */

	struct queue		free_buffer_queue[MAX_BACK_BUFFERS];
	struct queue		*free_buffer;
	struct queue		*free_buffer_unused;

        WLWSClientDisplay       *display;

        int                     ref_count;

        struct wl_listener      kms_buffer_destroy_listener;
        int                     pixmap_kms_buffer_in_use;

        int                     resized;        /* set when window is resized */

        WLWSClientSurface       *surface;
} WLWSClientDrawable;

/*
 * Wayland related routines
 */

struct wayland_cb_data {
	struct wl_callback **flag;
	const char *name;
};

static void wayland_sync_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
	struct wl_callback **p_callback = data;
	WSEGL_UNREFERENCED_PARAMETER(serial);

	*p_callback = NULL;
	wl_callback_destroy(callback);
}

static const struct wl_callback_listener wayland_sync_listener = {
	.done = wayland_sync_callback
};

/*
 * Wayland callback setting
 */
static void wayland_set_callback(WLWSClientDisplay *display,
				 struct wl_callback *callback,
				 struct wl_callback **flag, const char *name)
{
	struct wl_event_queue *queue = display->wl_queue;

	if (!flag)
		flag = &display->callback;
#if defined(DEBUG)
       WSEGL_DEBUG("%s: %s: callback=%s(%p)\n", __FILE__, __func__, name, callback);
#else
       WSEGL_UNREFERENCED_PARAMETER(name);
#endif
	/* we don't override callback */
	if (*flag) {
		WSEGL_DEBUG("%s: %s: callback already set to %p\n", __FILE__, __func__, *flag);
		wl_callback_destroy(callback);
		goto quit;
	} else {
		*flag = callback;
		wl_callback_add_listener(callback, &wayland_sync_listener, flag);
		wl_proxy_set_queue((struct wl_proxy*)callback, queue);
	}

quit:
	WSEGL_DEBUG("%s: %s: done\n", __FILE__, __func__);
}

/*
 * wl_kms notification listeners
 */

static void wayland_kms_handle_device(void *data, struct wl_kms *kms, const char *device)
{
	WLWSClientDisplay *display = data;
	drm_magic_t magic;

	WSEGL_DEBUG("%s: %s: %d (device=%s)\n", __FILE__, __func__, __LINE__, device);

	if ((display->fd = open(device, O_RDWR | O_CLOEXEC)) < 0) {
		WSEGL_DEBUG("%s: %s: %d: Can't open %s (%s)\n",
			    __FILE__, __func__, __LINE__, device, strerror(errno));
		return;
	}

	/* we can now request for authentication */
	drmGetMagic(display->fd, &magic);
	wl_kms_authenticate(kms, magic);
}

static void wayland_kms_handle_format(void *data, struct wl_kms *kms, uint32_t format)
{
	WSEGL_UNREFERENCED_PARAMETER(data);
	WSEGL_UNREFERENCED_PARAMETER(kms);
	WSEGL_UNREFERENCED_PARAMETER(format);

	//WLWSClientDisplay *display = data;
	WSEGL_DEBUG("%s: %s: %d (format=%08x)\n", __FILE__, __func__, __LINE__, format);
}

static void wayland_kms_handle_authenticated(void *data, struct wl_kms *kms)
{
	WLWSClientDisplay *display = data;
	WSEGL_UNREFERENCED_PARAMETER(kms);
	WSEGL_DEBUG("%s: %s: %d: authenticated.\n", __FILE__, __func__, __LINE__);

	display->authenticated = 1;
}

static const struct wl_kms_listener wayland_kms_listener = {
	.device = wayland_kms_handle_device,
	.format = wayland_kms_handle_format,
	.authenticated = wayland_kms_handle_authenticated,
};

static void dmabuf_format(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
			  uint32_t format)
{
	WSEGL_UNREFERENCED_PARAMETER(data);
	WSEGL_UNREFERENCED_PARAMETER(dmabuf);
	WSEGL_UNREFERENCED_PARAMETER(format);

	/* Deprecated */
}

static void dmabuf_modifiers(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
			     uint32_t format, uint32_t modifier_hi,
			     uint32_t modifier_lo)
{
	WSEGL_UNREFERENCED_PARAMETER(dmabuf);
	WLWSClientDisplay *display = data;
	uint64_t modifier = ((uint64_t)modifier_hi << 32) | modifier_lo;

	switch (format) {
	case DRM_FORMAT_ARGB8888:
		display->enable_formats |= ENABLE_FORMAT_ARGB8888;
		break;
	case DRM_FORMAT_XRGB8888:
		display->enable_formats |= ENABLE_FORMAT_XRGB8888;
		break;
	default:
		return;
	}

	if (modifier == DRM_FORMAT_MOD_LINEAR) {
		display->modifier_lo = modifier_lo;
		display->modifier_hi = modifier_hi;
	}
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
	dmabuf_format,
	dmabuf_modifiers
};

/*
 * registry routines to the server global objects
 */

static void wayland_registry_handle_global(void *data, struct wl_registry *registry,
					   uint32_t name, const char *interface, uint32_t version)
{
	WLWSClientDisplay *display = data;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);
	/*
	 * we need to connect to the wl_kms objects
	 */
	if (!strcmp(interface, "wl_kms")) {
		display->wl_kms = wl_registry_bind(registry, name, &wl_kms_interface, version);
	} else if (!strcmp(interface, "zwp_linux_dmabuf_v1")) {
		display->zlinux_dmabuf =
			wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, version);
		zwp_linux_dmabuf_v1_add_listener (display->zlinux_dmabuf, &dmabuf_listener, display);
	}
}

static void wayland_registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	WSEGL_UNREFERENCED_PARAMETER(data);
	WSEGL_UNREFERENCED_PARAMETER(registry);
	WSEGL_UNREFERENCED_PARAMETER(name);
}

static const struct wl_registry_listener wayland_registry_listener = {
	.global = wayland_registry_handle_global,
	.global_remove = wayland_registry_handle_global_remove,
};

/*
 * for wl_buffer management
 */

static void init_free_buffer_queue(WLWSClientDrawable *drawable)
{
	int i;
	struct queue *queue = drawable->free_buffer_queue;

	drawable->free_buffer = queue;
	for (i = 0; i < drawable->num_bufs; i++)
	    queue[i].buffer = &drawable->buffers[i];

	for (i = 0; i < drawable->num_bufs - 1; i++)
	    queue[i].next = &queue[i + 1];
}

static inline void put_free_buffer(WLWSClientDrawable *drawable, struct kms_buffer *buffer)
{
	struct queue *item = drawable->free_buffer_unused;
	if (!item) {
		WSEGL_DEBUG("%s: %s: Unlikly queue item is NULL.\n", __FILE__, __func__);
		return;
	}

	drawable->free_buffer_unused = item->next;
	item->buffer = buffer;
	item->next = drawable->free_buffer;
	drawable->free_buffer = item;
}

static inline struct kms_buffer* get_free_buffer(WLWSClientDrawable *drawable)
{
	struct queue *item = drawable->free_buffer;
	if (!item)
		return NULL;

	drawable->free_buffer = item->next;
	item->next = drawable->free_buffer_unused;
	drawable->free_buffer_unused = item;

	return item->buffer;
}

static void _kms_release_buffer(WLWSClientDrawable *drawable, struct kms_buffer *buffer);

static void wayland_buffer_release(void *data, struct wl_buffer *buffer)
{
	WLWSClientDrawable *drawable = data;
	int i;

	WSEGL_DEBUG("%s: %s\n", __FILE__, __func__);
	for (i = 0; i < drawable->num_bufs; i++) {
		struct kms_buffer *kms_buffer = &drawable->buffers[i];
		if (kms_buffer->wl_buffer == buffer) {
			WSEGL_DEBUG("%s: %s: buffer %d (%p) is released.\n", __FILE__, __func__, i, buffer);
			kms_buffer->flag &= ~KMS_BUFFER_FLAG_LOCKED;
			put_free_buffer(drawable, kms_buffer);
			goto done;
		}
	}
	wl_buffer_destroy(buffer);

done:
	WSEGL_DEBUG("%s: %s: done\n", __FILE__, __func__);
	return;
}

static const struct wl_buffer_listener wayland_buffer_listener = {
	.release = wayland_buffer_release
};

struct dmabuf_params_result
{
	struct wl_buffer *wl_buffer;
	int done;
};

static void zlinux_dmabuf_create_succeeded(
	void *data, struct zwp_linux_buffer_params_v1 *params,
	struct wl_buffer *new_buffer)
{
	struct dmabuf_params_result *result = data;

	result->wl_buffer = new_buffer;
	result->done = 1;

	zwp_linux_buffer_params_v1_destroy(params);
}

static void zlinux_dmabuf_create_failed(
	void *data, struct zwp_linux_buffer_params_v1 *params)
{
	struct dmabuf_params_result *result = data;

	result->wl_buffer = NULL;
	result->done = 1;

	WSEGL_DEBUG("%s: %s: %d: zlinux_buffer_params.create failed.\n",
		    __FILE__, __func__, __LINE__);

	zwp_linux_buffer_params_v1_destroy(params);
}

static const struct zwp_linux_buffer_params_v1_listener buffer_params_listener = {
	zlinux_dmabuf_create_succeeded,
	zlinux_dmabuf_create_failed
};

static struct wl_buffer*
wayland_get_wl_buffer_from_zlinux_dmabuf(WLWSClientDisplay *display,
					 WLWSClientDrawable *drawable, int fd)
{
	uint32_t pixelformat;
	struct zwp_linux_buffer_params_v1 *params;
	struct dmabuf_params_result params_result = {NULL, 0};
	int ret = 0;

	/* check the pixelformat */
	switch (drawable->info.pixelformat) {
	case WLWSEGL_PIXFMT_ARGB8888:
		if (!(display->enable_formats & ENABLE_FORMAT_ARGB8888))
			goto err;
		pixelformat = DRM_FORMAT_ARGB8888;
		break;
	case WLWSEGL_PIXFMT_XRGB8888:
		if (!(display->enable_formats & ENABLE_FORMAT_XRGB8888))
			goto err;
		pixelformat = DRM_FORMAT_XRGB8888;
		break;
	default:
		goto err;
	}

	params = zwp_linux_dmabuf_v1_create_params(display->zlinux_dmabuf);
	wl_proxy_set_queue((struct wl_proxy*)params, display->wl_queue);
	zwp_linux_buffer_params_v1_add(params, fd, 0, 0, drawable->info.pitch,
				       display->modifier_hi, display->modifier_lo);
	zwp_linux_buffer_params_v1_add_listener(params,
						&buffer_params_listener,
						&params_result);
	zwp_linux_buffer_params_v1_create(
		params, drawable->info.width, drawable->info.height,
		pixelformat, 0);
	wl_display_flush(display->wl_display);

	while (ret >= 0 && !params_result.done) {
		ret = wl_display_dispatch_queue(display->wl_display,
						display->wl_queue);
	}

	return params_result.wl_buffer;

err:
	WSEGL_DEBUG("%s: %s: %d: unexpected pixelformat %x passed.\n",
		    __FILE__, __func__, __LINE__,
		    drawable->info.pixelformat);
	return NULL;
}

static struct wl_buffer*
wayland_get_wl_buffer_from_wl_kms(WLWSClientDisplay *display,
				  WLWSClientDrawable *drawable, int fd)
{
	uint32_t pixelformat;

	/* check the pixelformat */
	switch (drawable->info.pixelformat) {
	case WLWSEGL_PIXFMT_ARGB8888:
		pixelformat = WL_KMS_FORMAT_ARGB8888;
		break;
	case WLWSEGL_PIXFMT_XRGB8888:
		pixelformat = WL_KMS_FORMAT_XRGB8888;
		break;
	default:
		WSEGL_DEBUG("%s: %s: %d: unexpected pixelformat %x passed.\n",
			    __FILE__, __func__, __LINE__,
			    drawable->info.pixelformat);
		return NULL;
	}

	return wl_kms_create_buffer(display->wl_kms, fd,
				    drawable->info.width, drawable->info.height,
				    drawable->info.pitch, pixelformat, 0);
}

static struct wl_buffer* wayland_get_wl_buffer(WLWSClientDisplay *display, struct kms_buffer *buffer)
{
	WLWSClientDrawable *drawable = buffer->drawable;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);
	if (buffer->wl_buffer)
		goto done;

	if (display->zlinux_dmabuf) {
		buffer->wl_buffer =
			wayland_get_wl_buffer_from_zlinux_dmabuf(
					display, drawable, buffer->prime_fd);
	} else {
		buffer->wl_buffer =
			wayland_get_wl_buffer_from_wl_kms(display, drawable,
							  buffer->prime_fd);
	}

	if (!buffer->wl_buffer)
		return NULL;

	WSEGL_DEBUG("%s: %s: %d: wl_buffer=%p\n", __FILE__, __func__, __LINE__, buffer->wl_buffer);

	wl_proxy_set_queue((struct wl_proxy*)buffer->wl_buffer, display->wl_queue);
	wl_buffer_add_listener(buffer->wl_buffer, &wayland_buffer_listener, drawable);

done:
	return buffer->wl_buffer;
}

static void wayland_wait_for_buffer_release(WLWSClientDrawable *drawable)
{
	WLWSClientDisplay *display = drawable->display;

	WSEGL_DEBUG("%s: %s\n", __FILE__, __func__);

	wl_display_dispatch_queue_pending(display->wl_display, display->wl_queue);
	if (!drawable->current)
		drawable->current = get_free_buffer(drawable);
	while (!drawable->current || IS_KMS_BUFFER_LOCKED(drawable->current)) {
		WSEGL_DEBUG("%s: %s: current=%p, callback=%p\n", __FILE__, __func__,
			    drawable->current, display->callback);

		if (display->aggressive_sync)
			wayland_set_callback(display, wl_display_sync(display->wl_display),
					     NULL, "wl_display_sync(2)");

		if (wl_display_dispatch_queue(display->wl_display, display->wl_queue) < 0)
			break;
		drawable->current = get_free_buffer(drawable);
	}

	/* we maybe in the wrong situation. wayland backend sometime drops the request. */
	if (display->callback) {
		WSEGL_DEBUG("%s: %s: destroying callback. something went wrong.\n", __FILE__, __func__);
		wl_callback_destroy(display->callback);
		display->callback = NULL;
	}

	WSEGL_DEBUG("%s: %s: buffer unlocked\n", __FILE__, __func__);

	return;
}

static int get_env_value(const char *env, int default_value)
{
	int rc = default_value;
	char *value;
	if ((value = getenv(env)))
		rc = atoi(value);
	WSEGL_DEBUG("%s: %s: %s = %d\n", __FILE__, __func__, env, rc);
	return rc;
}

static int get_config_value(const char *pvr_key, const char *env_key, int default_value)
{
	int ret;

	ret = pvr_get_config_value(pvr_key);
	if (ret >= 0) {
		WSEGL_DEBUG("%s: %s: %s = %d\n", __FILE__, __func__, pvr_key, ret);
		return ret;
	}

	return get_env_value(env_key, default_value);
}

static bool authenticate_kms_device(WLWSClientDisplay *display)
{
	if (!display->wl_kms)
		return false;

	wl_kms_add_listener(display->wl_kms, &wayland_kms_listener, display);

	if (wl_display_roundtrip_queue(display->wl_display, display->wl_queue) < 0 || display->fd == -1) {
		// no DRM device given
		return false;
	}

	if (wl_display_roundtrip_queue(display->wl_display, display->wl_queue) < 0 || !display->authenticated) {
		// Authentication failed...
		return false;
	}
	return true;
}

static bool setup_drm_device(WLWSClientDisplay *display)
{
	display->fd = -1;

	if (wl_display_roundtrip_queue(display->wl_display, display->wl_queue) < 0)
		return false;

	if (display->zlinux_dmabuf)
		display->fd = drmOpenWithType(RENDER_NODE_MODULE, NULL, DRM_NODE_RENDER);

	if (display->fd >= 0)
		return true;

	/* Fallback to authentication via wl_kms */
	return authenticate_kms_device(display);
}

static bool ensure_supported_dmabuf_formats(WLWSClientDisplay *display)
{
	if (!display->zlinux_dmabuf)
		return true;

	if (wl_display_roundtrip_queue(display->wl_display, display->wl_queue) < 0 ||
            !display->enable_formats) {
		/* No supported dmabuf pixel formats */
		return false;
	}

	return true;
}

/***********************************************************************************
 Function Name      : WSEGL_InitialiseDisplay
 Inputs             : hNativeDisplay
 Outputs            : phDisplay, psCapabilities, psConfigs
 Returns            : Error code
 Description        : Initialises a display
************************************************************************************/
static WSEGLError WSEGLc_InitialiseDisplay(NativeDisplayType hNativeDisplay,
					   WSEGLDisplayHandle *phDisplay,
					   const WSEGLCaps **psCapabilities,
					   WSEGLConfig **psConfigs,
					   PVRSRV_DEV_CONNECTION **ppsDevConnection)
{
	WLWSClientDisplay *display;
	WSEGLError err;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	if (!(display = calloc(1, sizeof(WLWSClientDisplay))))
		return WSEGL_OUT_OF_MEMORY;

	/*
	 * Extract display handles from hNativeDisplay
	 */
	if ((void*)hNativeDisplay == NULL) {
		/* create a default display */
		if (!(display->wl_display = wl_display_connect(NULL))) {
			err = WSEGL_BAD_NATIVE_DISPLAY;
			goto fail;
		}
		display->display_connected = 1;
	} else {
		display->wl_display = (struct wl_display*)hNativeDisplay;
	}
	display->fd = -1;

	/*
	 * Initialize modifier
	 */
	display->modifier_hi = DRM_FORMAT_MOD_INVALID >> 32;
	display->modifier_lo = DRM_FORMAT_MOD_INVALID & 0xffffffff;

	/*
	 * Create a queue to communicate with the server.
	 */
	display->wl_queue = wl_display_create_queue(display->wl_display);
	display->wl_registry = wl_display_get_registry(display->wl_display);
	wl_proxy_set_queue((struct wl_proxy*)display->wl_registry,
			   display->wl_queue);
	wl_registry_add_listener(display->wl_registry, &wayland_registry_listener, display);

	/* Now setup the DRM device */
	if (!setup_drm_device(display)) {
		err = WSEGL_BAD_NATIVE_DISPLAY;
		goto fail;
	}

	/* Get the list of supported pixel formats */
	if (!ensure_supported_dmabuf_formats(display)) {
		err = WSEGL_BAD_NATIVE_DISPLAY;
		goto fail;
	}

	/* XXX: should we wrap this with wl_kms client code? */
	if (kms_create(display->fd, &display->kms)) {
		err = WSEGL_BAD_NATIVE_DISPLAY;
		goto fail;
	}

	/* Create a PVR context */
	if (!(display->context = pvr_connect(ppsDevConnection))) {
		err = WSEGL_CANNOT_INITIALISE;
		goto fail;
	}

	/* set sync mode */
	display->aggressive_sync = get_config_value(PVRCONF_ENABLE_AGGRESSIVE_SYNC, ENV_ENABLE_AGGRESSIVE_SYNC, 0);

	/* return the pointers to the caps, configs, and the display handle */
	*psCapabilities = WLWSEGL_Caps;
	*psConfigs	= WLWSEGL_Configs;
	*phDisplay	= (WSEGLDisplayHandle)display;

	return WSEGL_SUCCESS;

fail:
	if (display->kms)
		kms_destroy(&display->kms);
	if (display->fd >= 0)
		close(display->fd);
	if (display->wl_kms)
		wl_kms_destroy(display->wl_kms);
	if (display->zlinux_dmabuf)
		zwp_linux_dmabuf_v1_destroy(display->zlinux_dmabuf);
	if (display->wl_registry)
		wl_registry_destroy(display->wl_registry);
	if (display->wl_queue)
		wl_event_queue_destroy(display->wl_queue);
	if (display->display_connected)
		wl_display_disconnect(display->wl_display);
	free(display);
	return err;
}


/***********************************************************************************
 Function Name      : WSEGL_CloseDisplay
 Inputs             : hDisplay
 Outputs            : None
 Returns            : Error code
 Description        : Closes a display
************************************************************************************/
static WSEGLError WSEGLc_CloseDisplay(WSEGLDisplayHandle hDisplay)
{
	WLWSClientDisplay *display = (WLWSClientDisplay*)hDisplay;
	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	pvr_disconnect(display->context);

	wl_kms_destroy(display->wl_kms);
	if (display->zlinux_dmabuf)
		zwp_linux_dmabuf_v1_destroy(display->zlinux_dmabuf);
	wl_registry_destroy(display->wl_registry);
	wl_event_queue_destroy(display->wl_queue);

	if (display->fd >= 0)
		close(display->fd);

	kms_destroy(&display->kms);

	if (display->display_connected)
		wl_display_disconnect(display->wl_display);
	free(display);

	return WSEGL_SUCCESS;
}

static void _kms_release_buffer(WLWSClientDrawable *drawable, struct kms_buffer *buffer)
{
	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	if (!buffer)
		return;

	if (buffer->map)
		pvr_unmap_memory(drawable->display->context, buffer->map);

	if (buffer->addr) {
		if (buffer->flag & KMS_BUFFER_FLAG_TYPE_BO)
			kms_bo_unmap(buffer->bo);
		else
			munmap(buffer->addr, drawable->info.size);
	}
	if (buffer->prime_fd)
		close(buffer->prime_fd);

	if (buffer->bo)
		kms_bo_destroy(&buffer->bo);

	if (buffer->wl_buffer)
		wl_buffer_destroy(buffer->wl_buffer);
}

static void _kms_release_buffers(WLWSClientDrawable *drawable)
{
	int i;

	WSEGL_DEBUG("%s: %s: %d: %p\n", __FILE__, __func__, __LINE__, drawable);

	if (!drawable)
		return;

	for (i = 0; i < drawable->num_bufs; i++) {
		WSEGL_DEBUG("%s: %s: %d: i=%d:\n", __FILE__, __func__, __LINE__, i);
		_kms_release_buffer(drawable, &drawable->buffers[i]);
		memset(&drawable->buffers[i], 0, sizeof(struct kms_buffer));
	}
	WSEGL_DEBUG("%s: %s: %d: done\n", __FILE__, __func__, __LINE__);
}

#define MIN(x, y)	((x) < (y)) ? (x) : (y)
#define MAX(x, y)	((x) > (y)) ? (x) : (y)

static int _kms_get_number_of_buffers(void)
{
	static int num_buffers = 0;

	if (!num_buffers) {
		num_buffers = get_config_value(PVRCONF_NUM_BUFFERS, ENV_NUM_BUFFERS, DEFAULT_BACK_BUFFERS);
		num_buffers = MIN(MAX(num_buffers, MIN_BACK_BUFFERS), MAX_BACK_BUFFERS);
	}

	return num_buffers;
}

static int _kms_create_buffers(WLWSClientDrawable *drawable)
{
	WLWSClientDisplay *display = drawable->display;
	int i, err;
	uint32_t handle;
	unsigned attr[] = {
		KMS_BO_TYPE, KMS_BO_TYPE_SCANOUT_X8R8G8B8,
		KMS_WIDTH, 0,
		KMS_HEIGHT, 0,
		KMS_TERMINATE_PROP_LIST
	};

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	drawable->info.width = drawable->window->width;
	drawable->info.height = drawable->window->height;

	// stride shall be 32 pixels aligned.
	attr[3] = drawable->info.stride = ((drawable->info.width + 31) >> 5) << 5;
	attr[5] = drawable->info.height;

	// number of buffers
	drawable->num_bufs = _kms_get_number_of_buffers();

	for (i = 0; i < drawable->num_bufs; i++) {
		if ((err = kms_bo_create(display->kms, attr, &drawable->buffers[i].bo)))
			goto kms_error;

		kms_bo_get_prop(drawable->buffers[i].bo, KMS_HANDLE, &handle);

		if (drmPrimeHandleToFD(display->fd, handle, DRM_CLOEXEC,
				       &drawable->buffers[i].prime_fd)) {
			WSEGL_DEBUG(
				"%s: %s: %d: drmPrimeHandleToFD failed. %s\n",
				__FILE__, __func__, __LINE__, strerror(errno));
			goto kms_error;
		}

		WSEGL_DEBUG("%s: %s: %d (prime_fd=%d)\n", __FILE__, __func__,
			    __LINE__, drawable->buffers[i].prime_fd);

		drawable->buffers[i].flag |= KMS_BUFFER_FLAG_TYPE_BO;

		drawable->buffers[i].drawable = drawable;
	}

	kms_bo_get_prop(drawable->buffers[0].bo, KMS_PITCH, (unsigned int*)&drawable->info.pitch);
	drawable->info.size = drawable->info.pitch * drawable->info.height;

	WSEGL_DEBUG("%s: %s: %d: size=%d, %dx%d, pitch=%d, stride=%d\n", __FILE__, __func__, __LINE__,
			drawable->info.size, drawable->info.width, drawable->info.height, drawable->info.pitch, drawable->info.stride);
	/* Wrap KMS BO with PVR service */
	for (i = 0; i < drawable->num_bufs; i++) {
		if (!(drawable->buffers[i].map =
		      pvr_map_dmabuf(display->context,
				     drawable->buffers[i].prime_fd,
				     CLIENT_PVR_MAP_NAME)))
			goto kms_error;
	}

	return 0;

kms_error:
	WSEGL_DEBUG("%s: %s: %d: %s\n", __FILE__, __func__, __LINE__, strerror((err == -1) ? errno : err));
	_kms_release_buffers(drawable);
	return -1;
}

static void _kms_resize_callback(struct wl_egl_window *window, void *private)
{
	WLWSClientDrawable *drawable = private;
	WSEGL_UNREFERENCED_PARAMETER(window);

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	if (drawable->info.width  != drawable->window->width ||
	    drawable->info.height != drawable->window->height)
		drawable->resized = 1;
}

/***********************************************************************************
 Function Name      : WSEGL_CreateWindowDrawable
 Inputs             : hDisplay, psConfig, hNativeWindow
 Outputs            : phDrawable, eRotationAngle
 Returns            : Error code
 Description        : Create a window drawable for a native window
************************************************************************************/
static WSEGLError WSEGLc_CreateWindowDrawable(WSEGLDisplayHandle hDisplay,
					      WSEGLConfig *psConfig,
					      WSEGLDrawableHandle *phDrawable,
					      NativeWindowType hNativeWindow,
					      WLWSEGL_ROTATION *eRotationAngle,
					      WLWSEGL_COLOURSPACE_FORMAT eColorSpace,
					      bool bIsProtected)
{
	WLWSClientDisplay *display = (WLWSClientDisplay*)hDisplay;
	WLWSClientDrawable *drawable;
	WSEGL_UNREFERENCED_PARAMETER(eColorSpace);
	WSEGL_UNREFERENCED_PARAMETER(bIsProtected);

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);
	if (!(drawable = calloc(sizeof(WLWSClientDrawable), 1)))
		return WSEGL_OUT_OF_MEMORY;

	/*
	 * For wl_surface, we have to create a wl_buffer with KMS BO and wrap
	 * BOs with PVR2DMemWrap again.  Later, this memory might be imported
	 * by the compositor with gbm_bo_import()
	 * for full-screen rendering, i.e. passed to DRM/KMS, or eglCreateImageKHR()
	 * to be composed with OpenGL/ES API later.
	 */
	drawable->info.ui32DrawableType = WSEGL_DRAWABLE_WINDOW;
	drawable->window = (struct wl_egl_window*)hNativeWindow;
	drawable->display = display;
	drawable->buffer_type = WLWS_BUFFER_KMS_BO;
	drawable->info.pixelformat = psConfig->ePixelFormat;

	/* Create KMS BO for rendering. */
	if (_kms_create_buffers(drawable))
		goto kms_error;

	/* now set the current rendering buffer */
	init_free_buffer_queue(drawable);
	drawable->current = get_free_buffer(drawable);

	/* set swap interval, either default value or whatever previously set before resizing */
	if (GET_EGL_WINDOW_PRIVATE(drawable->window)) {
		WLWSClientDrawable *previous_drawable = GET_EGL_WINDOW_PRIVATE(drawable->window);
		drawable->surface = previous_drawable->surface;
		previous_drawable->window = NULL;
	} else {
		drawable->surface = calloc(sizeof(WLWSClientSurface), 1);
		drawable->surface->interval = 1;
	}

	// check proxy version
	if (wl_proxy_get_version((struct wl_proxy*)drawable->window->surface) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
		drawable->enable_damage_buffer = true;

	// set resize callback
	drawable->window->resize_callback = _kms_resize_callback;
	SET_EGL_WINDOW_PRIVATE(drawable->window, drawable);

	// No rotation
	*eRotationAngle = WLWSEGL_ROTATE_0;

	*phDrawable = (WSEGLDrawableHandle)drawable;

	return WSEGL_SUCCESS;

kms_error:
	free(drawable);
	return WSEGL_CANNOT_INITIALISE;
}

static void _kms_buffer_destroy_callback(struct wl_listener *listener,
					 void *data)
{
	WLWSClientDrawable *drawable;
	WSEGL_UNREFERENCED_PARAMETER(data);

	drawable = wl_container_of(listener, drawable,
				  kms_buffer_destroy_listener);
	/* If DeleteDrawable() is called before this callback,
	   drawable should be destroyed. */
	if (drawable->ref_count > 0) {
		drawable->pixmap_kms_buffer_in_use = 0;
		drawable->kms_buffer_destroy_listener.notify = NULL;
	} else {
		_kms_release_buffers(drawable);
		free(drawable);
	}
}

static WLWSClientDrawable *import_wl_kms_buffer(WLWSClientDisplay *display, struct wl_kms_buffer *buffer)
{
	WLWSClientDrawable *drawable;
	int kms_fd;
	struct drm_mode_map_dumb arg;

	if (!(drawable = calloc(sizeof(WLWSClientDrawable), 1)))
		return NULL;

	drawable->current = drawable->source = &drawable->buffers[0];
	drawable->num_bufs = 1;
	drawable->display = display;
	drawable->buffer_type = WLWS_BUFFER_KMS_BO;

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

	kms_fd = wayland_kms_fd_get(buffer->kms);

	drawable->info.width  = buffer->width;
	drawable->info.height = buffer->height;
	drawable->info.pitch  = buffer->stride;
	drawable->info.size   = buffer->stride * buffer->height;
	drawable->info.stride = buffer->stride / 4;

	WSEGL_DEBUG("%s: %s: %d: buffer = %p (%dx%d, stride(pitch in wsegl)=%d, size=%d, format=%08x, handle=%d)\n",
			__FILE__, __func__, __LINE__, buffer, buffer->width, buffer->height, buffer->stride, drawable->info.size, buffer->format, buffer->handle);

	switch(buffer->format) {
	case WL_KMS_FORMAT_ARGB8888:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_ARGB8888;
		break;
	case WL_KMS_FORMAT_XRGB8888:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_XRGB8888;
		break;
	default:
		goto error;
	}

	/*
	 * wrap buffer->handle with PVR2DMemWrap()
	 */

	memset(&arg, 0, sizeof(arg));
	arg.handle = buffer->handle;

	if (drmIoctl(kms_fd, DRM_IOCTL_MODE_MAP_DUMB, &arg) < 0) {
		// error...
		goto error;
	}

	WSEGL_DEBUG("%s: %s: %d: mapping handle=%d from offset=%lld\n", __FILE__, __func__, __LINE__, arg.handle, arg.offset);
#if !defined(__LP64__) &&  !defined (_LP64)
	drawable->current->addr = (void *)syscall(__NR_mmap2, 0, drawable->info.size, PROT_READ | PROT_WRITE, MAP_SHARED, kms_fd, arg.offset >> 12);
#else
	drawable->current->addr = mmap(0, drawable->info.size, PROT_READ | PROT_WRITE, MAP_SHARED, kms_fd, arg.offset);
#endif
	if (drawable->current->addr == MAP_FAILED)
		goto error;

	drawable->current->flag &= ~KMS_BUFFER_FLAG_TYPE_BO;

	buffer->private = drawable;

	drawable->kms_buffer_destroy_listener.notify
		= _kms_buffer_destroy_callback;
	wl_resource_add_destroy_listener(
		buffer->resource, &drawable->kms_buffer_destroy_listener);
	drawable->pixmap_kms_buffer_in_use = 1;

	return drawable;

error:
	free(drawable);
	return NULL;
}

#define D_MIN_BUFFER_SIZE	16
#define D_MAX_BUFFER_SIZE	8192
#define D_STRIDE_GRANULARITY	2U

static int validate_rel_pixmap(EGLNativePixmapTypeREL *psNativePixmap)
{
	/* Check psNativePixmap */
	if (psNativePixmap == NULL) {
		WSEGL_DEBUG("Invalid Paramter: psNativePixmap = NULL\n");
		return -1;
	}

	/* Check Width */
	if ((psNativePixmap->width < D_MIN_BUFFER_SIZE) ||
	    (psNativePixmap->width > D_MAX_BUFFER_SIZE) ||
	    (psNativePixmap->width & (D_STRIDE_GRANULARITY - 1))) {
		WSEGL_DEBUG("Invalid Paramter: width = %d\n", psNativePixmap->width);
		return -1;
	}

	/* Check Height */
	if ((psNativePixmap->height < D_MIN_BUFFER_SIZE) ||
	    (psNativePixmap->height > D_MAX_BUFFER_SIZE)) {
		WSEGL_DEBUG("Invalid Paramter: height = %d\n", psNativePixmap->height);
		return -1;
	}

	/* Check Stride(Align & Larger than or Equal to Width) */
	if ((psNativePixmap->stride & (D_STRIDE_GRANULARITY - 1)) ||
	    (psNativePixmap->stride < psNativePixmap->width)) {
		WSEGL_DEBUG("Invalid Paramter: stride = %d\n", psNativePixmap->stride);
		return -1;
	}

	/* Check Pixel Format */
	switch (psNativePixmap->format) {
	case EGL_NATIVE_PIXFORMAT_RGB565_REL:
	case EGL_NATIVE_PIXFORMAT_ARGB1555_REL:
	case EGL_NATIVE_PIXFORMAT_ARGB8888_REL:
	case EGL_NATIVE_PIXFORMAT_ARGB4444_REL:
	case EGL_NATIVE_PIXFORMAT_YUYV_REL:
	case EGL_NATIVE_PIXFORMAT_UYVY_REL:
	case EGL_NATIVE_PIXFORMAT_VYUY_REL:
	case EGL_NATIVE_PIXFORMAT_YVYU_REL:
	case EGL_NATIVE_PIXFORMAT_NV12_REL:
	case EGL_NATIVE_PIXFORMAT_NV21_REL:
	case EGL_NATIVE_PIXFORMAT_I420_REL:
	case EGL_NATIVE_PIXFORMAT_YV12_REL:
	case EGL_NATIVE_PIXFORMAT_NV16_REL:
		break;
	default:
		WSEGL_DEBUG("Invalid Paramter: format = %d\n", psNativePixmap->format);
		return -1;
	}

	/* Buffer Address */
	if ((psNativePixmap->pixelData == NULL) ||
	    (((unsigned long)psNativePixmap->pixelData & 0xf) != 0)) {
		WSEGL_DEBUG("Invalid Paramter: pixelData = NULL\n");
		return -1;
	}

	return 0;
}

static WLWSClientDrawable *import_native_rel_buffer(WLWSClientDisplay *display, EGLNativePixmapTypeREL *buffer)
{
	WLWSClientDrawable *drawable;
	int bpp;
	int yuv = 0;

	if (!(drawable = calloc(sizeof(WLWSClientDrawable), 1)))
		return NULL;

	drawable->current = drawable->source = &drawable->buffers[0];
	drawable->num_bufs = 1;
	drawable->display = display;
	drawable->buffer_type = WLWS_BUFFER_USER_MEMORY;

	switch(buffer->format & D_MASK_FORMAT) {
	case EGL_NATIVE_PIXFORMAT_RGB565_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_RGB565;
		bpp = 16;
		break;
	case EGL_NATIVE_PIXFORMAT_ARGB1555_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_ARGB1555;
		bpp = 16;
		break;
	case EGL_NATIVE_PIXFORMAT_ARGB4444_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_ARGB4444;
		bpp = 16;
		break;
	case EGL_NATIVE_PIXFORMAT_ARGB8888_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_ARGB8888;
		bpp = 32;
		break;
	case EGL_NATIVE_PIXFORMAT_UYVY_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_UYVY;
		bpp = 16;
		yuv = 1;
		break;
	case EGL_NATIVE_PIXFORMAT_NV12_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_NV12;
		bpp = 12;
		yuv = 1;
		break;
	case EGL_NATIVE_PIXFORMAT_YUYV_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_YUYV;
		bpp = 16;
		yuv = 1;
		break;
	case EGL_NATIVE_PIXFORMAT_VYUY_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_VYUY;
		bpp = 16;
		yuv = 1;
		break;
	case EGL_NATIVE_PIXFORMAT_YVYU_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_YVYU;
		bpp = 16;
		yuv = 1;
		break;
	case EGL_NATIVE_PIXFORMAT_NV21_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_NV21;
		bpp = 12;
		yuv = 1;
		break;
	case EGL_NATIVE_PIXFORMAT_I420_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_I420;
		bpp = 12;
		yuv = 1;
		break;
	case EGL_NATIVE_PIXFORMAT_YV12_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_YV12;
		bpp = 12;
		yuv = 1;
		break;
	case EGL_NATIVE_PIXFORMAT_NV16_REL:
		drawable->info.pixelformat = WLWSEGL_PIXFMT_NV16;
		bpp = 16;
		yuv = 1;
		break;
	default:
		goto error;
	}

	if (yuv) {
		switch (buffer->format & D_MASK_YUV_COLORSPACE) {
		case EGL_YUV_COLORSPACE_BT601_CONFORMANT_RANGE_REL:
			drawable->info.eColorSpace = WLWSEGL_YUV_COLORSPACE_CONFORMANT_BT601;
			break;
		case EGL_YUV_COLORSPACE_BT601_FULL_RANGE_REL:
			drawable->info.eColorSpace = WLWSEGL_YUV_COLORSPACE_FULL_BT601;
			break;
		case EGL_YUV_COLORSPACE_BT709_CONFORMANT_RANGE_REL:
			drawable->info.eColorSpace = WLWSEGL_YUV_COLORSPACE_CONFORMANT_BT709;
			break;
		case EGL_YUV_COLORSPACE_BT709_FULL_RANGE_REL:
			drawable->info.eColorSpace = WLWSEGL_YUV_COLORSPACE_FULL_BT709;
			break;
		default:
			drawable->info.eColorSpace = WLWSEGL_YUV_COLORSPACE_FULL_BT601;
		}
		/* [RELCOMMENT P-0149] eChromaUInterp and eChromaVInterp are extra attributes that doesn't open. */
		switch (buffer->format & D_MASK_YUV_CHROMA_INTERP_U) {
		case EGL_CHROMA_INTERP_U_ZERO_REL:
			drawable->info.eChromaUInterp = IMG_CHROMA_INTERP_ZERO;
			break;
		case EGL_CHROMA_INTERP_U_QUATER_REL:
			drawable->info.eChromaUInterp = IMG_CHROMA_INTERP_QUARTER;
			break;
		case EGL_CHROMA_INTERP_U_HALF_REL:
			drawable->info.eChromaUInterp = IMG_CHROMA_INTERP_HALF;
			break;
		case EGL_CHROMA_INTERP_U_THREEQUARTERS_REL:
			drawable->info.eChromaUInterp = IMG_CHROMA_INTERP_THREEQUARTERS;
			break;
		default:
			drawable->info.eChromaUInterp = IMG_CHROMA_INTERP_ZERO;
		}

		switch (buffer->format & D_MASK_YUV_CHROMA_INTERP_V) {
		case EGL_CHROMA_INTERP_V_ZERO_REL:
			drawable->info.eChromaVInterp = IMG_CHROMA_INTERP_ZERO;
			break;
		case EGL_CHROMA_INTERP_V_QUATER_REL:
			drawable->info.eChromaVInterp = IMG_CHROMA_INTERP_QUARTER;
			break;
		case EGL_CHROMA_INTERP_V_HALF_REL:
			drawable->info.eChromaVInterp = IMG_CHROMA_INTERP_HALF;
			break;
		case EGL_CHROMA_INTERP_V_THREEQUARTERS_REL:
			drawable->info.eChromaVInterp = IMG_CHROMA_INTERP_THREEQUARTERS;
			break;
		default:
			drawable->info.eChromaVInterp = IMG_CHROMA_INTERP_ZERO;
		}
	}

	/* [RELCOMMENT P-0149] If format is YUV 2 or 3plane, set stride for Component of Y plane.*/
	switch (drawable->info.pixelformat) {
	case WLWSEGL_PIXFMT_NV12:
	case WLWSEGL_PIXFMT_NV21:
	case WLWSEGL_PIXFMT_I420:
	case WLWSEGL_PIXFMT_YV12:
		drawable->info.pitch = buffer->stride;
		break;
	default:
		drawable->info.pitch = (buffer->stride * bpp) >> 3;
	}

	drawable->info.width  = buffer->width;
	drawable->info.height = buffer->height;
	drawable->info.stride = buffer->stride;
	drawable->info.size   = (drawable->info.stride * drawable->info.height * bpp) >> 3;
	drawable->current->addr = buffer->pixelData;

	WSEGL_DEBUG("%s: %s: %d: buffer = %p (%dx%d, stride(pitch in wsegl)=%d, size=%d, format=%08x)\n",
			__FILE__, __func__, __LINE__, buffer, buffer->width, buffer->height, buffer->stride, drawable->info.size, buffer->format);

	return drawable;

error:
	free(drawable);
	return NULL;
}

/***********************************************************************************
 Function Name      : WSEGL_CreatePixmapDrawable
 Inputs             : hDisplay, psConfig, hNativePixmap
 Outputs            : phDrawable, eRotationAngle
 Returns            : Error code
 Description        : Create a pixmap drawable for a native pixmap
************************************************************************************/
static WSEGLError WSEGLc_CreatePixmapDrawable(WSEGLDisplayHandle hDisplay,
					      WSEGLConfig *psConfig,
					      WSEGLDrawableHandle *phDrawable,
					      NativePixmapType hNativePixmap,
					      WLWSEGL_ROTATION *eRotationAngle,
					      WLWSEGL_COLOURSPACE_FORMAT eColorSpace,
					      bool bIsProtected)
{
	WLWSClientDisplay *display = (WLWSClientDisplay*)hDisplay;
	WLWSClientDrawable *drawable = NULL;
	struct wl_kms_buffer *kms_buffer;
	WSEGL_UNREFERENCED_PARAMETER(psConfig);
	WSEGL_UNREFERENCED_PARAMETER(eRotationAngle);
	WSEGL_UNREFERENCED_PARAMETER(eColorSpace);
	WSEGL_UNREFERENCED_PARAMETER(bIsProtected);

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	kms_buffer = wayland_kms_buffer_get((struct wl_resource*)hNativePixmap);
	if (kms_buffer) {
		/* check if we already have a drawable associated with this wl_kms_buffer */
		if (kms_buffer->private) {
			drawable = (WLWSClientDrawable*)kms_buffer->private;
			drawable->ref_count++;
			*phDrawable = (WSEGLDrawableHandle)drawable;
			return WSEGL_SUCCESS;
		}

		if (!(drawable = import_wl_kms_buffer(display, kms_buffer)))
			goto error;
	} else {
		EGLNativePixmapTypeREL *rel_buffer = (EGLNativePixmapTypeREL*)hNativePixmap;

		if (validate_rel_pixmap(rel_buffer) != 0)
			goto error;

		if (!(drawable = import_native_rel_buffer(display, rel_buffer)))
			goto error;
	}

	if (!(drawable->current->map = pvr_map_memory(display->context, drawable->current->addr, drawable->info.size)))
		goto error;

	drawable->info.ui32DrawableType = WSEGL_DRAWABLE_PIXMAP;
	drawable->ref_count = 1;

	*phDrawable = (WSEGLDrawableHandle)drawable;
	return WSEGL_SUCCESS;

error:
	if (drawable) {
		_kms_release_buffer(drawable, drawable->current);
		free(drawable);
	}
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
static WSEGLError WSEGLc_DeleteDrawable(WSEGLDrawableHandle hDrawable)
{
	WLWSClientDrawable *drawable = (WLWSClientDrawable*)hDrawable;
	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	if (!drawable)
		return WSEGL_BAD_NATIVE_PIXMAP;

	drawable->ref_count--;
	if (drawable->ref_count > 0)
		return WSEGL_SUCCESS;

	/* reset resize callback */
	if (drawable->window) {
		drawable->window->resize_callback = NULL;
		SET_EGL_WINDOW_PRIVATE(drawable->window, NULL);
		if (drawable->surface->frame_sync)
			wl_callback_destroy(drawable->surface->frame_sync);
		free(drawable->surface);
		if (drawable->display->callback) {
			wl_callback_destroy(drawable->display->callback);
			drawable->display->callback = NULL;
		}
	}

	if (drawable->pixmap_kms_buffer_in_use)
		return WSEGL_SUCCESS;

	switch (drawable->buffer_type) {
	case WLWS_BUFFER_KMS_BO:
		_kms_release_buffers(drawable);
		break;

	case WLWS_BUFFER_USER_MEMORY:
		pvr_unmap_memory(drawable->display->context, drawable->current->map);
		break;

	default:
		WSEGL_DEBUG("unknown buffer type: %d\n", drawable->buffer_type);
	}

	free(drawable);
	
	return WSEGL_SUCCESS;
}

static void wayland_surface_damage_buffer(struct wl_surface *surface, WLWSDrawableInfo *info,
					  const EGLint *rects, EGLint num_rects)
{
	int i;
	for (i = 0; i < num_rects; i++) {
		int idx = i * 4;
		wl_surface_damage_buffer(surface,
					 rects[idx], info->height - rects[idx + 1] - rects[idx + 3],
					 rects[idx + 2], rects[idx + 3]);
	}
}

static int wayland_commit_buffer(WLWSClientDisplay *display,
				 WLWSClientDrawable *drawable,
				 const EGLint *rects, EGLint num_rects)
{
	struct wl_buffer *buffer;
	struct kms_buffer *kms_buffer = drawable->current;
	struct wl_egl_window *window = drawable->window;
	int interval = drawable->surface->interval;

	/* Sync with the server. */
	if (drawable->surface->frame_sync) {
		WSEGL_DEBUG("%s: %s: sync frame.\n", __FILE__, __func__);

		wl_display_dispatch_queue_pending(display->wl_display,
						  display->wl_queue);
		while (drawable->surface->frame_sync) {
			WSEGL_DEBUG("%s: %s: wait for sync (%p(@%p))\n",
				    __FILE__, __func__, drawable->surface->frame_sync, &drawable->surface->frame_sync);
			if (wl_display_dispatch_queue(display->wl_display,
						      display->wl_queue) < 0)
				break;
		}
	}

	/*
	 * Create wl_buffer. make sure that we get notified
	 * when the fornt buffer is released by the compositor.
	 * The compositor always holds at least one buffer for display.
	 * We create wl_buffer with the KMS BO handle.
	 */
	if (!(buffer = wayland_get_wl_buffer(display, kms_buffer))) {
		// we failed to get wl_buffer...Nothing we can do...
		WSEGL_DEBUG("%s: %s: %d: Unrecoverable error.\n", __FILE__, __func__, __LINE__);
		return -1;
	}

	WSEGL_DEBUG("%s: %s: got wl_buffer.\n", __FILE__, __func__);

	/*
	 * For SwapInterval.
	 */
	if (interval > 0)
		wayland_set_callback(display, wl_surface_frame(window->surface),
				     &drawable->surface->frame_sync, "wl_surface_frame()");

	WSEGL_DEBUG("%s: %s: attach wl_buffer.\n", __FILE__, __func__);
	/*
	 * After creating wl_buffer, we can now attach the wl_buffer
	 * to the wl_surface and send it to the compositor.
	 */
	wl_surface_attach(window->surface, buffer, window->dx, window->dy);

	window->attached_width = drawable->info.width;
	window->attached_height = drawable->info.height;
	window->dx = window->dy = 0;

	if (num_rects && drawable->enable_damage_buffer)
		wayland_surface_damage_buffer(window->surface, &drawable->info,
					      rects, num_rects);
	else
		wl_surface_damage(window->surface, 0, 0,
				  drawable->info.width, drawable->info.height);

	wl_surface_commit(window->surface);

	WSEGL_DEBUG("%s: %s: commited surface.\n", __FILE__, __func__);
	// just to throttle.
	if (!drawable->surface->frame_sync)
		wayland_set_callback(display, wl_display_sync(display->wl_display), NULL,
				     "wl_display_sync(1)");

	wl_display_flush(display->wl_display);

	return 0;
}

/******************************************************************************
****
 Function Name      : WSEGL_SwapDrawableWithDamage
 Inputs             : hDrawable, pasDamageRect, uiNumDamageRect, hFence
 Outputs            : None
 Returns            : Error code
 Description        : Post the colour buffer of a window drawable to a window
*******************************************************************************
****/
static WSEGLError WSEGLc_SwapDrawableWithDamage(WSEGLDrawableHandle hDrawable, EGLint *pasDamageRect, EGLint uiNumDamageRect, PVRSRV_FENCE hFence)
{
	WLWSClientDrawable *drawable = (WLWSClientDrawable*)hDrawable;
	WLWSClientDisplay *display = drawable->display;
	int i;
	PVRSRVFenceDestroyExt(display->context->connection, hFence);

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	/* NOP if current buffer is NULL */
	if (!drawable->current)
		return WSEGL_SUCCESS;

	for (i = 0; i < drawable->num_bufs; i++) {
		if (drawable->buffers[i].buffer_age > 0)
			drawable->buffers[i].buffer_age++;
	}
	drawable->current->buffer_age = 1;

	/* mark that the buffer is locked. */
	drawable->current->flag |= KMS_BUFFER_FLAG_LOCKED;

	if (wayland_commit_buffer(display, drawable, pasDamageRect, uiNumDamageRect))
		return WSEGL_BAD_NATIVE_WINDOW;

	/*
	 * We now have to get the new empty buffer.
	 */
	drawable->source = drawable->current;
	drawable->current = get_free_buffer(drawable);

	return WSEGL_SUCCESS;
}


/***********************************************************************************
 Function Name      : WSEGL_SwapControlInterval
 Inputs             : hDrawable, ui32Interval
 Outputs            : None
 Returns            : Error code
 Description        : Set the swap interval of a window drawable
************************************************************************************/
static WSEGLError WSEGLc_SwapControlInterval(WSEGLDrawableHandle hDrawable, EGLint interval)
{
	WLWSClientDrawable *drawable = (WLWSClientDrawable*)hDrawable;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	drawable->surface->interval = (int)interval;

	return WSEGL_SUCCESS;
}


/***********************************************************************************
 Function Name      : WSEGL_WaitNative
 Inputs             : hDrawable, ui32Engine
 Outputs            : None
 Returns            : Error code
 Description        : Flush any native rendering requests on a drawable
************************************************************************************/
static WSEGLError WSEGLc_WaitNative(WSEGLDrawableHandle hDrawable, EGLint engine)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

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
static WSEGLError WSEGLc_CopyFromDrawable(WSEGLDrawableHandle hDrawable, NativePixmapType hNativePixmap)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	WSEGL_UNREFERENCED_PARAMETER(hNativePixmap);

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

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
static WSEGLError WSEGLc_CopyFromPBuffer(PVRSRV_MEMDESC hMemDesc,
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

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);
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
static WSEGLError WSEGLc_GetDrawableParameters(WSEGLDrawableHandle hDrawable,
						  WSEGLDrawableParams *psSourceParams,
						  WSEGLDrawableParams *psRenderParams)
{
	WLWSClientDrawable *drawable = (WLWSClientDrawable*)hDrawable;

	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	/*
	 * This will let IMG EGL to delete the drawable, and then recreate
	 * the drawable from the native window, i.e. drawable->resized is reset
	 * automatically.
	 */
	if (drawable->resized)
		return WSEGL_BAD_DRAWABLE;

	/*
	 * We need to wait for buffer release if the drawable is a window.
	 */
	wayland_wait_for_buffer_release(drawable);

	if (drawable->current == NULL)
		return WSEGL_BAD_DRAWABLE;

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
static WSEGLError WSEGLc_GetImageParameters(WSEGLDrawableHandle hDrawable,
					    WSEGLImageParams *psImageParams,
					    unsigned long ulPlaneOffset)
{
	WLWSClientDrawable *drawable = (WLWSClientDrawable*)hDrawable;
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
static WSEGLError WSEGLc_ConnectDrawable(WSEGLDrawableHandle hDrawable)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

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
static WSEGLError WSEGLc_DisconnectDrawable(WSEGLDrawableHandle hDrawable)
{
	WSEGL_UNREFERENCED_PARAMETER(hDrawable);
	WSEGL_DEBUG("%s: %s: %d\n", __FILE__, __func__, __LINE__);

	// TODO: Should we release the drawable?

	return WSEGL_SUCCESS;
}

static WSEGLError WSEGLc_AcquireCPUMapping(WSEGLDrawableHandle hDrawable,
					   PVRSRV_MEMDESC hMemDesc,
					   void **ppvCpuVirtAddr)
{
	WLWSClientDrawable *drawable = (WLWSClientDrawable*)hDrawable;

	if (drawable->info.ui32DrawableType == WSEGL_DRAWABLE_WINDOW) {
		if (!pvr_acquire_cpu_mapping(hMemDesc, ppvCpuVirtAddr))
			return WSEGL_BAD_DRAWABLE;
	} else {
		/* WSEGL_DRAWABLE_PIXMAP */
		*ppvCpuVirtAddr = drawable->current->addr;
	}

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
static WSEGLError WSEGLc_ReleaseCPUMapping(WSEGLDrawableHandle hDrawable,
					   PVRSRV_MEMDESC hMemDesc)
{
	WLWSClientDrawable *drawable = (WLWSClientDrawable*)hDrawable;

	if (drawable->info.ui32DrawableType == WSEGL_DRAWABLE_WINDOW)
		pvr_release_cpu_mapping(hMemDesc);

	return WSEGL_SUCCESS;
}

/**********************************************************************
 *
 *       WARNING: Do not modify any code below this point
 *
 ***********************************************************************/ 

const WSEGL_FunctionTable __attribute__((visibility("internal"))) *WSEGLc_getFunctionTable()
{
	static const WSEGL_FunctionTable client_func_table =
	{
		.pfnWSEGL_InitialiseDisplay = WSEGLc_InitialiseDisplay,
		.pfnWSEGL_CloseDisplay = WSEGLc_CloseDisplay,
		.pfnWSEGL_CreateWindowDrawable = WSEGLc_CreateWindowDrawable,
		.pfnWSEGL_CreatePixmapDrawable = WSEGLc_CreatePixmapDrawable,
		.pfnWSEGL_DeleteDrawable = WSEGLc_DeleteDrawable,
		.pfnWSEGL_SwapDrawableWithDamage = WSEGLc_SwapDrawableWithDamage,
		.pfnWSEGL_SwapControlInterval = WSEGLc_SwapControlInterval,
		.pfnWSEGL_WaitNative = WSEGLc_WaitNative,
		.pfnWSEGL_CopyFromDrawable = WSEGLc_CopyFromDrawable,
		.pfnWSEGL_CopyFromPBuffer = WSEGLc_CopyFromPBuffer,
		.pfnWSEGL_GetDrawableParameters = WSEGLc_GetDrawableParameters,
		.pfnWSEGL_GetImageParameters = WSEGLc_GetImageParameters,
		.pfnWSEGL_ConnectDrawable = WSEGLc_ConnectDrawable,
		.pfnWSEGL_DisconnectDrawable = WSEGLc_DisconnectDrawable,
		.pfnWSEGL_AcquireCPUMapping = WSEGLc_AcquireCPUMapping,
		.pfnWSEGL_ReleaseCPUMapping = WSEGLc_ReleaseCPUMapping,
	};
	return &client_func_table;
}

/******************************************************************************
 End of file (wayland_client.c)
******************************************************************************/
