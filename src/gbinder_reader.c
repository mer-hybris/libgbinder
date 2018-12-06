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

#include "gbinder_reader_p.h"
#include "gbinder_buffer_p.h"
#include "gbinder_io.h"
#include "gbinder_object_registry.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

typedef struct gbinder_reader_priv {
    const guint8* start;
    const guint8* end;
    const guint8* ptr;
    const GBinderReaderData* data;
    void** objects;
} GBinderReaderPriv;

G_STATIC_ASSERT(sizeof(GBinderReader) >= sizeof(GBinderReaderPriv));

static inline GBinderReaderPriv* gbinder_reader_cast(GBinderReader* reader)
    { return (GBinderReaderPriv*)reader; }
static inline const GBinderReaderPriv* gbinder_reader_cast_c
    (const GBinderReader* reader)  { return (GBinderReaderPriv*)reader; }

void
gbinder_reader_init(
    GBinderReader* reader,
    GBinderReaderData* data,
    gsize offset,
    gsize len)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    p->data = data;
    if (G_LIKELY(data)) {
        GBinderBuffer* buffer = data->buffer;

        if (buffer) {
            /* We are assuming that the caller has checked offset and size */
            GASSERT(!buffer || (offset + len <= buffer->size));
            p->ptr = p->start = (guint8*)buffer->data + offset;
            p->end = p->ptr + len;
        } else {
            p->ptr = p->start = p->end = NULL;
        }
        p->objects = data->objects;
    } else {
        p->ptr = p->start = p->end = NULL;
        p->objects = NULL;
    }
}

gboolean
gbinder_reader_at_end(
    const GBinderReader* reader)
{
    const GBinderReaderPriv* p = gbinder_reader_cast_c(reader);

    return p->ptr >= p->end;
}

static
inline
gboolean
gbinder_reader_can_read(
    GBinderReaderPriv* p,
    gsize len)
{
    return (p->end - p->ptr) >= len;
}

