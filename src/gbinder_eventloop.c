/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gbinder_eventloop_p.h"

#include <gutil_macros.h>

typedef struct gbinder_idle_callback_data {
    GBinderEventLoopCallback* cb;
    GBinderEventLoopCallbackFunc func;
    GDestroyNotify destroy;
    gpointer data;
} GBinderIdleCallbackData;

#define GBINDER_DEFAULT_EVENTLOOP (&gbinder_eventloop_glib)

static const GBinderEventLoopIntegration gbinder_eventloop_glib;

/*==========================================================================*
 * GLib event loop integration
 *==========================================================================*/

typedef struct gbinder_eventloop_glib_timeout {
    GBinderEventLoopTimeout timeout;
    guint id;
    GSourceFunc func;
    gpointer data;
} GBinderEventLoopTimeoutGLib;

typedef struct gbinder_eventloop_glib_callback {
    GSource source;
    GBinderEventLoopCallback callback;
} GBinderEventLoopCallbackGLib;

static
inline
GBinderEventLoopTimeoutGLib*
gbinder_eventloop_glib_timeout_cast(
    GBinderEventLoopTimeout* timeout)
{
    return G_CAST(timeout,GBinderEventLoopTimeoutGLib,timeout);
}

static
inline
GSource*
gbinder_eventloop_glib_callback_source(
    GBinderEventLoopCallback* callback)
{
    return &(G_CAST(callback,GBinderEventLoopCallbackGLib,callback)->source);
}

static
gboolean
gbinder_eventloop_glib_timeout_callback(
    gpointer data)
{
    GBinderEventLoopTimeoutGLib* timeout = data;

    return timeout->func(timeout->data);
}

static
void
gbinder_eventloop_glib_timeout_finalize(
    gpointer data)
{
    g_slice_free1(sizeof(GBinderEventLoopTimeoutGLib), data);
}

static
GBinderEventLoopTimeout*
gbinder_eventloop_glib_timeout_add(
    guint interval,
    GSourceFunc func,
    gpointer data)
{
    GBinderEventLoopTimeoutGLib* impl =
        g_slice_new(GBinderEventLoopTimeoutGLib);

    impl->timeout.eventloop = &gbinder_eventloop_glib;
    impl->func = func;
    impl->data = data;
    impl->id = g_timeout_add_full(G_PRIORITY_DEFAULT, interval,
        gbinder_eventloop_glib_timeout_callback, impl,
        gbinder_eventloop_glib_timeout_finalize);
    return &impl->timeout;
}

static
void
gbinder_eventloop_glib_timeout_remove(
    GBinderEventLoopTimeout* timeout)
{
    g_source_remove(gbinder_eventloop_glib_timeout_cast(timeout)->id);
}

static
gboolean
gbinder_eventloop_glib_callback_prepare(
    GSource* source,
    gint* timeout)
{
    *timeout = 0;
    return TRUE;
}

static
gboolean
gbinder_eventloop_glib_callback_check(
    GSource* source)
{
    return TRUE;
}

static
gboolean
gbinder_eventloop_glib_callback_dispatch(
    GSource* source,
    GSourceFunc callback,
    gpointer user_data)
{
    ((GBinderEventLoopCallbackFunc)callback)(user_data);
    return G_SOURCE_REMOVE;
}

static
GBinderEventLoopCallback*
gbinder_eventloop_glib_callback_new(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify finalize)
{
    static GSourceFuncs callback_funcs = {
        gbinder_eventloop_glib_callback_prepare,
        gbinder_eventloop_glib_callback_check,
        gbinder_eventloop_glib_callback_dispatch
    };

    GBinderEventLoopCallbackGLib* impl = (GBinderEventLoopCallbackGLib*)
        g_source_new(&callback_funcs, sizeof(GBinderEventLoopCallbackGLib));

    impl->callback.eventloop = &gbinder_eventloop_glib;
    g_source_set_callback(&impl->source, (GSourceFunc) func, data, finalize);
    return &impl->callback;
}

static
void
gbinder_eventloop_glib_callback_ref(
    GBinderEventLoopCallback* cb)
{
    g_source_ref(gbinder_eventloop_glib_callback_source(cb));
}

static
void
gbinder_eventloop_glib_callback_unref(
    GBinderEventLoopCallback* cb)
{
    g_source_unref(gbinder_eventloop_glib_callback_source(cb));
}

static
void
gbinder_eventloop_glib_callback_schedule(
    GBinderEventLoopCallback* cb)
{
    static GMainContext* context = NULL;

    if (!context) context = g_main_context_default();
    g_source_attach(gbinder_eventloop_glib_callback_source(cb), context);
}

