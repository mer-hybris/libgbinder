/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 * Copyright (C) 2018-2024 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2024 Jolla Ltd.
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

#include "gbinder_reader_p.h"
#include "gbinder_buffer_p.h"
#include "gbinder_io.h"
#include "gbinder_object_registry.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

#include <errno.h>
#include <fcntl.h>

/*
 * GBINDER_READER_FLAG_HAS_PARENT flag means that the objects field
 * actually points to the top-most GBinderReader's objects pointer,
 * not to GBinderReaderData's objects area. That way, all nested
 * readers would share the same objects pointer and that pointer
 * can be correctly updated even if objects are read by the child
 * (parcelable) reader.
 *
 * +-------+-------+-----+-------+
 * | obj_1 | obj_2 | ... | obj_n | tx objects
 * +-------+-------+-----+-------+
 *            ^
 *            |
 *      +-----+
 *      |
 * +---------+
 * | objects | Top level GBinderReader
 * +---------+
 *      ^
 *      |    +---------+
 *      +----| objects | GBinderReader with a parent
 *      |    +---------+
 *      |
 *      |    +---------+
 *      +----| objects | GBinderReader with a parent
 *           +---------+
 */
typedef enum gbinder_reader_flags {
    GBINDER_READER_FLAG_NONE = 0,
    GBINDER_READER_FLAG_HAS_PARENT = 0x1,
} GBINDER_READER_FLAGS;

typedef struct gbinder_reader_priv {
    const guint8* start;
    const guint8* end;
    const guint8* ptr;
    const GBinderReaderData* data;
    void** objects;
    GBINDER_READER_FLAGS flags;
} GBinderReaderPriv;

G_STATIC_ASSERT(sizeof(GBinderReader) >= sizeof(GBinderReaderPriv));

static inline GBinderReaderPriv* gbinder_reader_cast(GBinderReader* reader)
    { return (GBinderReaderPriv*)reader; }
static inline const GBinderReaderPriv* gbinder_reader_cast_c
    (const GBinderReader* reader)  { return (GBinderReaderPriv*)reader; }

static
void
gbinder_reader_init2(
    GBinderReader* reader,
    const GBinderReaderData* data,
    gsize offset,
    gsize len,
    void*** parent_objects)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    p->data = data;
    p->flags = GBINDER_READER_FLAG_NONE;
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

        if (parent_objects) {
            p->objects = (void**)parent_objects;
            p->flags |= GBINDER_READER_FLAG_HAS_PARENT;
        } else {
            p->objects = data->objects;
        }
    } else {
        p->ptr = p->start = p->end = NULL;
        p->objects = NULL;
    }
}

void
gbinder_reader_init(
    GBinderReader* reader,
    GBinderReaderData* data,
    gsize offset,
    gsize len)
{
    gbinder_reader_init2(reader, data, offset, len, NULL);
}

