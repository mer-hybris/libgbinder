/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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

#ifndef GBINDER_DRIVER_H
#define GBINDER_DRIVER_H

#include "gbinder_types_p.h"

struct pollfd;

GBinderDriver*
gbinder_driver_new(
    const char* dev,
    const GBinderRpcProtocol* protocol)
    GBINDER_INTERNAL;

GBinderDriver*
gbinder_driver_ref(
    GBinderDriver* driver)
    GBINDER_INTERNAL;

void
gbinder_driver_unref(
    GBinderDriver* driver)
    GBINDER_INTERNAL;

int
gbinder_driver_fd(
    GBinderDriver* driver)
    GBINDER_INTERNAL;

int
gbinder_driver_poll(
    GBinderDriver* driver,
    struct pollfd* pollfd)
    GBINDER_INTERNAL;

const char*
gbinder_driver_dev(
    GBinderDriver* driver)
    GBINDER_INTERNAL;

const GBinderIo*
gbinder_driver_io(
    GBinderDriver* driver)
    GBINDER_INTERNAL;

gboolean
gbinder_driver_request_death_notification(
    GBinderDriver* driver,
    GBinderRemoteObject* obj)
    GBINDER_INTERNAL;

gboolean
gbinder_driver_clear_death_notification(
    GBinderDriver* driver,
    GBinderRemoteObject* obj)
    GBINDER_INTERNAL;

gboolean
gbinder_driver_increfs(
    GBinderDriver* driver,
    guint32 handle)
    GBINDER_INTERNAL;

gboolean
gbinder_driver_decrefs(
    GBinderDriver* driver,
    guint32 handle)
    GBINDER_INTERNAL;

gboolean
gbinder_driver_acquire(
    GBinderDriver* driver,
    guint32 handle)
    GBINDER_INTERNAL;

gboolean
gbinder_driver_release(
    GBinderDriver* driver,
    guint32 handle)
    GBINDER_INTERNAL;

void
gbinder_driver_close_fds(
    GBinderDriver* self,
    void** objects,
    const void* end)
    GBINDER_INTERNAL;

void
gbinder_driver_free_buffer(
    GBinderDriver* driver,
    void* buffer)
    GBINDER_INTERNAL;

gboolean
gbinder_driver_enter_looper(
    GBinderDriver* driver)
    GBINDER_INTERNAL;

gboolean
gbinder_driver_exit_looper(
    GBinderDriver* driver)
    GBINDER_INTERNAL;

int
gbinder_driver_read(
    GBinderDriver* driver,
    GBinderObjectRegistry* reg,
    GBinderHandler* handler)
    GBINDER_INTERNAL;

int
gbinder_driver_transact(
    GBinderDriver* driver,
    GBinderObjectRegistry* reg,
    GBinderHandler* handler,
    guint32 handle,
    guint32 code,
    GBinderLocalRequest* request,
    GBinderRemoteReply* reply)
    GBINDER_INTERNAL;

int
gbinder_driver_ping(
    GBinderDriver* driver,
    GBinderObjectRegistry* reg,
    guint32 handle)
    GBINDER_INTERNAL;

GBinderLocalRequest*
gbinder_driver_local_request_new(
    GBinderDriver* self,
    const char* iface)
    GBINDER_INTERNAL;

#endif /* GBINDER_DRIVER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
