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

#ifndef GBINDER_IPC_H
#define GBINDER_IPC_H

#include "gbinder_types_p.h"

#include <glib-object.h>

typedef struct gbinder_ipc_priv GBinderIpcPriv;
struct gbinder_ipc {
    GObject object;
    GBinderIpcPriv* priv;
    GBinderDriver* driver;
    const char* dev;
};

typedef struct gbinder_ipc_tx GBinderIpcTx;

typedef
void
(*GBinderIpcTxFunc)(
    const GBinderIpcTx* tx);

struct gbinder_ipc_tx {
    gulong id;
    gboolean cancelled;
    GBinderIpc* ipc;
    void* user_data;
};

typedef
gboolean
(*GBinderIpcLocalObjectCheckFunc)(
    GBinderLocalObject* obj,
    void* user_data);

typedef
void
(*GBinderIpcReplyFunc)(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data);

typedef
GBinderRemoteReply*
(*GBinderIpcSyncReplyFunc)(
    GBinderIpc* ipc,
    guint32 handle,
    guint32 code,
    GBinderLocalRequest* req,
    int* status);

typedef
int
(*GBinderIpcSyncOnewayFunc)(
    GBinderIpc* ipc,
    guint32 handle,
    guint32 code,
    GBinderLocalRequest* req);

struct gbinder_ipc_sync_api {
    GBinderIpcSyncReplyFunc sync_reply;
    GBinderIpcSyncOnewayFunc sync_oneway;
};

extern const GBinderIpcSyncApi gbinder_ipc_sync_main GBINDER_INTERNAL;
extern const GBinderIpcSyncApi gbinder_ipc_sync_worker GBINDER_INTERNAL;

GBinderIpc*
gbinder_ipc_new(
    const char* dev)
    GBINDER_INTERNAL;

GBinderIpc*
gbinder_ipc_ref(
    GBinderIpc* ipc)
    GBINDER_INTERNAL;

void
gbinder_ipc_unref(
    GBinderIpc* ipc)
    GBINDER_INTERNAL;

void
gbinder_ipc_looper_check(
    GBinderIpc* ipc)
    GBINDER_INTERNAL;

GBinderObjectRegistry*
gbinder_ipc_object_registry(
    GBinderIpc* ipc)
    GBINDER_INTERNAL;

const GBinderIo*
gbinder_ipc_io(
    GBinderIpc* ipc)
    GBINDER_INTERNAL;

const GBinderRpcProtocol*
gbinder_ipc_protocol(
    GBinderIpc* ipc)
    GBINDER_INTERNAL;

GBinderLocalObject*
gbinder_ipc_find_local_object(
    GBinderIpc* ipc,
    GBinderIpcLocalObjectCheckFunc func,
    void* user_data)
    GBINDER_INTERNAL
    G_GNUC_WARN_UNUSED_RESULT;

void
gbinder_ipc_register_local_object(
    GBinderIpc* ipc,
    GBinderLocalObject* obj)
    GBINDER_INTERNAL;

GBinderRemoteObject*
gbinder_ipc_get_service_manager(
    GBinderIpc* self)
    GBINDER_INTERNAL
    G_GNUC_WARN_UNUSED_RESULT;

void
gbinder_ipc_invalidate_remote_handle(
    GBinderIpc* ipc,
    guint32 handle)
    GBINDER_INTERNAL;

int
gbinder_ipc_ping_sync(
    GBinderIpc* ipc,
    guint32 handle,
    const GBinderIpcSyncApi* api)
    GBINDER_INTERNAL;

gulong
gbinder_ipc_transact(
    GBinderIpc* ipc,
    guint32 handle,
    guint32 code,
    guint32 flags, /* GBINDER_TX_FLAG_xxx */
    GBinderLocalRequest* req,
    GBinderIpcReplyFunc func,
    GDestroyNotify destroy,
    void* user_data)
    GBINDER_INTERNAL;

gulong
gbinder_ipc_transact_custom(
    GBinderIpc* ipc,
    GBinderIpcTxFunc exec,
    GBinderIpcTxFunc done,
    GDestroyNotify destroy,
    void* user_data)
    GBINDER_INTERNAL;

void
gbinder_ipc_cancel(
    GBinderIpc* ipc,
    gulong id)
    GBINDER_INTERNAL;

/* Internal for GBinderLocalObject */
void
gbinder_ipc_local_object_disposed(
    GBinderIpc* self,
    GBinderLocalObject* obj)
    GBINDER_INTERNAL;

/* Internal for GBinderRemoteObject */
void
gbinder_ipc_remote_object_disposed(
    GBinderIpc* self,
    GBinderRemoteObject* obj)
    GBINDER_INTERNAL;

/* Needed by unit tests */
gboolean
gbinder_ipc_set_max_threads(
    GBinderIpc* self,
    gint max_threads)
    GBINDER_INTERNAL;

/* Declared for unit tests */
void
gbinder_ipc_exit(
    void)
    GBINDER_INTERNAL
    GBINDER_DESTRUCTOR;

#endif /* GBINDER_IPC_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
