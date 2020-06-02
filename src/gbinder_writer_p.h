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

#ifndef GBINDER_WRITER_PRIVATE_H
#define GBINDER_WRITER_PRIVATE_H

#include <gbinder_writer.h>

#include "gbinder_cleanup.h"

typedef struct gbinder_writer_data {
    const GBinderIo* io;
    GByteArray* bytes;
    GUtilIntArray* offsets;
    gsize buffers_size;
    GBinderCleanup* cleanup;
} GBinderWriterData;

void
gbinder_writer_init(
    GBinderWriter* writer,
    GBinderWriterData* data)
    GBINDER_INTERNAL;

void
gbinder_writer_data_set_contents(
    GBinderWriterData* data,
    GBinderBuffer* buffer)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_bool(
    GBinderWriterData* data,
    gboolean value)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_int32(
    GBinderWriterData* data,
    guint32 value)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_int64(
    GBinderWriterData* data,
    guint64 value)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_float(
    GBinderWriterData* data,
    gfloat value)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_double(
    GBinderWriterData* data,
    gdouble value)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_string8(
    GBinderWriterData* data,
    const char* str)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_string8_len(
    GBinderWriterData* data,
    const char* str,
    gsize len)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_string16(
    GBinderWriterData* data,
    const char* utf8)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_string16_len(
    GBinderWriterData* data,
    const char* utf8,
    gssize num_bytes)
    GBINDER_INTERNAL;

guint
gbinder_writer_data_append_buffer_object(
    GBinderWriterData* data,
    const void* ptr,
    gsize size,
    const GBinderParent* parent)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_hidl_vec(
    GBinderWriterData* data,
    const void* base,
    guint count,
    guint elemsize)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_hidl_string(
    GBinderWriterData* data,
    const char* str)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_hidl_string_vec(
    GBinderWriterData* data,
    const char* strv[],
    gssize count)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_local_object(
    GBinderWriterData* data,
    GBinderLocalObject* obj)
    GBINDER_INTERNAL;

void
gbinder_writer_data_append_remote_object(
    GBinderWriterData* data,
    GBinderRemoteObject* obj)
    GBINDER_INTERNAL;

#endif /* GBINDER_WRITER_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
