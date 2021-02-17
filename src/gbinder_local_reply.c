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

#include "gbinder_local_reply_p.h"
#include "gbinder_output_data.h"
#include "gbinder_writer_p.h"
#include "gbinder_buffer_p.h"
#include "gbinder_log.h"

#include <gutil_intarray.h>
#include <gutil_macros.h>

struct gbinder_local_reply {
    gint refcount;
    GBinderWriterData data;
    GBinderOutputData out;
    GBinderBufferContents* contents;
};

GBINDER_INLINE_FUNC
GBinderLocalReply*
gbinder_local_reply_output_cast(
    GBinderOutputData* out)
{
    return G_CAST(out, GBinderLocalReply, out);
}

static
GUtilIntArray*
gbinder_local_reply_output_offsets(
    GBinderOutputData* out)
{
    return gbinder_local_reply_output_cast(out)->data.offsets;
}

static
gsize
gbinder_local_reply_output_buffers_size(
    GBinderOutputData* out)
{
    return gbinder_local_reply_output_cast(out)->data.buffers_size;
}

GBinderLocalReply*
gbinder_local_reply_new(
    const GBinderIo* io)
{
    GASSERT(io);
    if (io) {
        GBinderLocalReply* self = g_slice_new0(GBinderLocalReply);
        GBinderWriterData* data = &self->data;
        GBinderOutputData* out = &self->out;

        static const GBinderOutputDataFunctions local_reply_output_fn = {
            .offsets = gbinder_local_reply_output_offsets,
            .buffers_size = gbinder_local_reply_output_buffers_size
        };

        g_atomic_int_set(&self->refcount, 1);
        data->io = io;
        out->bytes = data->bytes = g_byte_array_new();
        out->f = &local_reply_output_fn;
        return self;
    }
    return NULL;
}

GBinderLocalReply*
gbinder_local_reply_set_contents(
    GBinderLocalReply* self,
    GBinderBuffer* buffer,
    GBinderObjectConverter* convert)
{
    if (self) {
        gbinder_writer_data_set_contents(&self->data, buffer, convert);
        gbinder_buffer_contents_unref(self->contents);
        self->contents = gbinder_buffer_contents_ref
            (gbinder_buffer_contents(buffer));
    }
    return self;
}

static
void
gbinder_local_reply_free(
    GBinderLocalReply* self)
{
    GBinderWriterData* data = &self->data;

    gutil_int_array_free(data->offsets, TRUE);
    g_byte_array_free(data->bytes, TRUE);
    gbinder_cleanup_free(data->cleanup);
    gbinder_buffer_contents_unref(self->contents);
    gutil_slice_free(self);
}

GBinderLocalReply*
gbinder_local_reply_ref(
    GBinderLocalReply* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
gbinder_local_reply_unref(
    GBinderLocalReply* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            gbinder_local_reply_free(self);
        }
    }
}

GBinderOutputData*
gbinder_local_reply_data(
    GBinderLocalReply* self)
{
    return G_LIKELY(self) ? &self->out :  NULL;
}

GBinderBufferContents*
gbinder_local_reply_contents(
    GBinderLocalReply* self)
{
    return G_LIKELY(self) ? self->contents :  NULL;
}

void
gbinder_local_reply_cleanup(
    GBinderLocalReply* self,
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
gbinder_local_reply_init_writer(
    GBinderLocalReply* self,
    GBinderWriter* writer)
{
    if (G_LIKELY(writer)) {
        gbinder_writer_init(writer, G_LIKELY(self) ? &self->data : NULL);
    }
}

GBinderLocalReply*
gbinder_local_reply_append_bool(
    GBinderLocalReply* self,
    gboolean value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_bool(&self->data, value);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_int32(
    GBinderLocalReply* self,
    guint32 value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_int32(&self->data, value);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_int64(
    GBinderLocalReply* self,
    guint64 value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_int64(&self->data, value);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_float(
    GBinderLocalReply* self,
    gfloat value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_float(&self->data, value);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_double(
    GBinderLocalReply* self,
    gdouble value)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_double(&self->data, value);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_string8(
    GBinderLocalReply* self,
    const char* str)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_string8(&self->data, str);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_string16(
    GBinderLocalReply* self,
    const char* utf8)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_string16(&self->data, utf8);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_hidl_string(
    GBinderLocalReply* self,
    const char* str)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_hidl_string(&self->data, str);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_hidl_string_vec(
    GBinderLocalReply* self,
    const char* strv[],
    gssize count)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_hidl_string_vec(&self->data, strv, count);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_local_object(
    GBinderLocalReply* self,
    GBinderLocalObject* obj)
{
    if (G_LIKELY(self)) {
        gbinder_writer_data_append_local_object(&self->data, obj);
    }
    return self;
}

GBinderLocalReply*
gbinder_local_reply_append_remote_object(
    GBinderLocalReply* self,
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
