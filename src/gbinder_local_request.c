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

#include "gbinder_local_request_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_output_data.h"
#include "gbinder_writer_p.h"
#include "gbinder_buffer_p.h"
#include "gbinder_io.h"
#include "gbinder_log.h"

#include <gutil_intarray.h>
#include <gutil_macros.h>

struct gbinder_local_request {
    gint refcount;
    GBinderWriterData data;
    GBinderOutputData out;
};

GBINDER_INLINE_FUNC
GBinderLocalRequest*
gbinder_local_request_output_cast(
    GBinderOutputData* out)
{
    return G_CAST(out, GBinderLocalRequest, out);
}

static
GUtilIntArray*
gbinder_local_request_output_offsets(
    GBinderOutputData* out)
{
    return gbinder_local_request_output_cast(out)->data.offsets;
}

static
gsize
gbinder_local_request_output_buffers_size(
    GBinderOutputData* out)
{
    return gbinder_local_request_output_cast(out)->data.buffers_size;
}

GBinderLocalRequest*
gbinder_local_request_new(
    const GBinderIo* io,
    GBytes* init)
{
    GASSERT(io);
    if (io) {
        GBinderLocalRequest* self = g_slice_new0(GBinderLocalRequest);
        GBinderWriterData* writer = &self->data;
        GBinderOutputData* out = &self->out;

        static const GBinderOutputDataFunctions local_request_output_fn = {
            .offsets = gbinder_local_request_output_offsets,
            .buffers_size = gbinder_local_request_output_buffers_size
        };

        g_atomic_int_set(&self->refcount, 1);
        writer->io = io;
        if (init) {
            gsize size;
            gconstpointer data = g_bytes_get_data(init, &size);

            writer->bytes = g_byte_array_sized_new(size);
            g_byte_array_append(writer->bytes, data, size);
        } else {
            writer->bytes = g_byte_array_new();
        }
        out->f = &local_request_output_fn;
        out->bytes = writer->bytes;
        return self;
    }
    return NULL;
}

GBinderLocalRequest*
gbinder_local_request_new_iface(
    const GBinderIo* io,
    const GBinderRpcProtocol* protocol,
    const char* iface)
{
    GBinderLocalRequest* self = gbinder_local_request_new(io, NULL);

    if (self && G_LIKELY(protocol) && G_LIKELY(iface)) {
        GBinderWriter writer;

        gbinder_local_request_init_writer(self, &writer);
        protocol->write_rpc_header(&writer, iface);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_new_from_data(
    GBinderBuffer* buffer,
    GBinderObjectConverter* convert)
{
    GBinderLocalRequest* self = gbinder_local_request_new
        (gbinder_buffer_io(buffer), NULL);

    if (self) {
        gbinder_writer_data_append_contents(&self->data, buffer, 0, convert);
    }
    return self;
}

void
gbinder_local_request_append_contents(
    GBinderLocalRequest* self,
    GBinderBuffer* buffer,
    gsize off,
    GBinderObjectConverter* convert)
{
    if (self) {
        gbinder_writer_data_append_contents(&self->data, buffer, off, convert);
    }
}

static
void
gbinder_local_request_free(
    GBinderLocalRequest* self)
{
    GBinderWriterData* data = &self->data;

    g_byte_array_free(data->bytes, TRUE);
    gutil_int_array_free(data->offsets, TRUE);
    gbinder_cleanup_free(data->cleanup);
    g_slice_free(GBinderLocalRequest, self);
}

GBinderLocalRequest*
gbinder_local_request_ref(
    GBinderLocalRequest* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
gbinder_local_request_unref(
    GBinderLocalRequest* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            gbinder_local_request_free(self);
        }
    }
}

GBinderOutputData*
gbinder_local_request_data(
    GBinderLocalRequest* self)
{
    return G_LIKELY(self) ? &self->out :  NULL;
}

void
gbinder_local_request_cleanup(
    GBinderLocalRequest* self,
    GDestroyNotify destroy,
    gpointer pointer)
{
    if (G_LIKELY(self)) {
        GBinderWriterData* data = &self->data;

        data->cleanup = gbinder_cleanup_add(data->cleanup, destroy, pointer);
    } else if (destroy) {
        destroy(pointer);
    }
}

void
gbinder_local_request_init_writer(
    GBinderLocalRequest* self,
    GBinderWriter* writer)
{
    if (G_LIKELY(writer)) {
        gbinder_writer_init(writer, G_LIKELY(self) ? &self->data : NULL);
    }
}

GBinderLocalRequest*
gbinder_local_request_append_bool(
    GBinderLocalRequest* self,
    gboolean value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_bool(&self->data, value);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_int32(
    GBinderLocalRequest* self,
    guint32 value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_int32(&self->data, value);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_int64(
    GBinderLocalRequest* self,
    guint64 value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_int64(&self->data, value);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_float(
    GBinderLocalRequest* self,
    gfloat value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_float(&self->data, value);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_double(
    GBinderLocalRequest* self,
    gdouble value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_double(&self->data, value);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_string8(
    GBinderLocalRequest* self,
    const char* str)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_string8(&self->data, str);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_string16(
    GBinderLocalRequest* self,
    const char* utf8)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_string16(&self->data, utf8);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_hidl_string(
    GBinderLocalRequest* self,
    const char* str)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_hidl_string(&self->data, str);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_hidl_string_vec(
    GBinderLocalRequest* self,
    const char* strv[],
    gssize count)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_hidl_string_vec(&self->data, strv, count);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_local_object(
    GBinderLocalRequest* self,
    GBinderLocalObject* obj)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_local_object(&self->data, obj);
    }
    return self;
}

GBinderLocalRequest*
gbinder_local_request_append_remote_object(
    GBinderLocalRequest* self,
    GBinderRemoteObject* obj)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_remote_object(&self->data, obj);
    }
    return self;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
