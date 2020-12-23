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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_local_object_p.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_remote_request.h"
#include "gbinder_writer.h"
#include "gbinder_log.h"

#include <gutil_strv.h>

#include <errno.h>

struct gbinder_local_object_priv {
    GMainContext* context;
    char** ifaces;
    GBinderLocalTransactFunc txproc;
    void* user_data;
};

G_DEFINE_TYPE(GBinderLocalObject, gbinder_local_object, G_TYPE_OBJECT)

#define GBINDER_LOCAL_OBJECT_GET_CLASS(obj) \
    G_TYPE_INSTANCE_GET_CLASS((obj), GBINDER_TYPE_LOCAL_OBJECT, \
    GBinderLocalObjectClass)

typedef
GBinderLocalReply*
(*GBinderLocalObjectTxHandler)(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    int* status);

enum gbinder_local_object_signal {
    SIGNAL_WEAK_REFS_CHANGED,
    SIGNAL_STRONG_REFS_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_WEAK_REFS_CHANGED_NAME    "weak_refs_changed"
#define SIGNAL_STRONG_REFS_CHANGED_NAME  "strong_refs_changed"

static guint gbinder_local_object_signals[SIGNAL_COUNT] = { 0 };

static const char hidl_base_interface[] = "android.hidl.base@1.0::IBase";

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
GBINDER_LOCAL_TRANSACTION_SUPPORT
gbinder_local_object_default_can_handle_transaction(
    GBinderLocalObject* self,
    const char* iface,
    guint code)
{
    switch (code) {
    case GBINDER_PING_TRANSACTION:
    case GBINDER_INTERFACE_TRANSACTION:
        return GBINDER_LOCAL_TRANSACTION_LOOPER;
    case HIDL_PING_TRANSACTION:
    case HIDL_GET_DESCRIPTOR_TRANSACTION:
    case HIDL_DESCRIPTOR_CHAIN_TRANSACTION:
        if (!g_strcmp0(iface, hidl_base_interface)) {
            return GBINDER_LOCAL_TRANSACTION_LOOPER;
        }
        /* no break */
    default:
        return self->priv->txproc ? GBINDER_LOCAL_TRANSACTION_SUPPORTED :
            GBINDER_LOCAL_TRANSACTION_NOT_SUPPORTED;
    }
}

static
GBinderLocalReply*
gbinder_local_object_default_handle_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
{
    GBinderLocalObjectPriv* priv = self->priv;

    if (priv->txproc) {
        return priv->txproc(self, req, code, flags, status, priv->user_data);
    } else {
        if (status) *status = (-EBADMSG);
        return NULL;
    }
}

static
GBinderLocalReply*
gbinder_local_object_ping_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    int* status)
{
    const GBinderIo* io = gbinder_local_object_io(self);
    GBinderLocalReply* reply = gbinder_local_reply_new(io);

    GVERBOSE("  PING_TRANSACTION");
    gbinder_local_reply_append_int32(reply, GBINDER_STATUS_OK);
    *status = GBINDER_STATUS_OK;
    return reply;
}

static
GBinderLocalReply*
gbinder_local_object_interface_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    int* status)
{
    const GBinderIo* io = gbinder_local_object_io(self);
    GBinderLocalObjectPriv* priv = self->priv;
    GBinderLocalReply* reply = gbinder_local_reply_new(io);

    GVERBOSE("  INTERFACE_TRANSACTION");
    gbinder_local_reply_append_string16(reply, priv->ifaces[0]);
    *status = GBINDER_STATUS_OK;
    return reply;
}

static
GBinderLocalReply*
gbinder_local_object_hidl_ping_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    int* status)
{
    /*android.hidl.base@1.0::IBase interfaceDescriptor() */
    const GBinderIo* io = gbinder_local_object_io(self);
    GBinderLocalReply* reply = gbinder_local_reply_new(io);

    GVERBOSE("  HIDL_PING_TRANSACTION \"%s\"",
        gbinder_remote_request_interface(req));
    gbinder_local_reply_append_int32(reply, GBINDER_STATUS_OK);
    *status = GBINDER_STATUS_OK;
    return reply;
}