gboolean
gbinder_reader_at_end(
    const GBinderReader* reader)
{
    const GBinderReaderPriv* p = gbinder_reader_cast_c(reader);

    return !p || p->ptr >= p->end;
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
    /*
     * Android's libhwbinder writes bool as a single byte and pads it
     * with zeros, but libbinder writes bool as int32 in native byte
     * order. The latter becomes either [0x01, 0x00, 0x00, 0x00] or
     * [0x00, 0x00, 0x00, 0x01] depending on the byte order. Reading
     * uint32 and comparing it with zero works in either case.
     */
    if (value) {
        guint32 padded;

        if (gbinder_reader_read_uint32(reader, &padded)) {
            *value = (padded != 0);
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        return gbinder_reader_read_uint32(reader, NULL);
    }
}

gboolean
gbinder_reader_read_int8(
    GBinderReader* reader,
    gint8* value) /* Since 1.1.15 */
{
    return gbinder_reader_read_uint8(reader, (guint8*)value);
}

gboolean
gbinder_reader_read_uint8(
    GBinderReader* reader,
    guint8* value) /* Since 1.1.15 */
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    /* Primitive values are supposed to be padded to 4-byte boundary */
    if (gbinder_reader_can_read(p, 4)) {
        if (value) {
            *value = p->ptr[0];
        }
        p->ptr += 4;
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
gbinder_reader_read_int16(
    GBinderReader* reader,
    gint16* value) /* Since 1.1.15 */
{
    return gbinder_reader_read_uint16(reader, (guint16*)value);
}

gboolean
gbinder_reader_read_uint16(
    GBinderReader* reader,
    guint16* value) /* Since 1.1.15 */
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    /* Primitive values are supposed to be padded to 4-byte boundary */
    if (gbinder_reader_can_read(p, 4)) {
        if (value) {
            *value = *(guint16*)p->ptr;
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

    if (data && data->reg) {
        void** objs = (p->flags & GBINDER_READER_FLAG_HAS_PARENT) ?
            (*((void***)p->objects)) : p->objects;

        if (objs && objs[0] == p->ptr) {
            return TRUE;
        }
    }
    return FALSE;
}

static
inline
gboolean
gbinder_reader_consume_object(
    GBinderReaderPriv* p,
    gsize size)
{
    if (size) {
        p->ptr += size;
        if (p->flags & GBINDER_READER_FLAG_HAS_PARENT) {
            /* Increment the top reader's obj pointer */
            (*((void***)p->objects))++;
        } else {
            /* We are the top reader */
            p->objects++;
        }
        return TRUE;
    }
    return FALSE;
}

int
gbinder_reader_read_fd(
    GBinderReader* reader) /* Since 1.0.18 */
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);

    if (gbinder_reader_can_read_object(p)) {
        const gsize size = gbinder_reader_bytes_remaining(reader);
        int fd = -1;

        if (gbinder_reader_consume_object(p, p->data->reg->io->
            decode_fd_object(p->ptr, size, &fd))) {
            GASSERT(fd >= 0);
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
        const GBinderRpcProtocol* rpc = gbinder_buffer_protocol(data->buffer);
        const gsize size = gbinder_reader_bytes_remaining(reader);

        if (gbinder_reader_consume_object(p, data->reg->io->
            decode_binder_object(p->ptr,size, data->reg, out, rpc))) {
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
        const gsize offset = p->ptr - (guint8*)buf->data;

        if (gbinder_reader_consume_object(p, data->reg->io->
            decode_buffer_object(buf, offset, out))) {
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

/* AIDL Parcelable */

static
gboolean
gbinder_reader_read_parcelable_header(
    GBinderReader* reader,
    guint32* size,
    gboolean* nullp)
{
    GBinderReaderPriv* p = gbinder_reader_cast(reader);
    const guint8* saved_ptr = p->ptr;
    guint32 flag;

    if (gbinder_reader_read_uint32(reader, &flag)) {
        if (flag == kNullParcelableFlag) {
            *size = 0;
            *nullp = TRUE;
            return TRUE;
        } else {
            guint32 payload_size;

            if (gbinder_reader_read_uint32(reader, &payload_size) &&
                payload_size >= sizeof(payload_size)) {
                payload_size -= sizeof(payload_size);
                if (p->ptr + payload_size <= p->end) {
                    /* Non-NULL parcelable */
                    *size = payload_size;
                    *nullp = FALSE;
                    return TRUE;
                }
            }
        }
    }

    /* Restore the state on failure */
    p->ptr = saved_ptr;
    return FALSE;
}

const void*
gbinder_reader_read_parcelable(
    GBinderReader* reader,
    gsize* size) /* Since 1.1.19 */
{
    /*
     * Use gbinder_reader_read_parcelable2 if you need to distinguish a failure
     * to read a parcelable from a successfully read NULL parcelable.
     */
    return gbinder_reader_read_parcelable2(reader, size, NULL);
}

const void*
gbinder_reader_read_parcelable2(
    GBinderReader* reader,
    gsize* size,
    gboolean* ok) /* Since 1.1.48 */
{
    const void* out = NULL;
    guint32 psize = 0;
    gboolean nullp;

    if (gbinder_reader_read_parcelable_header(reader, &psize, &nullp)) {
        GBinderReaderPriv* p = gbinder_reader_cast(reader);

        if (!nullp) {
            out = p->ptr;
            p->ptr += psize;

            /*
             * If this parcelable contains binder objects we must skip those
             * objects or else we won't be able to read next objects from this
             * tx buffer.
             */
            if (p->objects) {
                guint8*** objs = (guint8***)
                    ((p->flags & GBINDER_READER_FLAG_HAS_PARENT) ?
                     (void***) p->objects : &p->objects);

                if (*objs) {
                    while ((*objs)[0] && (*objs)[0] < p->ptr) {
                        (*objs)++;
                    }
                }
            }
        }

        if (ok) {
            *ok = TRUE;
        }
    } else if (ok) {
        *ok = FALSE;
    }

    if (size) {
        *size = psize;
    }
    return out;
}

gboolean
gbinder_reader_start_parcelable(
    GBinderReader* reader,
    GBinderReader* parcelable,
    gboolean* non_null) /* Since 1.1.48 */
{
    guint32 size;
    gboolean nullp;

    /*
     * gbinder_reader_finish_parcelable() should be invoked on every
     * parcelable reader initialized by this function. It's especially
     * important (if not to say mandatory) if the parcelable contains
     * binder objects.
     */
    if (gbinder_reader_read_parcelable_header(reader, &size, &nullp)) {
        if (nullp) {
            /* NULL parcelable (with a parent) */
            gbinder_reader_init(parcelable, NULL, 0, 0);
            gbinder_reader_cast(parcelable)->flags |=
                GBINDER_READER_FLAG_HAS_PARENT;
        } else {
            GBinderReaderPriv* p = gbinder_reader_cast(reader);
            const GBinderReaderData* data = p->data;

            gbinder_reader_init2(parcelable, data,
                (p->ptr - (guint8*)data->buffer->data), size,
                (p->flags & GBINDER_READER_FLAG_HAS_PARENT) ?
                (void***)p->objects : &p->objects);

            /* Move the pointer */
            p->ptr += size;
        }
        if (non_null) {
            *non_null = !nullp;
        }
        return TRUE;
    } else {
        memset(parcelable, 0, sizeof(*parcelable));
        if (non_null) {
            *non_null = FALSE;
        }
        return FALSE;
    }
}

void
gbinder_reader_finish_parcelable(
    GBinderReader* parcelable) /* Since 1.1.48 */
{
    GBinderReaderPriv* p = gbinder_reader_cast(parcelable);

    /*
     * The reader must be created by gbinder_reader_start_parcelable()
     * and have a parent but let's check it anyway.
     */
    if (p->flags & GBINDER_READER_FLAG_HAS_PARENT) {
        /*
         * If the remaining (unread) part of the parcelable contains binder
         * objects we must "consume" those objects or else the parent reader
         * won't be able to read any more objects.
         */
        if (p->objects) {
            guint8*** objs = (guint8***)p->objects;

            if (*objs) {
                while ((*objs)[0] && (*objs)[0] < p->end) {
                    (*objs)++;
                }
            }
        }

        /* No more reads from this reader */
        p->ptr = p->end;
    } else {
        GWARN("Invalid reader passed to gbinder_reader_finish_parcelable");
    }
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

/* The equivalent of Android's Parcel::readCString */
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

/* The equivalent of Android's Parcel::readString8 */
gboolean
gbinder_reader_read_nullable_string8(
    GBinderReader* reader,
    const char** out,
    gsize* out_len) /* Since 1.1.41 */
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
        } else if (len >= 0 && len < INT32_MAX) {
            const guint32 padded_len = G_ALIGN4(len + 1);
            const char* str = (char*)(p->ptr + 4);

            if ((p->ptr + padded_len + 4) <= p->end && !str[len]) {
                p->ptr += padded_len + 4;
                if (out) {
                    *out = str;
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

/* The equivalent of Android's Parcel::readString16 */
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
        } else if (len >= 0 && len < INT32_MAX) {
            const gsize padded_len = G_ALIGN4((((gsize)len) + 1) * 2);

            if ((p->ptr + padded_len + 4) <= p->end) {
                const gunichar2* utf16 = (gunichar2*)(p->ptr + 4);

                if (!utf16[len]) {
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
    return gbinder_reader_read_nullable_string16_utf16(reader, NULL, NULL);
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
            /* Android aligns byte array reads and writes to 4 bytes */
            p->ptr += G_ALIGN4(*len);
        }
    }
    return data;
}

const void*
gbinder_reader_get_data(
    const GBinderReader* reader,
    gsize* size) /* Since 1.1.14 */
{
    const GBinderReaderPriv* p = gbinder_reader_cast_c(reader);

    if (p) {
        const GBinderReaderData* data = p->data;

        if (data && data->buffer) {
            if (size) {
                *size = data->buffer->size;
            }
            return data->buffer->data;
        }
    }

    /* No data */
    if (size) {
        *size = 0;
    }
    return NULL;
}

gsize
gbinder_reader_bytes_read(
    const GBinderReader* reader)
{
    const GBinderReaderPriv* p = gbinder_reader_cast_c(reader);

    return p ? (p->ptr - p->start) : 0;
}

gsize
gbinder_reader_bytes_remaining(
    const GBinderReader* reader)
{
    const GBinderReaderPriv* p = gbinder_reader_cast_c(reader);

    return p ? (p->end - p->ptr) : 0;
}

void
gbinder_reader_copy(
    GBinderReader* dest,
    const GBinderReader* src)
{
    if (src) {
        memcpy(dest, src, sizeof(*dest));
    } else {
        memset(dest, 0, sizeof(*dest));
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
