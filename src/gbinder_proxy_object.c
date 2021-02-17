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
#include "gbinder_remote_reply_p.h"
#include "gbinder_object_converter.h"
#include "gbinder_object_registry.h"
#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_log.h"

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
    gulong remote_death_id;
    gboolean dropped;
    GBinderProxyTx* tx;
    GMutex mutex; /* Protects the hashtable below */
    GHashTable* subproxies;
};

G_DEFINE_TYPE(GBinderProxyObject, gbinder_proxy_object, \
    GBINDER_TYPE_LOCAL_OBJECT)
#define GBINDER_IS_PROXY_OBJECT(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, \
    GBINDER_TYPE_PROXY_OBJECT)

#define THIS(obj) GBINDER_PROXY_OBJECT(obj)
#define THIS_TYPE GBINDER_TYPE_PROXY_OBJECT
#define PARENT_CLASS gbinder_proxy_object_parent_class

static
void
gbinder_proxy_object_subproxy_gone(
    gpointer proxy,
    GObject* subproxy)
{
    GBinderProxyObject* self = THIS(proxy);
    GBinderProxyObjectPriv* priv = self->priv;

    /* Lock */
    g_mutex_lock(&priv->mutex);
    g_hash_table_remove(priv->subproxies, subproxy);
    if (g_hash_table_size(priv->subproxies) == 0) {
        g_hash_table_unref(priv->subproxies);
        priv->subproxies = NULL;
    }
    g_mutex_unlock(&priv->mutex);
    /* Unlock */
}

static
void
gbinder_proxy_object_drop_subproxies(
    GBinderProxyObject* self)
{
    GBinderProxyObjectPriv* priv = self->priv;
    GSList* list = NULL;

    /* Lock */
    g_mutex_lock(&priv->mutex);
    if (priv->subproxies) {
        GHashTableIter it;
        gpointer value;

        g_hash_table_iter_init(&it, priv->subproxies);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            list = g_slist_append(list, gbinder_local_object_ref(value));
            g_object_weak_unref(G_OBJECT(value),
                gbinder_proxy_object_subproxy_gone, self);
        }
        g_hash_table_destroy(priv->subproxies);
        priv->subproxies = NULL;
    }
    g_mutex_unlock(&priv->mutex);
    /* Unlock */

    /* Drop (and possibly destroy) the objects outside of the lock */
    g_slist_free_full(list, (GDestroyNotify) gbinder_local_object_drop);
}

static
void
gbinder_proxy_remote_death_proc(
    GBinderRemoteObject* obj,
    void* proxy)
{
    GBinderProxyObject* self = THIS(proxy);
    GBinderProxyObjectPriv* priv = self->priv;

    GDEBUG("Remote object %u died on %s", obj->handle, obj->ipc->dev);
    gbinder_remote_object_remove_handler(obj, priv->remote_death_id);
    priv->remote_death_id = 0;
    /* Drop the implicit reference */
    gbinder_local_object_unref(&self->parent);
}

/*==========================================================================*
 * Converter
 *==========================================================================*/

typedef struct gbinder_proxy_object_converter {
    GBinderObjectConverter pub;
    GBinderProxyObject* proxy;
    GBinderIpc* remote;
    GBinderIpc* local;
} GBinderProxyObjectConverter;

GBINDER_INLINE_FUNC
GBinderProxyObjectConverter*
gbinder_proxy_object_converter_cast(
    GBinderObjectConverter* pub)
{
    return G_CAST(pub, GBinderProxyObjectConverter, pub);
}

static
gboolean
gbinder_proxy_object_converter_check(
    GBinderLocalObject* obj,
    void* remote)
{
    if (GBINDER_IS_PROXY_OBJECT(obj) && THIS(obj)->remote == remote) {
        /* Found matching proxy object */
        return TRUE;
    }
    /* Keep looking */
    return FALSE;
}

static
GBinderLocalObject*
gbinder_proxy_object_converter_handle_to_local(
    GBinderObjectConverter* pub,
    guint32 handle)
{
    GBinderProxyObjectConverter* c = gbinder_proxy_object_converter_cast(pub);
    GBinderProxyObject* proxy = c->proxy;
    GBinderProxyObjectPriv* priv = proxy->priv;
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(c->remote);
    GBinderRemoteObject* remote = gbinder_object_registry_get_remote(reg,
        handle, REMOTE_REGISTRY_CAN_CREATE /* but don't acquire */);
    GBinderLocalObject* local = gbinder_ipc_find_local_object(c->local,
        gbinder_proxy_object_converter_check, remote);

    if (!local && !remote->dead) {
        /* GBinderProxyObject will reference GBinderRemoteObject */
        GBinderProxyObject* subp = gbinder_proxy_object_new(c->local, remote);

        /*
         * Auto-created proxies may get spontaneously destroyed and
         * not necessarily on the UI thread.
         */
        subp->priv->remote_death_id = gbinder_remote_object_add_death_handler
            (remote, gbinder_proxy_remote_death_proc, subp);

        /*
         * Remote keeps an implicit reference to this auto-created
         * proxy. The reference gets released when the remote object
         * dies, i.e. by gbinder_proxy_remote_death_proc().
         */
        gbinder_local_object_ref(local = GBINDER_LOCAL_OBJECT(subp));

        /* Lock */
        g_mutex_lock(&priv->mutex);
        if (!priv->subproxies) {
            priv->subproxies = g_hash_table_new(g_direct_hash, g_direct_equal);
        }
        g_hash_table_insert(priv->subproxies, subp, subp);
        g_object_weak_ref(G_OBJECT(subp),
            gbinder_proxy_object_subproxy_gone, proxy);
        g_mutex_unlock(&priv->mutex);
        /* Unlock */
    }

    /* Release the reference returned by gbinder_object_registry_get_remote */
    gbinder_remote_object_unref(remote);
    return local;
}

