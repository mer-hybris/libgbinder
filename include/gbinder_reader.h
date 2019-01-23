/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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

#ifndef GBINDER_READER_H
#define GBINDER_READER_H

#include "gbinder_types.h"

G_BEGIN_DECLS

/*
 * Normally, the reader is initialized by gbinder_remote_reply_init_reader or
 * gbinder_remote_request_init_reader function. Note that the reader doesn't
 * copy the data nor does it reference the object which initialized it. The
 * caller must make sure that the data outlive the reader.
 *
 * Also, these functions are not NULL tolerant. The reader is normally
 * allocated on stack.
 */

struct gbinder_reader {
    gconstpointer d[6];
};

gboolean
gbinder_reader_at_end(
    const GBinderReader* reader);

gboolean
gbinder_reader_read_byte(
    GBinderReader* reader,
    guchar* value);

gboolean
gbinder_reader_read_bool(
    GBinderReader* reader,
    gboolean* value);

gboolean
gbinder_reader_read_int32(
    GBinderReader* reader,
    gint32* value);

gboolean
gbinder_reader_read_uint32(
    GBinderReader* reader,
    guint32* value);

gboolean
gbinder_reader_read_int64(
    GBinderReader* reader,
    gint64* value);

gboolean
gbinder_reader_read_uint64(
    GBinderReader* reader,
    guint64* value);

gboolean
gbinder_reader_read_float(
    GBinderReader* reader,
    gfloat* value);

gboolean
gbinder_reader_read_double(
    GBinderReader* reader,
    gdouble* value);

int
gbinder_reader_read_fd(
    GBinderReader* reader); /* Since 1.0.18 */

int
gbinder_reader_read_dup_fd(
    GBinderReader* reader) /* Since 1.0.18 */
    G_GNUC_WARN_UNUSED_RESULT;

gboolean
gbinder_reader_read_nullable_object(
    GBinderReader* reader,
    GBinderRemoteObject** obj);

GBinderRemoteObject*
gbinder_reader_read_object(
    GBinderReader* reader)
    G_GNUC_WARN_UNUSED_RESULT;

#define gbinder_reader_skip_object(reader) \
    gbinder_reader_read_nullable_object(reader, NULL)

GBinderBuffer*
gbinder_reader_read_buffer(
    GBinderReader* reader)
    G_GNUC_WARN_UNUSED_RESULT;

const void*
gbinder_reader_read_hidl_struct1(
    GBinderReader* reader,
    gsize size); /* Since 1.0.9 */

#define gbinder_reader_read_hidl_struct(reader,type) \
    ((const type*)gbinder_reader_read_hidl_struct1(reader, sizeof(type)))

const void*
gbinder_reader_read_hidl_vec(
    GBinderReader* reader,
    gsize* count,
    gsize* elemsize);

const void*
gbinder_reader_read_hidl_vec1(
    GBinderReader* reader,
    gsize* count,
    guint expected_elemsize); /* Since 1.0.9 */

#define gbinder_reader_read_hidl_type_vec(reader,type,count) \
    ((const type*)gbinder_reader_read_hidl_vec1(reader, count, sizeof(type)))
#define gbinder_reader_read_hidl_byte_vec(reader,count) /* vec<uint8_t> */ \
    gbinder_reader_read_hidl_type_vec(reader,guint8,count)

char*
gbinder_reader_read_hidl_string(
    GBinderReader* reader)
    G_GNUC_WARN_UNUSED_RESULT;

const char*
gbinder_reader_read_hidl_string_c(
    GBinderReader* reader); /* Since 1.0.23 */

#define gbinder_reader_skip_hidl_string(reader) \
    (gbinder_reader_read_hidl_string_c(reader) != NULL)

char**
gbinder_reader_read_hidl_string_vec(
    GBinderReader* reader);

gboolean
gbinder_reader_skip_buffer(
    GBinderReader* reader);

const char*
gbinder_reader_read_string8(
    GBinderReader* reader);

char*
gbinder_reader_read_string16(
    GBinderReader* reader)
    G_GNUC_WARN_UNUSED_RESULT;

gboolean
gbinder_reader_read_nullable_string16(
    GBinderReader* reader,
    char** out);

gboolean
gbinder_reader_read_nullable_string16_utf16(
    GBinderReader* reader,
    const gunichar2** out,
    gsize* len); /* Since 1.0.17 */

const gunichar2*
gbinder_reader_read_string16_utf16(
    GBinderReader* reader,
    gsize* len); /* Since 1.0.26 */

gboolean
gbinder_reader_skip_string16(
    GBinderReader* reader);

const void*
gbinder_reader_read_byte_array(
    GBinderReader* reader,
    gsize* len); /* Since 1.0.12 */

gsize
gbinder_reader_bytes_read(
    const GBinderReader* reader);

gsize
gbinder_reader_bytes_remaining(
    const GBinderReader* reader);

void
gbinder_reader_copy(
    GBinderReader* dest,
    const GBinderReader* src); /* Since 1.0.16 */

G_END_DECLS

#endif /* GBINDER_READER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
