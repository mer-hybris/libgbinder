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

#ifndef GBINDER_EVENTLOOP_PRIVATE_H
#define GBINDER_EVENTLOOP_PRIVATE_H

#include "gbinder_types_p.h"
#include "gbinder_eventloop.h"

GBinderEventLoopTimeout*
gbinder_timeout_add(
    guint millis,
    GSourceFunc func,
    gpointer data)
    G_GNUC_WARN_UNUSED_RESULT
    GBINDER_INTERNAL;

GBinderEventLoopTimeout*
gbinder_idle_add(
    GSourceFunc func,
    gpointer data)
    G_GNUC_WARN_UNUSED_RESULT
    GBINDER_INTERNAL;

void
gbinder_timeout_remove(
    GBinderEventLoopTimeout* timeout)
    GBINDER_INTERNAL;

GBinderEventLoopCallback*
gbinder_idle_callback_new(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify destroy)
    G_GNUC_WARN_UNUSED_RESULT
    GBINDER_INTERNAL;

GBinderEventLoopCallback*
gbinder_idle_callback_schedule_new(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify destroy)
    G_GNUC_WARN_UNUSED_RESULT
    GBINDER_INTERNAL;

GBinderEventLoopCallback*
gbinder_idle_callback_ref(
    GBinderEventLoopCallback* cb)
    GBINDER_INTERNAL;

void
gbinder_idle_callback_unref(
    GBinderEventLoopCallback* cb)
    GBINDER_INTERNAL;

void
gbinder_idle_callback_schedule(
    GBinderEventLoopCallback* cb)
    GBINDER_INTERNAL;

void
gbinder_idle_callback_cancel(
    GBinderEventLoopCallback* cb)
    GBINDER_INTERNAL;

void
gbinder_idle_callback_destroy(
    GBinderEventLoopCallback* cb)
    GBINDER_INTERNAL;

void
gbinder_idle_callback_invoke_later(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify destroy)
    GBINDER_INTERNAL;

#endif /* GBINDER_EVENTLOOP_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
