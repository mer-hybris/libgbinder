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

#include "gbinder_reader_p.h"
#include "gbinder_buffer_p.h"
#include "gbinder_io.h"
#include "gbinder_object_registry.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

#include <errno.h>
#include <fcntl.h>

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

static
inline
gboolean
gbinder_reader_can_read_object(
    GBinderReaderPriv* p)
{
    const GBinderReaderData* data = p->data;

    return data && data->reg &&
        p->objects && p->objects[0] &&
        p->ptr == p->objects[0];
}

int
gbinder_reader_read_fd(
    GBinderReader* reader) /* Since 1.0.18 */
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (gbinder_reader_can_read_object(p)) {
        int fd;
        const guint eaten = p->data->reg->io->decode_fd_object(p->ptr,
            gbinder_reader_bytes_remaining(reader), &fd);

        if (eaten) {
            GASSERT(fd >= 0);
            p->ptr += eaten;
            p->objects++;
            return fd;
        }
    }
    return -1;
}

int
gbinder_reader_read_dup_fd(
    GBinderReader* reader) /* Since 1.0.18 */
{
    const int fd = gbinder_reader_read_fd(reader);

    if (fd >= 0) {
        const int dupfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);

        if (dupfd >= 0) {
            return dupfd;
        } else {
            GWARN("Error dupping fd %d: %s", fd, strerror(errno));
        }
    }
    return -1;
}

