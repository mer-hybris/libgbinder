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

#include "gbinder_remote_request_p.h"
#include "gbinder_reader_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_local_request_p.h"
#include "gbinder_object_converter.h"
#include "gbinder_object_registry.h"
#include "gbinder_buffer_p.h"
#include "gbinder_driver.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

#include <errno.h>

typedef struct gbinder_remote_request_priv {
    GBinderRemoteRequest pub;
    gint refcount;
    pid_t pid;
    uid_t euid;
    const GBinderRpcProtocol* protocol;
    const char* iface;
    char* iface2;
    gsize header_size;
    GBinderReaderData data;
} GBinderRemoteRequestPriv;

GBINDER_INLINE_FUNC GBinderRemoteRequestPriv*
gbinder_remote_request_cast(GBinderRemoteRequest* pub)
    { return G_LIKELY(pub) ? G_CAST(pub,GBinderRemoteRequestPriv,pub) : NULL; }

GBinderRemoteRequest*
gbinder_remote_request_new(
    GBinderObjectRegistry* reg,
    const GBinderRpcProtocol* protocol,
    pid_t pid,
    uid_t euid)
{
    GBinderRemoteRequestPriv* self = g_slice_new0(GBinderRemoteRequestPriv);
    GBinderReaderData* data = &self->data;

    g_atomic_int_set(&self->refcount, 1);
    self->pid = pid;
    self->euid = euid;
    self->protocol = protocol;
    data->reg = gbinder_object_registry_ref(reg);
    return &self->pub;
}

GBinderLocalRequest*
gbinder_remote_request_copy_to_local(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GBinderReaderData* d = &self->data;

        return gbinder_local_request_new_from_data(d->buffer, NULL);
    }
    return NULL;
}

GBinderLocalRequest*
gbinder_remote_request_convert_to_local(
    GBinderRemoteRequest* req,
    GBinderObjectConverter* convert)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GBinderReaderData* data = &self->data;

        if (!convert || convert->protocol == self->protocol) {
            /* The same protocol, the same format of RPC header */
            return gbinder_local_request_new_from_data(data->buffer, convert);
        } else {
            /* Need to translate to another format */
            GBinderLocalRequest* local = gbinder_local_request_new_iface
                (convert->io, convert->protocol, self->iface);

            gbinder_local_request_append_contents(local, data->buffer,
                self->header_size, convert);
            return local;
        }
    }
    return NULL;
}

static
void
gbinder_remote_request_free(
    GBinderRemoteRequestPriv* self)
{
    GBinderReaderData* data = &self->data;
    GBinderRemoteRequest* req = &self->pub;

    GASSERT(!req->tx);
    if (req->tx) {
        GWARN("Request is dropped without completing the transaction");
        gbinder_remote_request_complete(req, NULL, -ECANCELED);
    }
    gbinder_object_registry_unref(data->reg);
    gbinder_buffer_free(data->buffer);
    g_free(self->iface2);
    g_slice_free(GBinderRemoteRequestPriv, self);
}

static
inline
void
gbinder_remote_request_init_reader2(
    GBinderRemoteRequestPriv* self,
    GBinderReader* p)
{
    /* The caller has already checked the request for NULL */
    GBinderReaderData* data = &self->data;
    GBinderBuffer* buffer = data->buffer;

    if (buffer) {
        gbinder_reader_init(p, data, self->header_size,
            buffer->size - self->header_size);
    } else {
        gbinder_reader_init(p, data, 0, 0);
    }
}

void
gbinder_remote_request_set_data(
    GBinderRemoteRequest* req,
    guint32 txcode,
    GBinderBuffer* buffer)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GBinderReaderData* data = &self->data;
        GBinderReader reader;

        g_free(self->iface2);
        gbinder_buffer_free(data->buffer);
        data->buffer = buffer;
        data->objects = gbinder_buffer_objects(buffer);

        /* Parse RPC header */
        gbinder_remote_request_init_reader2(self, &reader);
        self->iface = self->protocol->read_rpc_header(&reader, txcode,
            &self->iface2);
        if (self->iface) {
            self->header_size = gbinder_reader_bytes_read(&reader);
        } else {
            /* No RPC header */
            self->header_size = 0;
        }
    } else {
        gbinder_buffer_free(buffer);
    }
}

const char*
gbinder_remote_request_interface(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    return G_LIKELY(self) ? self->iface : NULL;
}

GBinderRemoteRequest*
gbinder_remote_request_ref(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return req;
}

void
gbinder_remote_request_unref(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            gbinder_remote_request_free(self);
        }
    }
}

void
gbinder_remote_request_init_reader(
    GBinderRemoteRequest* req,
    GBinderReader* reader)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        gbinder_remote_request_init_reader2(self, reader);
    } else {
        gbinder_reader_init(reader, NULL, 0, 0);
    }
}

pid_t
gbinder_remote_request_sender_pid(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    return G_LIKELY(self) ? self->pid : (uid_t)(-1);
}

uid_t
gbinder_remote_request_sender_euid(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    return G_LIKELY(self) ? self->euid : (uid_t)(-1);
}

gboolean
gbinder_remote_request_read_int32(
    GBinderRemoteRequest* self,
    gint32* value)
{
    return gbinder_remote_request_read_uint32(self, (guint32*)value);
}

gboolean
gbinder_remote_request_read_uint32(
    GBinderRemoteRequest* req,
    guint32* value)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_uint32(&reader, value);
    }
    return FALSE;
}

gboolean
gbinder_remote_request_read_int64(
    GBinderRemoteRequest* self,
    gint64* value)
{
    return gbinder_remote_request_read_uint64(self, (guint64*)value);
}

gboolean
gbinder_remote_request_read_uint64(
    GBinderRemoteRequest* req,
    guint64* value)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_uint64(&reader, value);
    }
    return FALSE;
}

const char*
gbinder_remote_request_read_string8(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_string8(&reader);
    }
    return NULL;
}

char*
gbinder_remote_request_read_string16(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_string16(&reader);
    }
    return NULL;
}

GBinderRemoteObject*
gbinder_remote_request_read_object(
    GBinderRemoteRequest* req)
{
    GBinderRemoteRequestPriv* self = gbinder_remote_request_cast(req);

    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_object(&reader);
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
