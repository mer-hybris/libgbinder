/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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

#ifndef GBINDER_LOCAL_OBJECT_PRIVATE_H
#define GBINDER_LOCAL_OBJECT_PRIVATE_H

#include <gbinder_local_object.h>

#include "gbinder_types_p.h"

#include <glib-object.h>

/*
 * Some of this stuff may become public if we decide to allow the clients
 * to derive their own classes from GBinderLocalObject. For now it's all
 * private.
 */

typedef
void
(*GBinderLocalObjectFunc)(
    GBinderLocalObject* obj,
    void* user_data);

typedef struct gbinder_local_object_priv GBinderLocalObjectPriv;
struct gbinder_local_object {
    GObject object;
    GBinderLocalObjectPriv* priv;
    GBinderIpc* ipc;
    const char* const* ifaces;
    gint weak_refs;
    gint strong_refs;
};

typedef enum gbinder_local_transaction_support {
    GBINDER_LOCAL_TRANSACTION_NOT_SUPPORTED,
    GBINDER_LOCAL_TRANSACTION_SUPPORTED,     /* On the main thread */
    GBINDER_LOCAL_TRANSACTION_LOOPER         /* On the looper thread */
} GBINDER_LOCAL_TRANSACTION_SUPPORT;

typedef struct gbinder_local_object_class {
    GObjectClass parent;
    GBINDER_LOCAL_TRANSACTION_SUPPORT (*can_handle_transaction)
    (GBinderLocalObject* self, const char* iface, guint code);
    GBinderLocalReply* (*handle_transaction)
        (GBinderLocalObject* self, GBinderRemoteRequest* req, guint code,
            guint flags, int* status);
    GBinderLocalReply* (*handle_looper_transaction)
        (GBinderLocalObject* self, GBinderRemoteRequest* req, guint code,
            guint flags, int* status);
    void (*acquire)(GBinderLocalObject* self);
    void (*release)(GBinderLocalObject* self);
    void (*drop)(GBinderLocalObject* self);
    /* Need to add some placeholders if this class becomes public */
} GBinderLocalObjectClass;

GType gbinder_local_object_get_type(void) GBINDER_INTERNAL;
#define GBINDER_TYPE_LOCAL_OBJECT (gbinder_local_object_get_type())
#define GBINDER_LOCAL_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        GBINDER_TYPE_LOCAL_OBJECT, GBinderLocalObject))
#define GBINDER_LOCAL_OBJECT_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), \
        GBINDER_TYPE_LOCAL_OBJECT, GBinderLocalObjectClass)

#define gbinder_local_object_dev(obj) (gbinder_driver_dev((obj)->ipc->driver))
#define gbinder_local_object_io(obj) (gbinder_driver_io((obj)->ipc->driver))

GBinderLocalObject*
gbinder_local_object_new_with_type(
    GType type,
    GBinderIpc* ipc,
    const char* const* ifaces,
    GBinderLocalTransactFunc txproc,
    void* user_data)
    GBINDER_INTERNAL;

void
gbinder_local_object_init_base(
    GBinderLocalObject* self,
    GBinderIpc* ipc,
    const char* const* ifaces,
    GBinderLocalTransactFunc txproc,
    void* user_data)
    GBINDER_INTERNAL;

gulong
gbinder_local_object_add_weak_refs_changed_handler(
    GBinderLocalObject* obj,
    GBinderLocalObjectFunc func,
    void* user_data)
    GBINDER_INTERNAL;

gulong
gbinder_local_object_add_strong_refs_changed_handler(
    GBinderLocalObject* obj,
    GBinderLocalObjectFunc func,
    void* user_data)
    GBINDER_INTERNAL;

void
gbinder_local_object_remove_handler(
    GBinderLocalObject* obj,
    gulong id)
    GBINDER_INTERNAL;

GBINDER_LOCAL_TRANSACTION_SUPPORT
gbinder_local_object_can_handle_transaction(
    GBinderLocalObject* self,
    const char* iface,
    guint code)
    GBINDER_INTERNAL;

GBinderLocalReply*
gbinder_local_object_handle_transaction(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
    GBINDER_INTERNAL;

GBinderLocalReply*
gbinder_local_object_handle_looper_transaction(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
    GBINDER_INTERNAL;

void
gbinder_local_object_handle_increfs(
    GBinderLocalObject* obj)
    GBINDER_INTERNAL;

void
gbinder_local_object_handle_decrefs(
    GBinderLocalObject* obj)
    GBINDER_INTERNAL;

void
gbinder_local_object_handle_acquire(
    GBinderLocalObject* obj,
    GBinderBufferContentsList* bufs)
    GBINDER_INTERNAL;

void
gbinder_local_object_handle_release(
    GBinderLocalObject* obj)
    GBINDER_INTERNAL;

#endif /* GBINDER_LOCAL_OBJECT_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