static
void
gbinder_eventloop_glib_callback_cancel(
    GBinderEventLoopCallback* cb)
{
    g_source_destroy(gbinder_eventloop_glib_callback_source(cb));
}

static
void
gbinder_eventloop_glib_cleanup(
    void)
{
}

static const GBinderEventLoopIntegration gbinder_eventloop_glib = {
    gbinder_eventloop_glib_timeout_add,
    gbinder_eventloop_glib_timeout_remove,
    gbinder_eventloop_glib_callback_new,
    gbinder_eventloop_glib_callback_ref,
    gbinder_eventloop_glib_callback_unref,
    gbinder_eventloop_glib_callback_schedule,
    gbinder_eventloop_glib_callback_cancel,
    gbinder_eventloop_glib_cleanup
};

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
gbinder_idle_callback_invoke_proc(
    void* user_data)
{
    GBinderIdleCallbackData* idle = user_data;

    if (idle->func) {
        idle->func(idle->data);
    }
    gbinder_idle_callback_unref(idle->cb);
}

static
void
gbinder_idle_callback_invoke_done(
    void* user_data)
{
    GBinderIdleCallbackData* idle = user_data;

    if (idle->destroy) {
        idle->destroy(idle->data);
    }
    gutil_slice_free(idle);
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

static const GBinderEventLoopIntegration* gbinder_eventloop =
    GBINDER_DEFAULT_EVENTLOOP;

GBinderEventLoopTimeout*
gbinder_timeout_add(
    guint interval,
    GSourceFunc function,
    gpointer data)
{
    return gbinder_eventloop->timeout_add(interval, function, data);
}

GBinderEventLoopTimeout*
gbinder_idle_add(
    GSourceFunc function,
    gpointer data)
{
    return gbinder_eventloop->timeout_add(0, function, data);
}

void
gbinder_timeout_remove(
    GBinderEventLoopTimeout* timeout)
{
    if (timeout) {
        timeout->eventloop->timeout_remove(timeout);
    }
}

GBinderEventLoopCallback*
gbinder_idle_callback_new(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify finalize)
{
    return gbinder_eventloop->callback_new(func, data, finalize);
}

GBinderEventLoopCallback*
gbinder_idle_callback_schedule_new(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify finalize)
{
    GBinderEventLoopCallback* cb =
        gbinder_eventloop->callback_new(func, data, finalize);

    gbinder_idle_callback_schedule(cb);
    return cb;
}

GBinderEventLoopCallback*
gbinder_idle_callback_ref(
    GBinderEventLoopCallback* cb)
{
    if (cb) {
        cb->eventloop->callback_ref(cb);
        return cb;
    }
    return NULL;
}

void
gbinder_idle_callback_unref(
    GBinderEventLoopCallback* cb)
{
    if (cb) {
        cb->eventloop->callback_unref(cb);
    }
}

void
gbinder_idle_callback_schedule(
    GBinderEventLoopCallback* cb)
{
    if (cb) {
        cb->eventloop->callback_schedule(cb);
    }
}

void
gbinder_idle_callback_cancel(
    GBinderEventLoopCallback* cb)
{
    if (cb) {
        cb->eventloop->callback_cancel(cb);
    }
}

void
gbinder_idle_callback_destroy(
    GBinderEventLoopCallback* cb)
{
    if (cb) {
        const GBinderEventLoopIntegration* eventloop = cb->eventloop;

        eventloop->callback_cancel(cb);
        eventloop->callback_unref(cb);
    }
}

/* Non-cancellable callback */
void
gbinder_idle_callback_invoke_later(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify destroy)
{
    GBinderIdleCallbackData* idle = g_slice_new(GBinderIdleCallbackData);

    idle->func = func;
    idle->data = data;
    idle->destroy = destroy;
    idle->cb = gbinder_idle_callback_new(gbinder_idle_callback_invoke_proc,
        idle, gbinder_idle_callback_invoke_done);
    gbinder_idle_callback_schedule(idle->cb);
}

/*==========================================================================*
 * Public interface
 *==========================================================================*/

void
gbinder_eventloop_set(
    const GBinderEventLoopIntegration* loop)
{
    if (!loop) loop = GBINDER_DEFAULT_EVENTLOOP;
    if (gbinder_eventloop != loop) {
        const GBinderEventLoopIntegration* prev = gbinder_eventloop;

        gbinder_eventloop = loop;
        prev->cleanup();
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