static
GBinderLocalReply*
gbinder_local_object_hidl_get_descriptor_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    int* status)
{
    /*android.hidl.base@1.0::IBase interfaceDescriptor() */
    const GBinderIo* io = gbinder_local_object_io(self);
    GBinderLocalObjectPriv* priv = self->priv;
    GBinderLocalReply* reply = gbinder_local_reply_new(io);
    GBinderWriter writer;

    GVERBOSE("  HIDL_GET_DESCRIPTOR_TRANSACTION \"%s\"",
        gbinder_remote_request_interface(req));
    gbinder_local_reply_init_writer(reply, &writer);
    gbinder_writer_append_int32(&writer, GBINDER_STATUS_OK);
    gbinder_writer_append_hidl_string(&writer, priv->ifaces[0]);
    *status = GBINDER_STATUS_OK;
    return reply;
}

static
GBinderLocalReply*
gbinder_local_object_hidl_descriptor_chain_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    int* status)
{
    /*android.hidl.base@1.0::IBase interfaceChain() */
    const GBinderIo* io = gbinder_local_object_io(self);
    GBinderLocalReply* reply = gbinder_local_reply_new(io);
    GBinderWriter writer;

    GVERBOSE("  HIDL_DESCRIPTOR_CHAIN_TRANSACTION \"%s\"",
        gbinder_remote_request_interface(req));

    gbinder_local_reply_init_writer(reply, &writer);
    gbinder_writer_append_int32(&writer, GBINDER_STATUS_OK);
    gbinder_writer_append_hidl_string_vec(&writer, (const char**)
        self->ifaces, -1);
    *status = GBINDER_STATUS_OK;
    return reply;
}

static
GBinderLocalReply*
gbinder_local_object_default_handle_looper_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
{
    GBinderLocalObjectTxHandler handler = NULL;

    switch (code) {
    case GBINDER_PING_TRANSACTION:
        handler = gbinder_local_object_ping_transaction;
        break;
    case GBINDER_INTERFACE_TRANSACTION:
        handler = gbinder_local_object_interface_transaction;
        break;
    case HIDL_PING_TRANSACTION:
        handler = gbinder_local_object_hidl_ping_transaction;
        break;
    case HIDL_GET_DESCRIPTOR_TRANSACTION:
        handler = gbinder_local_object_hidl_get_descriptor_transaction;
        break;
    case HIDL_DESCRIPTOR_CHAIN_TRANSACTION:
        handler = gbinder_local_object_hidl_descriptor_chain_transaction;
        break;
    default:
        if (status) *status = (-EBADMSG);
        return NULL;
    }

    GASSERT(!(flags & GBINDER_TX_FLAG_ONEWAY));
    return handler(self, req, status);
}

static
void
gbinder_local_object_handle_later(
    GBinderLocalObject* self,
    GSourceFunc function)
{
    if (G_LIKELY(self)) {
        GBinderLocalObjectPriv* priv = self->priv;

        g_main_context_invoke_full(priv->context, G_PRIORITY_DEFAULT, function,
            gbinder_local_object_ref(self), g_object_unref);
    }
}

static
gboolean
gbinder_local_object_handle_increfs_proc(
    gpointer local)
{
    GBinderLocalObject* self = GBINDER_LOCAL_OBJECT(local);

    self->weak_refs++;
    g_signal_emit(self, gbinder_local_object_signals
        [SIGNAL_WEAK_REFS_CHANGED], 0);
    return G_SOURCE_REMOVE;
}

static
gboolean
gbinder_local_object_handle_decrefs_proc(
    gpointer local)
{
    GBinderLocalObject* self = GBINDER_LOCAL_OBJECT(local);

    GASSERT(self->weak_refs > 0);
    self->weak_refs--;
    g_signal_emit(self, gbinder_local_object_signals
        [SIGNAL_WEAK_REFS_CHANGED], 0);
    return G_SOURCE_REMOVE;
}