gboolean
gbinder_reader_read_byte(
    GBinderReader* reader,
    guchar* value)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (p->ptr < p->end) {
        if (value) *value = *p->ptr;
        p->ptr++;
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
gbinder_reader_read_bool(
    GBinderReader* reader,
    gboolean* value)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    /* Boolean values are supposed to be padded to 4-byte boundary */
    if (gbinder_reader_can_read(p, 4)) {
        if (value) {
            *value = (p->ptr[0] != 0);
        }
        p->ptr += 4;
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
gbinder_reader_read_int32(
    GBinderReader* reader,
    gint32* value)
{
    return gbinder_reader_read_uint32(reader, (guint32*)value);
}

gboolean
gbinder_reader_read_uint32(
    GBinderReader* reader,
    guint32* value)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (gbinder_reader_can_read(p, sizeof(*value))) {
        if (value) {
            const gint32* ptr = (void*)p->ptr;

            *value = *ptr;
        }
        p->ptr += sizeof(*value);
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
gbinder_reader_read_int64(
    GBinderReader* reader,
    gint64* value)
{
    return gbinder_reader_read_uint64(reader, (guint64*)value);
}

gboolean
gbinder_reader_read_uint64(
    GBinderReader* reader,
    guint64* value)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (gbinder_reader_can_read(p, sizeof(*value))) {
        if (value) {
            const gint64* ptr = (void*)p->ptr;

            *value = *ptr;
        }
        p->ptr += sizeof(*value);
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
gbinder_reader_read_float(
    GBinderReader* reader,
    gfloat* value)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (gbinder_reader_can_read(p, sizeof(*value))) {
        if (value) {
            const gfloat* ptr = (void*)p->ptr;

            *value = *ptr;
        }
        p->ptr += sizeof(*value);
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
gbinder_reader_read_double(
    GBinderReader* reader,
    gdouble* value)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (gbinder_reader_can_read(p, sizeof(*value))) {
        if (value) {
            const gdouble* ptr = (void*)p->ptr;

            *value = *ptr;
        }
        p->ptr += sizeof(*value);
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
gbinder_reader_read_nullable_object(
    GBinderReader* reader,
    GBinderRemoteObject** out)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);
    const GBinderReaderData* data = p->data;

    if (data && data->reg && p->objects && p->objects[0] &&
        p->ptr == p->objects[0]) {
        const guint eaten = data->reg->io->decode_binder_object(p->ptr,
            gbinder_reader_bytes_remaining(reader), data->reg, out);

        if (eaten) {
            p->ptr += eaten;
            p->objects++;
            return TRUE;
        }
    }
    if (out) *out = NULL;
    return FALSE;
}

GBinderRemoteObject*
gbinder_reader_read_object(
    GBinderReader* reader)
{
    GBinderRemoteObject* obj = NULL;

    gbinder_reader_read_nullable_object(reader, &obj);
    return obj;
}

static
gboolean
gbinder_reader_read_buffer_impl(
    GBinderReader* reader,
    GBinderBuffer** out)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);
    const GBinderReaderData* data = p->data;

    if (data && data->reg && p->objects && p->objects[0] &&
        p->ptr == p->objects[0]) {
        GBinderBuffer* buf = data->buffer;
        const GBinderIo* io = data->reg->io;
        const gsize offset = p->ptr - (guint8*)buf->data;
        const guint eaten = io->decode_buffer_object(buf, offset, out);

        if (eaten) {
            p->ptr += eaten;
            p->objects++;
            return TRUE;
        }
    }
    if (out) *out = NULL;
    return FALSE;
}

GBinderBuffer*
gbinder_reader_read_buffer(
    GBinderReader* reader)
{
    GBinderBuffer* buf = NULL;

    gbinder_reader_read_buffer_impl(reader, &buf);
    return buf;
}

gboolean
gbinder_reader_skip_buffer(
    GBinderReader* reader)
{
    return gbinder_reader_read_buffer_impl(reader, NULL);
}

/* Helper for gbinder_reader_read_hidl_struct() macro */
const void*
gbinder_reader_read_hidl_struct1(
    GBinderReader* reader,
    gsize size) /* since 1.0.9 */
{
    const void* result = NULL;
    GBinderBuffer* buf = gbinder_reader_read_buffer(reader);

    /* Check the size */
    if (buf && buf->size == size) {
        result = buf->data;
    }
    gbinder_buffer_free(buf);
    return result;
}

/* Doesn't copy the data */
const void*
gbinder_reader_read_hidl_vec(
    GBinderReader* reader,
    gsize* count,
    gsize* elemsize)
{
    GBinderBuffer* buf = gbinder_reader_read_buffer(reader);
    gsize out_count = 0, out_elemsize = 0;
    const void* out = NULL;

    if (buf && buf->size == sizeof(GBinderHidlVec)) {
        const GBinderHidlVec* vec = buf->data;
        const void* next = vec->data.ptr;

        if (next) {
            GBinderBuffer* vbuf = gbinder_reader_read_buffer(reader);

            if (vbuf && vbuf->data == next && ((!vec->count && !vbuf->size) ||
                (vec->count && vbuf->size && !(vbuf->size % vec->count)))) {
                out_elemsize = vec->count ? (vbuf->size / vec->count) : 0;
                out_count = vec->count;
                out = vbuf->data;
            }
            gbinder_buffer_free(vbuf);
        } else if (!vec->count) {
            /* Any non-NULL pointer just to indicate success */
            out = vec;
        }
    }
    gbinder_buffer_free(buf);
    if (elemsize) {
        *elemsize = out_elemsize;
    }
    if (count) {
        *count = out_count;
    }
    return out;
}

/* Helper for gbinder_reader_read_hidl_struct_vec() macro */
const void*
gbinder_reader_read_hidl_vec1(
    GBinderReader* reader,
    gsize* count,
    guint expected_elem_size) /* since 1.0.9 */
{
    gsize actual;
    const void* data = gbinder_reader_read_hidl_vec(reader, count, &actual);

    /* Actual size will be zero for an empty array */
    return (data && (actual == expected_elem_size || !actual)) ? data : NULL;
}

char*
gbinder_reader_read_hidl_string(
    GBinderReader* reader)
{
    GBinderBuffer* buf = gbinder_reader_read_buffer(reader);
    char* str = NULL;

    if (buf && buf->size == sizeof(GBinderHidlString)) {
        const GBinderHidlString* s = buf->data;
        GBinderBuffer* sbuf = gbinder_reader_read_buffer(reader);

        if (sbuf && sbuf->size == s->len + 1 &&
            sbuf->data == s->data.str &&
            s->data.str[s->len] == 0) {
            str = g_strdup(s->data.str);
        }
        gbinder_buffer_free(sbuf);
    }
    gbinder_buffer_free(buf);
    return str;
}

char**
gbinder_reader_read_hidl_string_vec(
    GBinderReader* reader)
{
    GBinderBuffer* buf = gbinder_reader_read_buffer(reader);

    /* First buffer contains hidl_vector */
    if (buf && buf->size == sizeof(GBinderHidlVec)) {
        GBinderHidlVec* vec = buf->data;
        const guint n = vec->count;
        const void* next = vec->data.ptr;

        gbinder_buffer_free(buf);
        if (!next && !n) {
            char** out = g_new(char*, 1);

            out[0] = NULL;
            return out;
        } else {
            /* The second buffer (if any) contains n hidl_string's */
            buf = gbinder_reader_read_buffer(reader);
            if (buf && buf->data == next &&
                buf->size == (sizeof(GBinderHidlString) * n)) {
                const GBinderHidlString* strings = buf->data;
                GBinderBuffer* sbuf;
                GPtrArray* list = g_ptr_array_new();
                guint i;

                /* Now we expect n buffers containing the actual data */
                for (i=0; i<n &&
                    (sbuf = gbinder_reader_read_buffer(reader)); i++) {
                    const GBinderHidlString* s = strings + i;
                    if (sbuf->size == s->len + 1 &&
                        sbuf->data == s->data.str &&
                        s->data.str[s->len] == 0) {
                        char* name = g_strdup(s->data.str);

                        g_ptr_array_add(list, name);
                        GVERBOSE_("%u. %s", i + 1, name);
                        gbinder_buffer_free(sbuf);
                    } else {
                        GWARN("Unexpected hidl_string buffer %p/%u vs %p/%u",
                            sbuf->data, (guint)sbuf->size, s->data.str, s->len);
                        gbinder_buffer_free(sbuf);
                        break;
                    }
                }

                if (i == n) {
                    gbinder_buffer_free(buf);
                    g_ptr_array_add(list, NULL);
                    return (char**)g_ptr_array_free(list, FALSE);
                }

                g_ptr_array_set_free_func(list, g_free);
                g_ptr_array_free(list, TRUE);
            }
        }
    }
    GWARN("Invalid hidl_vec<string>");
    gbinder_buffer_free(buf);
    return NULL;
}

const char*
gbinder_reader_read_string8(
    GBinderReader* reader)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);
    const guint8* ptr = p->ptr;

    /* Calculate the length */
    while (ptr < p->end && *ptr) ptr++;
    if (ptr < p->end) {
        /* Zero terminator has been found within the bounds */
        const gsize len = ptr - p->ptr;
        const gsize size = G_ALIGN4(len+1);

        if (p->ptr + size <= p->end) {
            const char* str = (char*)p->ptr;

            p->ptr += size;
            return str;
        }
    }
    return NULL;
}

