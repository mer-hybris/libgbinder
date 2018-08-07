/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
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

#ifndef GBINDER_WRITER_H
#define GBINDER_WRITER_H

#include <gbinder_types.h>

G_BEGIN_DECLS

/*
 * Writers are initialized by LocalRequest and LocalReply objects.
 * Note that writers directly update the objects which initialized
 * them but don't reference those object. The caller must make sure
 * that objects outlive their writers.
 *
 * Writers are normally allocated on stack.
 */

struct gbinder_writer {
    gconstpointer d[4];
};

struct gbinder_parent {
    guint32 index;
    guint32 offset;
};

void
gbinder_writer_append_int32(
    GBinderWriter* writer,
    guint32 value);

void
gbinder_writer_append_int64(
    GBinderWriter* writer,
    guint64 value);

void
gbinder_writer_append_float(
    GBinderWriter* writer,
    gfloat value);

void
gbinder_writer_append_double(
    GBinderWriter* writer,
    gdouble value);

void
gbinder_writer_append_string16(
    GBinderWriter* writer,
    const char* utf8);

void
gbinder_writer_append_string16_len(
    GBinderWriter* writer,
    const char* utf8,
    gssize num_bytes);

void
gbinder_writer_append_string8(
    GBinderWriter* writer,
    const char* str);

void
gbinder_writer_append_string8_len(
    GBinderWriter* writer,
    const char* str,
    gsize len);

void
gbinder_writer_append_bool(
    GBinderWriter* writer,
    gboolean value);

void
gbinder_writer_append_bytes(
    GBinderWriter* writer,
    const void* data,
    gsize size);

guint
gbinder_writer_append_buffer_object_with_parent(
    GBinderWriter* writer,
    const void* buf,
    gsize len,
    const GBinderParent* parent);

guint
gbinder_writer_append_buffer_object(
    GBinderWriter* writer,
    const void* buf,
    gsize len);

void
gbinder_writer_append_hidl_string(
    GBinderWriter* writer,
    const char* str);

void
gbinder_writer_append_hidl_string_vec(
    GBinderWriter* writer,
    const char* data[],
    gssize count);

void
gbinder_writer_append_local_object(
    GBinderWriter* writer,
    GBinderLocalObject* obj);

void
gbinder_writer_append_remote_object(
    GBinderWriter* writer,
    GBinderRemoteObject* obj);

G_END_DECLS

#endif /* GBINDER_WRITER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
