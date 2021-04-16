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

#include "gbinder_client_p.h"
#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_output_data.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_local_request_p.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

#include <stdlib.h>
#include <errno.h>
#include <limits.h>

typedef struct gbinder_client_iface_range {
    char* iface;
    GBytes* rpc_header;
    GBinderLocalRequest* basic_req;
    guint32 last_code;
} GBinderClientIfaceRange;

typedef struct gbinder_client_priv {
    GBinderClient pub;
    guint32 refcount;
    GBinderClientIfaceRange* ranges;
    guint nr;
} GBinderClientPriv;

typedef struct gbinder_client_tx {
    GBinderClient* client;
    GBinderClientReplyFunc reply;
    GDestroyNotify destroy;
    void* user_data;
} GBinderClientTx;

static inline GBinderClientPriv* gbinder_client_cast(GBinderClient* client)
    { return G_CAST(client, GBinderClientPriv, pub); }

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
const GBinderClientIfaceRange*
gbinder_client_find_range(
    GBinderClientPriv* priv,
    guint32 code)
{
    guint i;

    for (i = 0; i < priv->nr; i++) {
        const GBinderClientIfaceRange* r = priv->ranges + i;

        if (r->last_code >= code) {
            return r;
        }
    }
    return NULL;
}

/*
 * Generates basic request (without additional parameters) for the
 * specified interface and pulls header data out of it. The basic
 * request can be reused for those transactions which have no
 * additional parameters. The header data are needed for building
 * non-trivial requests.
 */
static
void
gbinder_client_init_range(
    GBinderClientIfaceRange* r,
    GBinderDriver* driver,
    const GBinderClientIfaceInfo* info)
{
    GBinderOutputData* hdr;

    r->basic_req = gbinder_driver_local_request_new(driver, info->iface);
    hdr = gbinder_local_request_data(r->basic_req);
    r->rpc_header = g_bytes_new(hdr->bytes->data, hdr->bytes->len);
    r->iface = g_strdup(info->iface);
    r->last_code = info->last_code;
}

static
int
gbinder_client_sort_ranges(
    const void* p1,
    const void* p2)
{
    const GBinderClientIfaceRange* r1 = p1;
    const GBinderClientIfaceRange* r2 = p2;

    return (r1->last_code < r2->last_code) ? (-1) :
        (r1->last_code > r2->last_code) ? 1 : 0;
}

static
void
gbinder_client_free(
    GBinderClientPriv* priv)
{
    GBinderClient* self = &priv->pub;
    guint i;

    for (i = 0; i < priv->nr; i++) {
        GBinderClientIfaceRange* r = priv->ranges + i;

        gbinder_local_request_unref(r->basic_req);
        g_free(r->iface);
        if (r->rpc_header) {
            g_bytes_unref(r->rpc_header);
        }
    }
    g_free(priv->ranges);
    gbinder_remote_object_unref(self->remote);
    g_slice_free(GBinderClientPriv, priv);
}

static
void
gbinder_client_transact_reply(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* data)
{
    GBinderClientTx* tx = data;

    if (tx->reply) {
        tx->reply(tx->client, reply, status, tx->user_data);
    }
}

