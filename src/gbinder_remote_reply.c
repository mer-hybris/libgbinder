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

#include "gbinder_remote_reply_p.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_reader_p.h"
#include "gbinder_object_registry.h"
#include "gbinder_buffer_p.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

struct gbinder_remote_reply {
    gint refcount;
    GBinderReaderData data;
};

GBinderRemoteReply*
gbinder_remote_reply_new(
    GBinderObjectRegistry* reg)
{
    GBinderRemoteReply* self = g_slice_new0(GBinderRemoteReply);
    GBinderReaderData* data = &self->data;

    g_atomic_int_set(&self->refcount, 1);
    data->reg = gbinder_object_registry_ref(reg);
    return self;
}

static
void
gbinder_remote_reply_free(
    GBinderRemoteReply* self)
{
    GBinderReaderData* data = &self->data;

    gbinder_object_registry_unref(data->reg);
    gbinder_buffer_free(data->buffer);
    g_slice_free(GBinderRemoteReply, self);
}

void
gbinder_remote_reply_set_data(
    GBinderRemoteReply* self,
    GBinderBuffer* buffer)
{
    if (G_LIKELY(self)) {
        GBinderReaderData* data = &self->data;

        gbinder_buffer_free(data->buffer);
        data->buffer = buffer;
        data->objects = gbinder_buffer_objects(buffer);
    } else {
        gbinder_buffer_free(buffer);
    }
}

GBinderRemoteReply*
gbinder_remote_reply_ref(
    GBinderRemoteReply* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
gbinder_remote_reply_unref(
    GBinderRemoteReply* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            gbinder_remote_reply_free(self);
        }
    }
}

gboolean
gbinder_remote_reply_is_empty(
    GBinderRemoteReply* self)
{
    return !self || !self->data.buffer || !self->data.buffer->size;
}

GBinderLocalReply*
gbinder_remote_reply_copy_to_local(
    GBinderRemoteReply* self)
{
    return gbinder_remote_reply_convert_to_local(self, NULL);
}

GBinderLocalReply*
gbinder_remote_reply_convert_to_local(
    GBinderRemoteReply* self,
    GBinderObjectConverter* convert)
{
    if (G_LIKELY(self)) {
        GBinderReaderData* d = &self->data;
        GBinderObjectRegistry* reg = d->reg;

        if (reg) {
            return gbinder_local_reply_set_contents
                (gbinder_local_reply_new(reg->io), d->buffer, convert);
        }
    }
    return NULL;
}

static
inline
void
gbinder_remote_reply_init_reader2(
    GBinderRemoteReply* self,
    GBinderReader* p)
{
    /* The caller has already checked the reply for NULL */
    GBinderReaderData* data = &self->data;
    GBinderBuffer* buffer = data->buffer;

    if (buffer) {
        gbinder_reader_init(p, data, 0, buffer->size);
    } else {
        gbinder_reader_init(p, data, 0, 0);
    }
}

void
gbinder_remote_reply_init_reader(
    GBinderRemoteReply* self,
    GBinderReader* reader)
{
    if (G_LIKELY(self)) {
        gbinder_remote_reply_init_reader2(self, reader);
    } else {
        gbinder_reader_init(reader, NULL, 0, 0);
    }
}

gboolean
gbinder_remote_reply_read_int32(
    GBinderRemoteReply* self,
    gint32* value)
{
    return gbinder_remote_reply_read_uint32(self, (guint32*)value);
}

gboolean
gbinder_remote_reply_read_uint32(
    GBinderRemoteReply* self,
    guint32* value)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_reply_init_reader2(self, &reader);
        return gbinder_reader_read_uint32(&reader, value);
    }
    return FALSE;
}

gboolean
gbinder_remote_reply_read_int64(
    GBinderRemoteReply* self,
    gint64* value)
{
    return gbinder_remote_reply_read_uint64(self, (guint64*)value);
}

gboolean
gbinder_remote_reply_read_uint64(
    GBinderRemoteReply* self,
    guint64* value)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_reply_init_reader2(self, &reader);
        return gbinder_reader_read_uint64(&reader, value);
    }
    return FALSE;
}

const char*
gbinder_remote_reply_read_string8(
    GBinderRemoteReply* self)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_reply_init_reader2(self, &reader);
        return gbinder_reader_read_string8(&reader);
    }
    return NULL;
}

char*
gbinder_remote_reply_read_string16(
    GBinderRemoteReply* self)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_reply_init_reader2(self, &reader);
        return gbinder_reader_read_string16(&reader);
    }
    return NULL;
}

GBinderRemoteObject*
gbinder_remote_reply_read_object(
    GBinderRemoteReply* self)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_reply_init_reader2(self, &reader);
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