gboolean
gbinder_reader_read_nullable_object(
    GBinderReader* reader,
    GBinderRemoteObject** out)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (gbinder_reader_can_read_object(p)) {
        const GBinderReaderData* data = p->data;
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
gbinder_reader_read_buffer_object(
    GBinderReader* reader,
    GBinderIoBufferObject* out)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (gbinder_reader_can_read_object(p)) {
        const GBinderReaderData* data = p->data;
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
    return FALSE;
}

GBinderBuffer*
gbinder_reader_read_buffer(
    GBinderReader* reader)
{
    GBinderIoBufferObject obj;

    if (gbinder_reader_read_buffer_object(reader, &obj)) {
        const GBinderReaderData* data = gbinder_reader_cast(reader)->data;
        GBinderBuffer* buf = data->buffer;

        return gbinder_buffer_new_with_parent(buf, obj.data, obj.size);
    }
    return NULL;
}

gboolean
gbinder_reader_skip_buffer(
    GBinderReader* reader)
{
    return gbinder_reader_read_buffer_object(reader, NULL);
}

/* Helper for gbinder_reader_read_hidl_struct() macro */
const void*
gbinder_reader_read_hidl_struct1(
    GBinderReader* reader,
    gsize size) /* Since 1.0.9 */
{
    GBinderIoBufferObject obj;

    if (gbinder_reader_read_buffer_object(reader, &obj) && obj.size == size) {
        return obj.data;
    }
    return NULL;
}

/* Doesn't copy the data */
const void*
gbinder_reader_read_hidl_vec(
    GBinderReader* reader,
    gsize* count,
    gsize* elemsize)
{
    GBinderIoBufferObject obj;
    const void* out = NULL;
    gsize out_count = 0, out_elemsize = 0;

    if (gbinder_reader_read_buffer_object(reader, &obj) &&
        obj.data && obj.size == sizeof(GBinderHidlVec)) {
        const GBinderHidlVec* vec = obj.data;
        const void* next = vec->data.ptr;

        if (next) {
            if (gbinder_reader_read_buffer_object(reader, &obj) &&
                obj.data == next && ((!vec->count && !obj.size) ||
                (vec->count && obj.size && !(obj.size % vec->count)))) {
                out_elemsize = vec->count ? (obj.size / vec->count) : 0;
                out_count = vec->count;
                out = obj.data;
            }
        } else if (!vec->count) {
            /* Any non-NULL pointer just to indicate success? */
            out = vec;
        }
    }
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
    guint expected_elem_size) /* Since 1.0.9 */
{
    gsize actual;
    const void* data = gbinder_reader_read_hidl_vec(reader, count, &actual);

    /* Actual size will be zero for an empty array */
    return (data && (actual == expected_elem_size || !actual)) ? data : NULL;
}

const char*
gbinder_reader_read_hidl_string_c(
    GBinderReader* reader) /* Since 1.0.23 */
{
    GBinderIoBufferObject obj;

    if (gbinder_reader_read_buffer_object(reader, &obj) &&
        obj.data && obj.size == sizeof(GBinderHidlString)) {
        const GBinderHidlString* str = obj.data;

        if (gbinder_reader_read_buffer_object(reader, &obj) &&
            obj.has_parent &&
            obj.parent_offset == GBINDER_HIDL_STRING_BUFFER_OFFSET &&
            obj.data == str->data.str &&
            obj.size == str->len + 1 &&
            str->data.str[str->len] == 0) {
            return str->data.str;
        }
    }
    return NULL;
}

char*
gbinder_reader_read_hidl_string(
    GBinderReader* reader)
{
    /* This function should've been called gbinder_reader_dup_hidl_string */
    return g_strdup(gbinder_reader_read_hidl_string_c(reader));
}

char**
gbinder_reader_read_hidl_string_vec(
    GBinderReader* reader)
{
    GBinderIoBufferObject obj;

    /* First buffer contains hidl_vector */
    if (gbinder_reader_read_buffer_object(reader, &obj) &&
        obj.data && obj.size == sizeof(GBinderHidlVec)) {
        GBinderHidlVec* vec = obj.data;
        const guint n = vec->count;
        const void* next = vec->data.ptr;

        if (!next && !n) {
            /* Should this be considered an error? */
            return g_new0(char*, 1);
        } else if (gbinder_reader_read_buffer_object(reader, &obj) &&
                   /* The second buffer (if any) contains n hidl_string's */
                   obj.parent_offset == GBINDER_HIDL_VEC_BUFFER_OFFSET &&
                   obj.has_parent &&
                   obj.data == next &&
                   obj.size == (sizeof(GBinderHidlString) * n)) {
            const GBinderHidlString* strings = obj.data;
            GPtrArray* list = g_ptr_array_sized_new(n + 1);
            guint i;

            /* Now we expect n buffers containing the actual data */
            for (i = 0; i < n &&
                gbinder_reader_read_buffer_object(reader, &obj); i++) {
                const GBinderHidlString* s = strings + i;
                const gsize expected_offset = (i * sizeof(*s)) +
                    GBINDER_HIDL_STRING_BUFFER_OFFSET;
                if (obj.has_parent &&
                    obj.parent_offset == expected_offset &&
                    obj.data == s->data.str &&
                    obj.size == s->len + 1 &&
                    s->data.str[s->len] == 0) {
                    char* name = g_strdup(s->data.str);

                    g_ptr_array_add(list, name);
                    GVERBOSE_("%u. %s", i + 1, name);
                } else {
                    GWARN("Unexpected hidl_string buffer %p/%u vs %p/%u",
                        obj.data, (guint)obj.size, s->data.str, s->len);
                    break;
                }
            }

            if (i == n) {
                g_ptr_array_add(list, NULL);
                return (char**)g_ptr_array_free(list, FALSE);
            }

            g_ptr_array_set_free_func(list, g_free);
            g_ptr_array_free(list, TRUE);
        }
    }
    GWARN("Invalid hidl_vec<string>");
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
    const gunichar2* str;
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
    const gunichar2** out,
    gsize* out_len) /* Since 1.0.17 */
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
            const gunichar2* utf16 = (gunichar2*)(p->ptr + 4);

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

const gunichar2*
gbinder_reader_read_string16_utf16(
    GBinderReader* reader,
    gsize* len) /* Since 1.0.26 */
{
    const gunichar2* str;

    /*
     * Use gbinder_reader_read_nullable_string16_utf16 to distinguish
     * NULL string from a parsing failure.
     */
    return gbinder_reader_read_nullable_string16_utf16(reader, &str, len) ?
        str : NULL;
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
    gsize* len) /* Since 1.0.12 */
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