gboolean
gbinder_reader_read_nullable_string16(
    GBinderReader* reader,
    char** out)
{
    gunichar2* str;
    gsize len;

    if (gbinder_reader_read_nullable_string16_utf16(reader, &str, &len)) {
        if (out) {
            *out = str ? g_utf16_to_utf8(str, len, NULL, NULL, NULL) : NULL;
        }
        return TRUE;
    }
    return FALSE;
}

gboolean
gbinder_reader_read_nullable_string16_utf16(
    GBinderReader* reader,
    gunichar2** out,
    gsize* out_len) /* since 1.0.17 */
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if ((p->ptr + 4) <= p->end) {
        const gint32* len_ptr = (gint32*)p->ptr;
        const gint32 len = *len_ptr;

        if (len == -1) {
            /* NULL string */
            p->ptr += 4;
            if (out) {
                *out = NULL;
            }
            if (out_len) {
                *out_len = 0;
            }
            return TRUE;
        } else if (len >= 0) {
            const guint32 padded_len = G_ALIGN4((len + 1)*2);
            gunichar2* utf16 = (gunichar2*)(p->ptr + 4);

            if ((p->ptr + padded_len + 4) <= p->end) {
                p->ptr += padded_len + 4;
                if (out) {
                    *out = utf16;
                }
                if (out_len) {
                    *out_len = len;
                }
                return TRUE;
            }
        }
    }
    return FALSE;
}

char*
gbinder_reader_read_string16(
    GBinderReader* reader)
{
    char* str = NULL;

    gbinder_reader_read_nullable_string16(reader, &str);
    return str;
}

gboolean
gbinder_reader_skip_string16(
    GBinderReader* reader)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if ((p->ptr + 4) <= p->end) {
        const gint32* len_ptr = (gint32*)p->ptr;
        const gint32 len = *len_ptr;

        if (len == -1) {
            /* NULL string */
            p->ptr += 4;
            return TRUE;
        } else if (len >= 0) {
            const guint32 padded_len = G_ALIGN4((len+1)*2);

            if ((p->ptr + padded_len + 4) <= p->end) {
                p->ptr += padded_len + 4;
                return TRUE;
            }
        }
    }
    return FALSE;
}

const void*
gbinder_reader_read_byte_array(
    GBinderReader* reader,
    gsize* len) /* since 1.0.12 */
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);
    const void* data = NULL;
    const gint32* ptr;
    *len = 0;

    if (gbinder_reader_can_read(p, sizeof(*ptr))) {
        ptr = (void*)p->ptr;
        if (*ptr <= 0) {
            p->ptr += sizeof(*ptr);
            /* Any non-NULL pointer just to indicate success */
            data = p->start;
        } else if (gbinder_reader_can_read(p, sizeof(*ptr) + *ptr)) {
            *len = (gsize)*ptr;
            p->ptr += sizeof(*ptr);
            data = p->ptr;
            p->ptr += *len;
        }
    }
    return data;
}

gsize
gbinder_reader_bytes_read(
    const GBinderReader* reader)
{
    const GBinderReaderPriv* p = gbinder_reader_cast_c(reader);

    return p->ptr - p->start;
}

gsize
gbinder_reader_bytes_remaining(
    const GBinderReader* reader)
{
    const GBinderReaderPriv* p = gbinder_reader_cast_c(reader);

    return p->end - p->ptr;
}

void
gbinder_reader_copy(
    GBinderReader* dest,
    const GBinderReader* src)
{
    /* It's actually quite simple :) */
    memcpy(dest, src, sizeof(*dest));
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