static
gboolean
gbinder_local_object_handle_acquire_proc(
    gpointer local)
{
    GBinderLocalObject* self = GBINDER_LOCAL_OBJECT(local);

    self->strong_refs++;
    g_signal_emit(self, gbinder_local_object_signals
        [SIGNAL_STRONG_REFS_CHANGED], 0);
    return G_SOURCE_REMOVE;
}

static
gboolean
gbinder_local_object_handle_release_proc(
    gpointer local)
{
    GBinderLocalObject* self = GBINDER_LOCAL_OBJECT(local);

    GASSERT(self->strong_refs > 0);
    self->strong_refs--;
    g_signal_emit(self, gbinder_local_object_signals
        [SIGNAL_STRONG_REFS_CHANGED], 0);
    gbinder_local_object_unref(self);
    return G_SOURCE_REMOVE;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderLocalObject*
gbinder_local_object_new(
    GBinderIpc* ipc,
    const char* const* ifaces,
    GBinderLocalTransactFunc txproc,
    void* user_data) /* Since 1.0.30 */
{
    if (G_LIKELY(ipc)) {
        GBinderLocalObject* self = g_object_new
            (GBINDER_TYPE_LOCAL_OBJECT, NULL);

        gbinder_local_object_init_base(self, ipc, ifaces, txproc, user_data);
        gbinder_ipc_register_local_object(ipc, self);
        return self;
    }
    return NULL;
}

void
gbinder_local_object_init_base(
    GBinderLocalObject* self,
    GBinderIpc* ipc,
    const char* const* ifaces,
    GBinderLocalTransactFunc txproc,
    void* user_data)
{
    GBinderLocalObjectPriv* priv = self->priv;
    guint i = 0, n = gutil_strv_length((char**)ifaces);
    gboolean append_base_interface;

    if (g_strcmp0(gutil_strv_last((char**)ifaces), hidl_base_interface)) {
        append_base_interface = TRUE;
        n++;
    } else {
        append_base_interface = FALSE;
    }

    priv->ifaces = g_new(char*, n + 1);
    if (ifaces) {
        while (*ifaces) {
            priv->ifaces[i++] = g_strdup(*ifaces++);
        }
    }
    if (append_base_interface) {
        priv->ifaces[i++] = g_strdup(hidl_base_interface);
    }
    priv->ifaces[i] = NULL;

    self->ipc = gbinder_ipc_ref(ipc);
    self->ifaces = (const char**)priv->ifaces;
    priv->txproc = txproc;
    priv->user_data = user_data;
}

GBinderLocalObject*
gbinder_local_object_ref(
    GBinderLocalObject* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(GBINDER_LOCAL_OBJECT(self));
        return self;
    } else {
        return NULL;
    }
}

void
gbinder_local_object_unref(
    GBinderLocalObject* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(GBINDER_LOCAL_OBJECT(self));
    }
}

void
gbinder_local_object_drop(
    GBinderLocalObject* self)
{
    if (G_LIKELY(self)) {
        GBinderLocalObjectPriv* priv = self->priv;

        /* Clear the transaction callback */
        priv->txproc = NULL;
        priv->user_data = NULL;
        g_object_unref(GBINDER_LOCAL_OBJECT(self));
    }
}

GBinderLocalReply*
gbinder_local_object_new_reply(
    GBinderLocalObject* self)
{
    if (G_LIKELY(self)) {
        return gbinder_local_reply_new(gbinder_local_object_io(self));
    }
    return NULL;
}

gulong
gbinder_local_object_add_weak_refs_changed_handler(
    GBinderLocalObject* self,
    GBinderLocalObjectFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_WEAK_REFS_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
gbinder_local_object_add_strong_refs_changed_handler(
    GBinderLocalObject* self,
    GBinderLocalObjectFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_STRONG_REFS_CHANGED_NAME, G_CALLBACK(func), user_data) : 0;
}