static
void
gbinder_proxy_object_converter_init(
    GBinderProxyObjectConverter* convert,
    GBinderProxyObject* proxy,
    GBinderIpc* remote,
    GBinderIpc* local)
{
    static const GBinderObjectConverterFunctions gbinder_converter_fn = {
        .handle_to_local = gbinder_proxy_object_converter_handle_to_local
    };

    GBinderObjectConverter* pub = &convert->pub;
    GBinderIpc* dest = proxy->parent.ipc;

    memset(convert, 0, sizeof(*convert));
    convert->proxy = proxy;
    convert->remote = remote;
    convert->local = local;
    pub->f = &gbinder_converter_fn;
    pub->io = gbinder_ipc_io(dest);
    pub->protocol = gbinder_ipc_protocol(dest);
}

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
    GBinderProxyObject* self = tx->proxy;
    GBinderProxyObjectConverter convert;
    GBinderLocalReply* fwd;

    /*
     * For proxy objects auto-created by the reply, the remote side (the
     * one sent the reply) will be the remote GBinderIpc and this object's
     * GBinderIpc will be the local, i.e. those proxies will work in the
     * same direction as the top level object. The direction gets inverted
     * twice.
     */
    gbinder_proxy_object_converter_init(&convert, self, ipc, self->parent.ipc);
    fwd = gbinder_remote_reply_convert_to_local(reply, &convert.pub);
    tx->id = 0;
    gbinder_proxy_tx_dequeue(tx);
    gbinder_remote_request_complete(tx->req, fwd,
        (status > 0) ? (-EFAULT) : status);
    if (status == GBINDER_STATUS_DEAD_OBJECT) {
        /*
         * Some kernels sometimes don't bother sending us death notifications.
         * Let's also interpret BR_DEAD_REPLY as an obituary to make sure that
         * we don't keep dead remote objects around.
         */
        gbinder_remote_object_commit_suicide(self->remote);
    }
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
    GBinderProxyObject* self = THIS(object);
    GBinderProxyObjectPriv* priv = self->priv;
    GBinderRemoteObject* remote = self->remote;

    if (!priv->dropped && !gbinder_remote_object_is_dead(remote)) {
        GBinderLocalRequest* fwd;
        GBinderProxyTx* tx = g_slice_new0(GBinderProxyTx);
        GBinderProxyObjectConverter convert;

        g_object_ref(tx->proxy = self);
        tx->req = gbinder_remote_request_ref(req);
        tx->next = priv->tx;
        priv->tx = tx;

        /* Mark the incoming request as pending */
        gbinder_remote_request_block(req);

        /*
         * For auto-created proxy objects, this object's GBinderIpc will
         * become a remote, and the remote's GBinderIpc will become local
         * because they work in the opposite direction.
         */
        gbinder_proxy_object_converter_init(&convert, self, object->ipc,
            remote->ipc);

        /* Forward the transaction */
        fwd = gbinder_remote_request_convert_to_local(req, &convert.pub);
        tx->id = gbinder_ipc_transact(remote->ipc, remote->handle, code, flags,
            fwd, gbinder_proxy_tx_reply, gbinder_proxy_tx_destroy, tx);
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
gbinder_proxy_object_acquire(
    GBinderLocalObject* object)
{
    GBinderProxyObject* self = THIS(object);
    GBinderProxyObjectPriv* priv = self->priv;

    if (priv->remote_death_id && !object->strong_refs) {
        GBinderRemoteObject* remote = self->remote;

        /* First strong ref, acquire the attached remote object */
        gbinder_driver_acquire(remote->ipc->driver, remote->handle);
    }
    GBINDER_LOCAL_OBJECT_CLASS(PARENT_CLASS)->acquire(object);
}

static
void
gbinder_proxy_object_release(
    GBinderLocalObject* object)
{
    GBinderProxyObject* self = THIS(object);
    GBinderProxyObjectPriv* priv = self->priv;

    if (priv->remote_death_id && object->strong_refs == 1) {
        GBinderRemoteObject* remote = self->remote;

        /* Last strong ref, release the attached remote object */
        gbinder_driver_release(remote->ipc->driver, remote->handle);
    }
    GBINDER_LOCAL_OBJECT_CLASS(PARENT_CLASS)->release(object);
}

static
void
gbinder_proxy_object_drop(
    GBinderLocalObject* object)
{
    GBinderProxyObject* self = THIS(object);
    GBinderProxyObjectPriv* priv = self->priv;

    priv->dropped = TRUE;
    gbinder_proxy_object_drop_subproxies(self);
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
            (THIS_TYPE, src, NULL, NULL, NULL);

        if (object) {
            GBinderProxyObject* self = THIS(object);

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
    GBinderProxyObject* self = THIS(object);
    GBinderProxyObjectPriv* priv = self->priv;

    gbinder_proxy_object_drop_subproxies(self);
    gbinder_remote_object_remove_handler(self->remote, priv->remote_death_id);
    gbinder_remote_object_unref(self->remote);
    g_mutex_clear(&priv->mutex);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
gbinder_proxy_object_init(
    GBinderProxyObject* self)
{
    GBinderProxyObjectPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        THIS_TYPE, GBinderProxyObjectPriv);

    self->priv = priv;
    g_mutex_init(&priv->mutex);
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
    klass->acquire = gbinder_proxy_object_acquire;
    klass->release = gbinder_proxy_object_release;
    klass->drop = gbinder_proxy_object_drop;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