static
void
gbinder_client_transact_destroy(
    gpointer data)
{
    GBinderClientTx* tx = data;

    if (tx->destroy) {
        tx->destroy(tx->user_data);
    }
    gbinder_client_unref(tx->client);
    g_slice_free(GBinderClientTx, tx);
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

GBinderRemoteReply*
gbinder_client_transact_sync_reply2(
    GBinderClient* self,
    guint32 code,
    GBinderLocalRequest* req,
    int* status,
    const GBinderIpcSyncApi* api)
{
    if (G_LIKELY(self)) {
        GBinderRemoteObject* obj = self->remote;

        if (G_LIKELY(!obj->dead)) {
            if (!req) {
                const GBinderClientIfaceRange* r = gbinder_client_find_range
                    (gbinder_client_cast(self), code);

                /* Default empty request (just the header, no parameters) */
                if (r) {
                    req = r->basic_req;
                }
            }
            if (req) {
                return api->sync_reply(obj->ipc, obj->handle, code, req,
                    status);
            } else {
                GWARN("Unable to build empty request for tx code %u", code);
            }
        } else {
            GDEBUG("Refusing to perform transaction with a dead object");
        }
    }
    return NULL;
}

int
gbinder_client_transact_sync_oneway2(
    GBinderClient* self,
    guint32 code,
    GBinderLocalRequest* req,
    const GBinderIpcSyncApi* api)
{
    if (G_LIKELY(self)) {
        GBinderRemoteObject* obj = self->remote;

        if (G_LIKELY(!obj->dead)) {
            if (!req) {
                const GBinderClientIfaceRange* r = gbinder_client_find_range
                    (gbinder_client_cast(self), code);

                /* Default empty request (just the header, no parameters) */
                if (r) {
                    req = r->basic_req;
                }
            }
            if (req) {
                return api->sync_oneway(obj->ipc, obj->handle, code, req);
            } else {
                GWARN("Unable to build empty request for tx code %u", code);
            }
        } else {
            GDEBUG("Refusing to perform transaction with a dead object");
            return (-ESTALE);
        }
    }
    return (-EINVAL);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderClient*
gbinder_client_new2(
    GBinderRemoteObject* remote,
    const GBinderClientIfaceInfo* ifaces,
    gsize count)
{
    if (G_LIKELY(remote)) {
        GBinderClientPriv* priv = g_slice_new0(GBinderClientPriv);
        GBinderClient* self = &priv->pub;
        GBinderDriver* driver = remote->ipc->driver;

        g_atomic_int_set(&priv->refcount, 1);
        self->remote = gbinder_remote_object_ref(remote);
        if (count > 0) {
            gsize i;

            priv->nr = count;
            priv->ranges = g_new(GBinderClientIfaceRange, priv->nr);
            for (i = 0; i < count; i++) {
                gbinder_client_init_range(priv->ranges + i, driver, ifaces + i);
            }
            qsort(priv->ranges, count, sizeof(GBinderClientIfaceRange),
                gbinder_client_sort_ranges);
        } else {
            /* No interface info */
            priv->nr = 1;
            priv->ranges = g_new0(GBinderClientIfaceRange, 1);
            priv->ranges[0].last_code = UINT_MAX;
            priv->ranges[0].basic_req = gbinder_local_request_new
                (gbinder_driver_io(driver), NULL);
        }
        return self;
    }
    return NULL;
}

GBinderClient*
gbinder_client_new(
    GBinderRemoteObject* remote,
    const char* iface)
{
    GBinderClientIfaceInfo info;

    info.iface = iface;
    info.last_code = UINT_MAX;
    return gbinder_client_new2(remote, &info, 1);
}

GBinderClient*
gbinder_client_ref(
    GBinderClient* self)
{
    if (G_LIKELY(self)) {
        GBinderClientPriv* priv = gbinder_client_cast(self);

        GASSERT(priv->refcount > 0);
        g_atomic_int_inc(&priv->refcount);
    }
    return self;
}

void
gbinder_client_unref(
    GBinderClient* self)
{
    if (G_LIKELY(self)) {
        GBinderClientPriv* priv = gbinder_client_cast(self);

        GASSERT(priv->refcount > 0);
        if (g_atomic_int_dec_and_test(&priv->refcount)) {
            gbinder_client_free(priv);
        }
    }
}

const char*
gbinder_client_interface(
    GBinderClient* self) /* since 1.0.22 */
{
    return G_LIKELY(self) ? gbinder_client_cast(self)->ranges->iface : NULL;
}

const char*
gbinder_client_interface2(
    GBinderClient* self,
    guint32 code) /* since 1.0.42 */
{
    if (G_LIKELY(self)) {
        const GBinderClientIfaceRange* r =
            gbinder_client_find_range(gbinder_client_cast(self), code);

        if (r) {
            return r->iface;
        }
    }
    return NULL;
}

GBinderLocalRequest*
gbinder_client_new_request(
    GBinderClient* self)
{
    if (G_LIKELY(self)) {
        GBinderClientPriv* priv = gbinder_client_cast(self);
        const GBinderIo* io = gbinder_driver_io(self->remote->ipc->driver);

        return gbinder_local_request_new(io, priv->ranges->rpc_header);
    }
    return NULL;
}

GBinderLocalRequest*
gbinder_client_new_request2(
    GBinderClient* self,
    guint32 code) /* since 1.0.42 */
{
    if (G_LIKELY(self)) {
        GBinderClientPriv* priv = gbinder_client_cast(self);
        const GBinderClientIfaceRange* r = gbinder_client_find_range
            (priv, code);

        if (r) {
            const GBinderIo* io = gbinder_driver_io(self->remote->ipc->driver);

            return gbinder_local_request_new(io, r->rpc_header);
        }
    }
    return NULL;
}

GBinderRemoteReply*
gbinder_client_transact_sync_reply(
    GBinderClient* self,
    guint32 code,
    GBinderLocalRequest* req,
    int* status)
{
    return gbinder_client_transact_sync_reply2(self, code, req, status,
        &gbinder_ipc_sync_main);
}

int
gbinder_client_transact_sync_oneway(
    GBinderClient* self,
    guint32 code,
    GBinderLocalRequest* req)
{
    return gbinder_client_transact_sync_oneway2(self, code, req,
        &gbinder_ipc_sync_main);
}

gulong
gbinder_client_transact(
    GBinderClient* self,
    guint32 code,
    guint32 flags,
    GBinderLocalRequest* req,
    GBinderClientReplyFunc reply,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(self)) {
        GBinderRemoteObject* obj = self->remote;

        if (G_LIKELY(!obj->dead)) {
            if (!req) {
                const GBinderClientIfaceRange* r = gbinder_client_find_range
                    (gbinder_client_cast(self), code);

                /* Default empty request (just the header, no parameters) */
                if (r) {
                    req = r->basic_req;
                }
            }
            if (req) {
                GBinderClientTx* tx = g_slice_new0(GBinderClientTx);

                tx->client = gbinder_client_ref(self);
                tx->reply = reply;
                tx->destroy = destroy;
                tx->user_data = user_data;
                return gbinder_ipc_transact(obj->ipc, obj->handle, code,
                    flags, req, gbinder_client_transact_reply,
                    gbinder_client_transact_destroy, tx);
            } else {
                GWARN("Unable to build empty request for tx code %u", code);
            }
        } else {
            GDEBUG("Refusing to perform transaction with a dead object");
        }
    }
    return 0;
}

void
gbinder_client_cancel(
    GBinderClient* self,
    gulong id)
{
    if (G_LIKELY(self)) {
        gbinder_ipc_cancel(gbinder_client_ipc(self), id);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