void
gbinder_local_object_remove_handler(
    GBinderLocalObject* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

GBINDER_LOCAL_TRANSACTION_SUPPORT
gbinder_local_object_can_handle_transaction(
    GBinderLocalObject* self,
    const char* iface,
    guint code)
{
    return G_LIKELY(self) ?
        GBINDER_LOCAL_OBJECT_GET_CLASS(self)->can_handle_transaction
            (self, iface, code) : GBINDER_LOCAL_TRANSACTION_NOT_SUPPORTED;
}

GBinderLocalReply*
gbinder_local_object_handle_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
{
    if (G_LIKELY(self)) {
        return GBINDER_LOCAL_OBJECT_GET_CLASS(self)->handle_transaction
            (self, req, code, flags, status);
    } else {
        if (status) *status = (-EBADMSG);
        return NULL;
    }
}

GBinderLocalReply*
gbinder_local_object_handle_looper_transaction(
    GBinderLocalObject* self,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
{
    if (G_LIKELY(self)) {
        return GBINDER_LOCAL_OBJECT_GET_CLASS(self)->handle_looper_transaction
            (self, req, code, flags, status);
    } else {
        if (status) *status = -EBADMSG;
        return NULL;
    }
}

void
gbinder_local_object_handle_increfs(
    GBinderLocalObject* self)
{
    gbinder_local_object_handle_later(self,
        gbinder_local_object_handle_increfs_proc);
}

void
gbinder_local_object_handle_decrefs(
    GBinderLocalObject* self)
{
    gbinder_local_object_handle_later(self,
        gbinder_local_object_handle_decrefs_proc);
}

void
gbinder_local_object_handle_acquire(
    GBinderLocalObject* self)
{
    gbinder_local_object_ref(self);
    gbinder_local_object_handle_later(self,
        gbinder_local_object_handle_acquire_proc);
}

void
gbinder_local_object_handle_release(
    GBinderLocalObject* self)
{
    gbinder_local_object_handle_later(self,
        gbinder_local_object_handle_release_proc);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
gbinder_local_object_init(
    GBinderLocalObject* self)
{
    GBinderLocalObjectPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        GBINDER_TYPE_LOCAL_OBJECT, GBinderLocalObjectPriv);

    priv->context = g_main_context_default();
    self->priv = priv;
}

static
void
gbinder_local_object_dispose(
    GObject* local)
{
    GBinderLocalObject* self = GBINDER_LOCAL_OBJECT(local);

    gbinder_ipc_local_object_disposed(self->ipc, self);
    G_OBJECT_CLASS(gbinder_local_object_parent_class)->dispose(local);
}

static
void
gbinder_local_object_finalize(
    GObject* local)
{
    GBinderLocalObject* self = GBINDER_LOCAL_OBJECT(local);
    GBinderLocalObjectPriv* priv = self->priv;

    GASSERT(!self->strong_refs);
    gbinder_ipc_unref(self->ipc);
    g_strfreev(priv->ifaces);
    G_OBJECT_CLASS(gbinder_local_object_parent_class)->finalize(local);
}

static
void
gbinder_local_object_class_init(
    GBinderLocalObjectClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = gbinder_local_object_dispose;
    object_class->finalize = gbinder_local_object_finalize;

    g_type_class_add_private(klass, sizeof(GBinderLocalObjectPriv));
    klass->handle_transaction =
        gbinder_local_object_default_handle_transaction;
    klass->handle_looper_transaction =
        gbinder_local_object_default_handle_looper_transaction;
    klass->can_handle_transaction =
        gbinder_local_object_default_can_handle_transaction;

    gbinder_local_object_signals[SIGNAL_WEAK_REFS_CHANGED] =
        g_signal_new(SIGNAL_WEAK_REFS_CHANGED_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST, 0,
            NULL, NULL, NULL, G_TYPE_NONE, 0);
    gbinder_local_object_signals[SIGNAL_STRONG_REFS_CHANGED] =
        g_signal_new(SIGNAL_STRONG_REFS_CHANGED_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST, 0,
            NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
