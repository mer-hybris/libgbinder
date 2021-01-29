/*
 * Copyright (C) 2021 Jolla Ltd.
 * Copyright (C) 2021 Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_proxy_object.h"
#include "gbinder_local_object_p.h"
#include "gbinder_local_request.h"
#include "gbinder_local_reply.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_remote_request_p.h"
#include "gbinder_remote_reply.h"
#include "gbinder_ipc.h"

#include <gutil_macros.h>

#include <errno.h>

typedef GBinderLocalObjectClass GBinderProxyObjectClass;
typedef struct gbinder_proxy_tx GBinderProxyTx;

struct gbinder_proxy_tx {
    GBinderProxyTx* next;
    GBinderRemoteRequest* req;
    GBinderProxyObject* proxy;
    gulong id;
};

struct gbinder_proxy_object_priv {
    gboolean dropped;
    GBinderProxyTx* tx;
};

GType gbinder_proxy_object_get_type(void) GBINDER_INTERNAL;

G_DEFINE_TYPE(GBinderProxyObject, gbinder_proxy_object, \
    GBINDER_TYPE_LOCAL_OBJECT)
#define PARENT_CLASS gbinder_proxy_object_parent_class

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
gbinder_proxy_tx_dequeue(
    GBinderProxyTx* tx)
{
    GBinderProxyObject* proxy = tx->proxy;

    if (proxy) {
        GBinderProxyObjectPriv* priv = proxy->priv;

        if (priv->tx) {
            if (priv->tx == tx) {
                priv->tx = tx->next;
            } else {
                GBinderProxyTx* prev = priv->tx;

                while (prev->next) {
                    if (prev->next == tx) {
                        prev->next = tx->next;
                        break;
                    }
                    prev = prev->next;
                }
            }
        }
        tx->next = NULL;
        tx->proxy = NULL;
        g_object_unref(proxy);
    }
}

static
void
gbinder_proxy_tx_reply(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    GBinderProxyTx* tx = user_data;
    GBinderLocalReply* fwd = gbinder_remote_reply_copy_to_local(reply);

    tx->id = 0;
    gbinder_proxy_tx_dequeue(tx);
    gbinder_remote_request_complete(tx->req, fwd, status);
    gbinder_local_reply_unref(fwd);
}

static
void
gbinder_proxy_tx_destroy(
    gpointer user_data)
{
    GBinderProxyTx* tx = user_data;

    gbinder_proxy_tx_dequeue(tx);
    gbinder_remote_request_unref(tx->req);
    gutil_slice_free(tx);
}

static
GBinderLocalReply*
gbinder_proxy_object_handle_transaction(
    GBinderLocalObject* object,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
{
    GBinderProxyObject* self = GBINDER_PROXY_OBJECT(object);
    GBinderProxyObjectPriv* priv = self->priv;
    GBinderRemoteObject* remote = self->remote;

    if (!priv->dropped && !gbinder_remote_object_is_dead(remote)) {
        GBinderIpc* remote_ipc = remote->ipc;
        GBinderDriver* remote_driver = remote_ipc->driver;
        GBinderLocalRequest* fwd =
            gbinder_remote_request_translate_to_local(req, remote_driver);
        GBinderProxyTx* tx = g_slice_new0(GBinderProxyTx);

        g_object_ref(tx->proxy = self);
        tx->req = gbinder_remote_request_ref(req);
        tx->next = priv->tx;
        priv->tx = tx;

        /* Mark the incoming request as pending */
        gbinder_remote_request_block(req);

        /* Forward the transaction */
        tx->id = gbinder_ipc_transact(remote_ipc, remote->handle, code, flags,
            fwd , gbinder_proxy_tx_reply, gbinder_proxy_tx_destroy, tx);
        gbinder_local_request_unref(fwd);
        *status = GBINDER_STATUS_OK;
    } else {
        *status = (-EBADMSG);
    }
    return NULL;
}

static
GBINDER_LOCAL_TRANSACTION_SUPPORT
gbinder_proxy_object_can_handle_transaction(
    GBinderLocalObject* self,
    const char* iface,
    guint code)
{
    /* Process all transactions on the main thread */
    return GBINDER_LOCAL_TRANSACTION_SUPPORTED;
}

static
void
gbinder_proxy_object_drop(
    GBinderLocalObject* object)
{
    GBinderProxyObject* self = GBINDER_PROXY_OBJECT(object);
    GBinderProxyObjectPriv* priv = self->priv;

    priv->dropped = TRUE;
    GBINDER_LOCAL_OBJECT_CLASS(PARENT_CLASS)->drop(object);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderProxyObject*
gbinder_proxy_object_new(
    GBinderIpc* src,
    GBinderRemoteObject* remote)
{
    if (G_LIKELY(remote)) {
        /*
         * We don't need to specify the interface list because all
         * transactions (including HIDL_GET_DESCRIPTOR_TRANSACTION
         * and HIDL_DESCRIPTOR_CHAIN_TRANSACTION) are getting forwared
         * to the remote object.
         */
        GBinderLocalObject* object = gbinder_local_object_new_with_type
            (GBINDER_TYPE_PROXY_OBJECT, src, NULL, NULL, NULL);

        if (object) {
            GBinderProxyObject* self = GBINDER_PROXY_OBJECT(object);

            self->remote = gbinder_remote_object_ref(remote);
            return self;
        }
    }
    return NULL;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
gbinder_proxy_object_finalize(
    GObject* object)
{
    GBinderProxyObject* self = GBINDER_PROXY_OBJECT(object);

    gbinder_remote_object_unref(self->remote);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
gbinder_proxy_object_init(
    GBinderProxyObject* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, GBINDER_TYPE_PROXY_OBJECT,
        GBinderProxyObjectPriv);
}

static
void
gbinder_proxy_object_class_init(
    GBinderProxyObjectClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GBinderProxyObjectPriv));
    object_class->finalize = gbinder_proxy_object_finalize;
    klass->can_handle_transaction = gbinder_proxy_object_can_handle_transaction;
    klass->handle_transaction = gbinder_proxy_object_handle_transaction;
    klass->drop = gbinder_proxy_object_drop;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
