/*
 * Copyright (C) 2018-2024 Jolla Ltd.
 * Copyright (C) 2018-2024 Slava Monich <slava@monich.com>
 *
 * You may use this file under the terms of the BSD license as follows:
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

#include "gbinder_writer_p.h"
#include "gbinder_buffer_p.h"
#include "gbinder_fmq_p.h"
#include "gbinder_local_object.h"
#include "gbinder_object_converter.h"
#include "gbinder_io.h"
#include "gbinder_log.h"

#include <gutil_intarray.h>
#include <gutil_macros.h>
#include <gutil_strv.h>
#include <gutil_misc.h>

#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>

typedef struct gbinder_writer_priv {
    GBinderWriterData* data;
} GBinderWriterPriv;

const GBinderWriterType gbinder_writer_type_byte = { "byte", 1, NULL };
const GBinderWriterType gbinder_writer_type_int32 = { "int32", 4, NULL };
static const GBinderWriterField gbinder_writer_type_hidl_string_f[] = {
    {
        "hidl_string.data.str", 0, NULL,
        gbinder_writer_field_hidl_string_write_buf, NULL
    },
    GBINDER_WRITER_FIELD_END()
};
const GBinderWriterType gbinder_writer_type_hidl_string = {
    "hidl_string", sizeof(GBinderHidlString),
    gbinder_writer_type_hidl_string_f
};

G_STATIC_ASSERT(sizeof(GBinderWriter) >= sizeof(GBinderWriterPriv));

GBINDER_INLINE_FUNC GBinderWriterPriv* gbinder_writer_cast(GBinderWriter* pub)
    { return (GBinderWriterPriv*)pub; }
GBINDER_INLINE_FUNC GBinderWriterData* gbinder_writer_data(GBinderWriter* pub)
    { return G_LIKELY(pub) ? gbinder_writer_cast(pub)->data : NULL; }

void
gbinder_writer_data_set_contents(
    GBinderWriterData* data,
    GBinderBuffer* buffer,
    GBinderObjectConverter* convert)
{
    g_byte_array_set_size(data->bytes, 0);
    gutil_int_array_set_count(data->offsets, 0);
    data->buffers_size = 0;
    gbinder_cleanup_reset(data->cleanup);
    gbinder_writer_data_append_contents(data, buffer, 0, convert);
}

void
gbinder_writer_data_append_contents(
    GBinderWriterData* data,
    GBinderBuffer* buffer,
    gsize off,
    GBinderObjectConverter* convert)
{
    GBinderBufferContents* contents = gbinder_buffer_contents(buffer);

    if (contents) {
        gsize bufsize;
        GByteArray* dest = data->bytes;
        const guint8* bufdata = gbinder_buffer_data(buffer, &bufsize);
        void** objects = gbinder_buffer_objects(buffer);

        data->cleanup = gbinder_cleanup_add(data->cleanup, (GDestroyNotify)
            gbinder_buffer_contents_unref,
            gbinder_buffer_contents_ref(contents));
        if (objects && *objects) {
            const GBinderIo* io = gbinder_buffer_io(buffer);
            const GBinderRpcProtocol* proto = gbinder_buffer_protocol(buffer);

            /* GBinderIo must be the same because it's defined by the kernel */
            GASSERT(io == data->io);
            if (!data->offsets) {
                data->offsets = gutil_int_array_new();
            }
            while (*objects) {
                const guint8* obj = *objects++;
                gsize objsize, offset = obj - bufdata;
                GBinderLocalObject* local;
                guint32 handle;

                GASSERT(offset >= off && offset < bufsize);
                if (offset > off) {
                    /* Copy serialized data preceeding this object */
                    g_byte_array_append(dest, bufdata + off, offset - off);
                    off = offset;
                }
                /* Offset in the destination buffer */
                gutil_int_array_append(data->offsets, dest->len);

                /* Convert remote object into local if necessary */
                if (convert && io->decode_binder_handle(obj, &handle, proto) &&
                    (local = gbinder_object_converter_handle_to_local
                    (convert, handle))) {
                    const guint pos = dest->len;

                    g_byte_array_set_size(dest, pos +
                        GBINDER_MAX_BINDER_OBJECT_SIZE);
                    objsize = io->encode_local_object(dest->data + pos,
                        local, proto);
                    g_byte_array_set_size(dest, pos + objsize);

                    /* Keep the reference */
                    data->cleanup = gbinder_cleanup_add(data->cleanup,
                        (GDestroyNotify) gbinder_local_object_unref, local);
                } else {
                    objsize = io->object_size(obj, proto);
                    g_byte_array_append(dest, obj, objsize);
                }

                /* Size of each buffer has to be 8-byte aligned */
                data->buffers_size += G_ALIGN8(io->object_data_size(obj));
                off += objsize;
            }
        }
        if (off < bufsize) {
            /* Copy remaining data */
            g_byte_array_append(dest, bufdata + off, bufsize - off);
        }
    }
}

static
void
gbinder_writer_data_record_offset(
    GBinderWriterData* data,
    guint offset)
{
    if (!data->offsets) {
        data->offsets = gutil_int_array_new();
    }
    gutil_int_array_append(data->offsets, offset);
}

void
gbinder_writer_init(
    GBinderWriter* self,
    GBinderWriterData* data)
{
    memset(self, 0, sizeof(*self));
    gbinder_writer_cast(self)->data = data;
}

const void*
gbinder_writer_get_data(
    GBinderWriter* self,
    gsize* size) /* Since 1.1.14 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        GByteArray* buf = data->bytes;

        if (size) {
            *size = buf->len;
        }
        return buf->data;
    } else {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
}

gsize
gbinder_writer_bytes_written(
    GBinderWriter* self) /* since 1.0.21 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    return G_LIKELY(data) ? data->bytes->len : 0;
}

static
void
gbinder_writer_data_append_padded(
    GByteArray* buf,
    const void* data,
    guint size)
{
    /* Primitive values are padded to 4-byte boundary */
    const guint padded_size = 4;
    guint8* ptr;

    g_byte_array_set_size(buf, buf->len + padded_size);
    ptr = buf->data + (buf->len - padded_size);
    memcpy(ptr, data, size);
    memset(ptr + size, 0, padded_size - size);
}

void
gbinder_writer_append_bool(
    GBinderWriter* self,
    gboolean value)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_bool(data, value);
    }
}

void
gbinder_writer_data_append_bool(
    GBinderWriterData* data,
    gboolean value)
{
    const guint8 b = (value != FALSE);

    gbinder_writer_data_append_padded(data->bytes, &b, sizeof(b));
}

void
gbinder_writer_append_int8(
    GBinderWriter* self,
    guint8 value) /* Since 1.1.15 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_padded(data->bytes, &value, sizeof(value));
    }
}

void
gbinder_writer_append_int16(
    GBinderWriter* self,
    guint16 value) /* Since 1.1.15 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_padded(data->bytes, &value, sizeof(value));
    }
}

void
gbinder_writer_append_int32(
    GBinderWriter* self,
    guint32 value)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_int32(data, value);
    }
}

void
gbinder_writer_data_append_int32(
    GBinderWriterData* data,
    guint32 value)
{
    GByteArray* buf = data->bytes;
    guint32* ptr;

    g_byte_array_set_size(buf, buf->len + sizeof(*ptr));
    ptr = (void*)(buf->data + (buf->len - sizeof(*ptr)));
    *ptr = value;
}

void
gbinder_writer_overwrite_int32(
    GBinderWriter* self,
    gsize offset,
    gint32 value) /* since 1.0.21 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        GByteArray* buf = data->bytes;

        if (buf->len >= offset + sizeof(gint32)) {
            *((gint32*)(buf->data + offset)) = value;
        } else {
            GWARN("Can't overwrite at %lu as buffer is only %u bytes long",
                (gulong)offset, buf->len);
        }
    }
}

void
gbinder_writer_append_int64(
    GBinderWriter* self,
    guint64 value)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_int64(data, value);
    }
}

void
gbinder_writer_data_append_int64(
    GBinderWriterData* data,
    guint64 value)
{
    GByteArray* buf = data->bytes;
    guint64* ptr;

    g_byte_array_set_size(buf, buf->len + sizeof(*ptr));
    ptr = (void*)(buf->data + (buf->len - sizeof(*ptr)));
    *ptr = value;
}

void
gbinder_writer_append_float(
    GBinderWriter* self,
    gfloat value)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_float(data, value);
    }
}

void
gbinder_writer_data_append_float(
    GBinderWriterData* data,
    gfloat value)
{
    GByteArray* buf = data->bytes;
    gfloat* ptr;

    g_byte_array_set_size(buf, buf->len + sizeof(*ptr));
    ptr = (void*)(buf->data + (buf->len - sizeof(*ptr)));
    *ptr = value;
}

void
gbinder_writer_append_double(
    GBinderWriter* self,
    gdouble value)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_double(data, value);
    }
}

void
gbinder_writer_data_append_double(
    GBinderWriterData* data,
    gdouble value)
{
    GByteArray* buf = data->bytes;
    gdouble* ptr;

    g_byte_array_set_size(buf, buf->len + sizeof(*ptr));
    ptr = (void*)(buf->data + (buf->len - sizeof(*ptr)));
    *ptr = value;
}

void
gbinder_writer_append_string8(
    GBinderWriter* self,
    const char* str)
{
    gbinder_writer_append_string8_len(self, str, str ? strlen(str) : 0);
}

void
gbinder_writer_append_string8_len(
    GBinderWriter* self,
    const char* str,
    gsize len)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_string8_len(data, str, len);
    }
}

void
gbinder_writer_data_append_string8(
    GBinderWriterData* data,
    const char* str)
{
    gbinder_writer_data_append_string8_len(data, str, str ? strlen(str) : 0);
}

void
gbinder_writer_data_append_string8_len(
    GBinderWriterData* data,
    const char* str,
    gsize len)
{
    if (G_LIKELY(str)) {
        GByteArray* buf = data->bytes;
        const gsize old_size = buf->len;
        gsize padded_len = G_ALIGN4(len + 1);
        guint32* dest;

        /* Preallocate space */
        g_byte_array_set_size(buf, old_size + padded_len);

        /* Zero the last word */
        dest = (guint32*)(buf->data + old_size);
        dest[padded_len/4 - 1] = 0;

        /* Copy the data */
        memcpy(dest, str, len);
    }
}

void
gbinder_writer_append_string16(
    GBinderWriter* self,
    const char* utf8)
{
    gbinder_writer_append_string16_len(self, utf8, utf8 ? strlen(utf8) : 0);
}

void
gbinder_writer_append_string16_len(
    GBinderWriter* self,
    const char* utf8,
    gssize num_bytes)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_string16_len(data, utf8, num_bytes);
    }
}

void
gbinder_writer_data_append_string16(
    GBinderWriterData* data,
    const char* utf8)
{
    gbinder_writer_data_append_string16_len(data, utf8, utf8? strlen(utf8) : 0);
}

static
void
gbinder_writer_data_append_string16_null(
    GBinderWriterData* data)
{
    /* NULL string */
    gbinder_writer_data_append_int32(data, -1);
}

static
void
gbinder_writer_data_append_string16_empty(
    GBinderWriterData* data)
{
    GByteArray* buf = data->bytes;
    const gsize old_size = buf->len;
    guint16* ptr16;

    /* Empty string */
    g_byte_array_set_size(buf, old_size + 8);
    ptr16 = (guint16*)(buf->data + old_size);
    ptr16[0] = ptr16[1] = ptr16[2] = 0; ptr16[3] = 0xffff;
}

void
gbinder_writer_data_append_string16_len(
    GBinderWriterData* data,
    const char* utf8,
    gssize num_bytes)
{
    if (utf8) {
        const char* end = utf8;

        g_utf8_validate(utf8, num_bytes, &end);
        num_bytes = end - utf8;
    } else {
        num_bytes = 0;
    }

    if (num_bytes > 0) {
        GByteArray* buf = data->bytes;
        const gsize old_size = buf->len;
        glong len = g_utf8_strlen(utf8, num_bytes);
        gsize padded_len = G_ALIGN4((len+1)*2);
        guint32* len_ptr;
        gunichar2* utf16_ptr;
        gunichar2* utf16 = NULL;

        /* Create utf-16 string to make sure of its size */
        if (len > 0) {
            glong utf16_len = 0;

            utf16 = g_utf8_to_utf16(utf8, num_bytes, NULL, &utf16_len, NULL);
            if (utf16) {
                len = utf16_len;
                padded_len = G_ALIGN4((len+1)*2);
            }
        }

        /* Preallocate space */
        g_byte_array_set_size(buf, old_size + padded_len + 4);
        len_ptr = (guint32*)(buf->data + old_size);
        utf16_ptr = (gunichar2*)(len_ptr + 1);

        /* Copy string */
        if (utf16) {
            memcpy(utf16_ptr, utf16, len*2);
            g_free(utf16);
        }

        /* Actual length */
        *len_ptr = len;

        /* Zero padding */
        if (padded_len > len*2) {
            memset(utf16_ptr + len, 0, padded_len - len*2);
        }
    } else if (utf8) {
        /* Empty string */
        gbinder_writer_data_append_string16_empty(data);
    } else {
        /* NULL string */
        gbinder_writer_data_append_string16_null(data);
    }
}

static
void
gbinder_writer_data_append_string16_utf16(
    GBinderWriterData* data,
    const gunichar2* utf16,
    gssize length)
{
    if (length < 0) {
        length = 0;
        if (utf16) {
            const guint16* ptr;

            /* Assume NULL terminated string */
            for (ptr = utf16; *ptr; ptr++);
            length = ptr - utf16;
        }
    }
    if (length > 0) {
        GByteArray* buf = data->bytes;
        const gsize old_size = buf->len;
        const gsize padded_size = G_ALIGN4((length + 1) * 2);
        guint32* len_ptr;
        gunichar2* utf16_ptr;

        /* Preallocate space */
        g_byte_array_set_size(buf, old_size + padded_size + 4);
        len_ptr = (guint32*)(buf->data + old_size);
        utf16_ptr = (gunichar2*)(len_ptr + 1);

        /* Actual length */
        *len_ptr = length;

        /* Characters */
        memcpy(utf16_ptr, utf16, 2 * length);

        /* Zero padding */
        memset(utf16_ptr + length, 0, padded_size - 2 * length);
    } else if (utf16) {
        /* Empty string */
        gbinder_writer_data_append_string16_empty(data);
    } else {
        /* NULL string */
        gbinder_writer_data_append_string16_null(data);
    }
}

void
gbinder_writer_append_string16_utf16(
    GBinderWriter* self,
    const gunichar2* utf16,
    gssize length) /* Since 1.0.17 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_string16_utf16(data, utf16, length);
    }
}

void
gbinder_writer_append_bytes(
    GBinderWriter* self,
    const void* ptr,
    gsize size)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        g_byte_array_append(data->bytes, ptr, size);
    }
}

static
void
gbinder_writer_data_close_fd(
    gpointer data)
{
    const int fd = GPOINTER_TO_INT(data);

    if (close(fd) < 0) {
        GWARN("Error closing fd %d: %s", fd, strerror(errno));
    }
}

void
gbinder_writer_data_append_fd(
    GBinderWriterData* data,
    int fd)
{
    GByteArray* buf = data->bytes;
    const guint offset = buf->len;
    /* Duplicate the descriptor so that caller can do whatever with
     * the one it passed in. */
    const int dupfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    guint written;

    /* Preallocate enough space */
    g_byte_array_set_size(buf, offset + GBINDER_MAX_BINDER_OBJECT_SIZE);
    /* Write the original fd if we failed to dup it */
    if (dupfd < 0) {
        GWARN("Error dupping fd %d: %s", fd, strerror(errno));
        written = data->io->encode_fd_object(buf->data + offset, fd);
    } else {
        written = data->io->encode_fd_object(buf->data + offset, dupfd);
        data->cleanup = gbinder_cleanup_add(data->cleanup,
            gbinder_writer_data_close_fd, GINT_TO_POINTER(dupfd));
    }
    /* Fix the data size */
    g_byte_array_set_size(buf, offset + written);
    /* Record the offset */
    gbinder_writer_data_record_offset(data, offset);
}

void
gbinder_writer_append_fd(
    GBinderWriter* self,
    int fd) /* Since 1.0.18 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_fd(data, fd);
    }
}

void
gbinder_writer_data_append_fda_object(
    GBinderWriterData* data,
    const GBinderFds *fds,
    const GBinderParent* parent)
{
    GByteArray* buf = data->bytes;
    const guint offset = buf->len;
    guint written;

    /* Preallocate enough space */
    g_byte_array_set_size(buf, offset + GBINDER_MAX_BINDER_OBJECT_SIZE);

    written = data->io->encode_fda_object(buf->data + offset, fds, parent);

    /* Fix the data size */
    g_byte_array_set_size(buf, offset + written);
    /* Record the offset */
    gbinder_writer_data_record_offset(data, offset);
}

void
gbinder_writer_data_append_fds(
    GBinderWriterData* data,
    const GBinderFds *fds,
    const GBinderParent* parent)
{
    if (fds) {
        /* Size, fds data buffer and fd_array_object */
        const gsize fds_total = sizeof(GBinderFds) +
            sizeof(int) * (fds->num_fds + fds->num_ints);
        GBinderParent fds_parent;

        gbinder_writer_data_append_int64(data, fds_total);
        fds_parent.index = gbinder_writer_data_append_buffer_object(data,
            fds, fds_total, parent);
        fds_parent.offset = sizeof(GBinderFds);
        gbinder_writer_data_append_fda_object(data, fds, &fds_parent);
    } else {
        /* If the pointer is null only write zero size */
        gbinder_writer_data_append_int64(data, 0);
    }
}

guint
gbinder_writer_append_buffer_object_with_parent(
    GBinderWriter* self,
    const void* buf,
    gsize len,
    const GBinderParent* parent)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        return gbinder_writer_data_append_buffer_object(data, buf, len, parent);
    }
    return 0;
}

guint
gbinder_writer_append_buffer_object(
    GBinderWriter* self,
    const void* buf,
    gsize len)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        return gbinder_writer_data_append_buffer_object(data, buf, len, NULL);
    }
    return 0;
}

static
void
gbinder_writer_append_fields(
    GBinderWriter* writer,
    const void* obj,
    const GBinderWriterField* fields,
    const GBinderParent* parent) /* Since 1.1.27 */
{
    if (fields) {
        const GBinderWriterField* field = fields;
        const guint8* base_ptr = obj;
        GBinderParent parent2;

        parent2.index = parent->index;
        for (field = fields; field->type || field->write_buf; field++) {
            const void* field_ptr = base_ptr + field->offset;

            parent2.offset = parent->offset + field->offset;
            if (field->write_buf) {
                field->write_buf(writer, field_ptr, field, &parent2);
            } else {
                gbinder_writer_append_buffer_object_with_parent(writer,
                    *(void**)field_ptr, field->type->size, &parent2);
            }
        }
    }
}

/*
 * Note that gbinder_writer_append_struct doesn't copy the data, it writes
 * buffer objects pointing to whatever was passed in. The caller must make
 * sure that those pointers outlive the transaction. That's most commonly
 * done with by using gbinder_writer_malloc() and friends for allocating
 * memory for the transaction.
 */
void
gbinder_writer_append_struct(
    GBinderWriter* writer,
    const void* ptr,
    const GBinderWriterType* type,
    const GBinderParent* parent) /* Since 1.1.27 */
{
    if (type) {
        GBinderParent child;

        child.offset = 0;
        child.index = gbinder_writer_append_buffer_object_with_parent(writer,
            ptr, type->size, parent);
        gbinder_writer_append_fields(writer, ptr, type->fields, &child);
    } else {
        /* No type - no fields */
        gbinder_writer_append_buffer_object_with_parent(writer, ptr, 0, parent);
    }
}

/*
 * Appends top-level vec<type>. Allocates GBinderHidlVec for that, but
 * unlike gbinder_writer_append_hidl_vec(), doesn't copy the contents
 * of the vector.
 */
void
gbinder_writer_append_struct_vec(
    GBinderWriter* writer,
    const void* ptr,
    guint count,
    const GBinderWriterType* type) /* Since 1.1.29 */
{
    GBinderHidlVec* vec = gbinder_writer_new0(writer, GBinderHidlVec);
    GBinderWriterField vec_f[2];
    GBinderWriterType vec_t;

    memset(vec_f, 0, sizeof(vec_f));
    vec_f->name = "hidl_vec.data.ptr";
    vec_f->type = type;
    vec_f->write_buf = gbinder_writer_field_hidl_vec_write_buf;

    memset(&vec_t, 0, sizeof(vec_t));
    vec_t.name = "hidl_vec";
    vec_t.size = sizeof(GBinderHidlVec);
    vec_t.fields = vec_f;

    vec->owns_buffer = TRUE;
    vec->data.ptr = ptr;
    vec->count = count;

    gbinder_writer_append_struct(writer, vec, &vec_t, NULL);
}

void
gbinder_writer_field_hidl_vec_write_buf(
    GBinderWriter* writer,
    const void* ptr,
    const GBinderWriterField* field,
    const GBinderParent* parent) /* Since 1.1.27 */
{
    const GBinderHidlVec* vec = ptr;
    const guint8* buf = vec->data.ptr;
    const GBinderWriterType* elem_type = field->type;

    if (elem_type) {
        GBinderParent child;
        guint i;

        child.index = gbinder_writer_append_buffer_object_with_parent
            (writer, buf, vec->count * elem_type->size, parent);
        for (i = 0; i < vec->count; i++) {
            child.offset = elem_type->size * i;
            gbinder_writer_append_fields(writer, buf + child.offset,
                elem_type->fields, &child);
        }
    } else {
        /* Probably a programming error but write an empty buffer anyway */
        gbinder_writer_append_buffer_object_with_parent(writer, buf, 0, parent);
    }
}

void
gbinder_writer_field_hidl_string_write_buf(
    GBinderWriter* writer,
    const void* ptr,
    const GBinderWriterField* field,
    const GBinderParent* parent) /* Since 1.1.27 */
{
    const GBinderHidlString* str = ptr;

    gbinder_writer_append_buffer_object_with_parent(writer, str->data.str,
        str->data.str ? (str->len + 1) : 0, parent);
}

guint
gbinder_writer_data_append_buffer_object(
    GBinderWriterData* data,
    const void* ptr,
    gsize size,
    const GBinderParent* parent)
{
    const guint index = data->offsets ? data->offsets->count : 0;
    GByteArray* buf = data->bytes;
    const guint offset = buf->len;
    guint n;

    /* Preallocate enough space */
    g_byte_array_set_size(buf, offset + GBINDER_MAX_BUFFER_OBJECT_SIZE);
    /* Write the object */
    n = data->io->encode_buffer_object(buf->data + offset, ptr, size, parent);
    /* Fix the data size */
    g_byte_array_set_size(buf, offset + n);
    /* Record the offset */
    gbinder_writer_data_record_offset(data, offset);
    /* The driver seems to require each buffer to be 8-byte aligned */
    data->buffers_size += G_ALIGN8(size);
    return index;
}

/*
 * This is supposed to be used to write aidl parcelables, and is not
 * guaranteed to work on any other kind of parcelable.
 */
void
gbinder_writer_append_parcelable(
    GBinderWriter* self,
    const void* buf,
    gsize len) /* Since 1.1.19 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_parcelable(data, buf, len);
    }
}

/*
 * This is compatible with aidl parcelables, and is not guaranteed to work
 * with any other kind of parcelable.
 */
void
gbinder_writer_data_append_parcelable(
    GBinderWriterData* data,
    const void* ptr,
    gsize size)
{
    if (ptr) {
        /* Non-null */
        gbinder_writer_data_append_int32(data, 1);

        /*
         * Write the parcelable size, taking in account the size of this
         * integer as well.
         */
        gbinder_writer_data_append_int32(data, size + sizeof(gint32));

        /* Append the parcelable data */
        g_byte_array_append(data->bytes, ptr, size);
    } else {
        /* Null */
        gbinder_writer_data_append_int32(data, 0);
    }
}

void
gbinder_writer_append_hidl_string(
    GBinderWriter* self,
    const char* str)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_hidl_string(data, str);
    }
}

void
gbinder_writer_append_hidl_string_copy(
    GBinderWriter* self,
    const char* str) /* Since 1.1.13 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        static const char empty[] = "";

        gbinder_writer_data_append_hidl_string(data, str ? (str[0] ?
            gbinder_writer_strdup(self, str) : empty) : NULL);
    }
}

void
gbinder_writer_data_append_hidl_vec(
    GBinderWriterData* data,
    const void* base,
    guint count,
    guint elemsize)
{
    GBinderParent vec_parent;
    GBinderHidlVec* vec = g_new0(GBinderHidlVec, 1);
    const gsize total = count * elemsize;
    void* buf = gutil_memdup(base, total);

    /* Fill in the vector descriptor */
    if (buf) {
        vec->data.ptr = buf;
        vec->count = count;
        data->cleanup = gbinder_cleanup_add(data->cleanup, g_free, buf);
    }
    vec->owns_buffer = TRUE;
    data->cleanup = gbinder_cleanup_add(data->cleanup, g_free, vec);

    /* Every vector, even the one without data, requires two buffer objects */
    vec_parent.offset = GBINDER_HIDL_VEC_BUFFER_OFFSET;
    vec_parent.index = gbinder_writer_data_append_buffer_object(data,
        vec, sizeof(*vec), NULL);
    gbinder_writer_data_append_buffer_object(data, buf, total, &vec_parent);
}

void
gbinder_writer_append_hidl_vec(
    GBinderWriter* self,
    const void* base,
    guint count,
    guint elemsize)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_hidl_vec(data, base, count, elemsize);
    }
}

void
gbinder_writer_data_append_hidl_string(
    GBinderWriterData* data,
    const char* str)
{
    GBinderParent str_parent;
    GBinderHidlString* hidl_string = g_new0(GBinderHidlString, 1);
    const gsize len = str ? strlen(str) : 0;

    /* Fill in the string descriptor and store it */
    hidl_string->data.str = str;
    hidl_string->len = len;
    hidl_string->owns_buffer = TRUE;
    data->cleanup = gbinder_cleanup_add(data->cleanup, g_free, hidl_string);

    /* Write the buffer object pointing to the string descriptor */
    str_parent.offset = GBINDER_HIDL_STRING_BUFFER_OFFSET;
    str_parent.index = gbinder_writer_data_append_buffer_object(data,
        hidl_string, sizeof(*hidl_string), NULL);

    if (str) {
        /* Write the buffer pointing to the string data including the
         * NULL terminator, referencing string descriptor as a parent. */
        gbinder_writer_data_append_buffer_object(data, str, len+1, &str_parent);
        GVERBOSE_("\"%s\" %u %u %u", str, (guint)len, (guint)str_parent.index,
            (guint)data->buffers_size);
    } else {
        gbinder_writer_data_append_buffer_object(data, NULL, 0, &str_parent);
    }
}

void
gbinder_writer_append_hidl_string_vec(
    GBinderWriter* self,
    const char* strv[],
    gssize count)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_hidl_string_vec(data, strv, count);
    }
}

void
gbinder_writer_data_append_hidl_string_vec(
    GBinderWriterData* data,
    const char* strv[],
    gssize count)
{
    GBinderParent vec_parent;
    GBinderHidlVec* vec = g_new0(GBinderHidlVec, 1);
    GBinderHidlString* strings = NULL;
    int i;

    if (count < 0) {
        /* Assume NULL terminated array */
        count = gutil_strv_length((char**)strv);
    }

    /* Fill in the vector descriptor */
    if (count > 0) {
        strings = g_new0(GBinderHidlString, count);
        vec->data.ptr = strings;
        data->cleanup = gbinder_cleanup_add(data->cleanup, g_free, strings);
    }
    vec->count = count;
    vec->owns_buffer = TRUE;
    data->cleanup = gbinder_cleanup_add(data->cleanup, g_free, vec);

    /* Fill in string descriptors */
    for (i = 0; i < count; i++) {
        const char* str = strv[i];
        GBinderHidlString* hidl_str = strings + i;

        if ((hidl_str->data.str = str) != NULL) {
            hidl_str->len = strlen(str);
            hidl_str->owns_buffer = TRUE;
        }
    }

    /* Write the vector object */
    vec_parent.offset = GBINDER_HIDL_VEC_BUFFER_OFFSET;
    vec_parent.index = gbinder_writer_data_append_buffer_object(data,
        vec, sizeof(*vec), NULL);

    if (strings) {
        GBinderParent str_parent;

        /* Prepare parent descriptor for the string data */
        str_parent.index = data->offsets->count;
        str_parent.offset = GBINDER_HIDL_STRING_BUFFER_OFFSET;

        /* Write the vector data (it's parent for the string data) */
        gbinder_writer_data_append_buffer_object(data, strings,
            sizeof(*strings) * count, &vec_parent);

        /* Write the string data */
        for (i = 0; i < count; i++) {
            GBinderHidlString* hidl_str = strings + i;

            if (hidl_str->data.str) {
                gbinder_writer_data_append_buffer_object(data,
                    hidl_str->data.str, hidl_str->len + 1, &str_parent);
                GVERBOSE_("%d. \"%s\" %u %u %u", i + 1, hidl_str->data.str,
                    (guint)hidl_str->len, (guint)str_parent.index,
                    (guint)data->buffers_size);
            } else {
                GVERBOSE_("%d. NULL %u %u %u", i + 1, (guint)hidl_str->len,
                    (guint)str_parent.index, (guint)data->buffers_size);
                gbinder_writer_data_append_buffer_object(data, NULL, 0,
                    &str_parent);
            }
            str_parent.offset += sizeof(GBinderHidlString);
        }
    } else {
        gbinder_writer_data_append_buffer_object(data, NULL, 0, &vec_parent);
    }
}

void
gbinder_writer_append_local_object(
    GBinderWriter* self,
    GBinderLocalObject* obj)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_local_object(data, obj);
    }
}

void
gbinder_writer_data_append_local_object(
    GBinderWriterData* data,
    GBinderLocalObject* obj)
{
    GByteArray* buf = data->bytes;
    const guint offset = buf->len;
    guint n;

    /* Preallocate enough space */
    g_byte_array_set_size(buf, offset + GBINDER_MAX_BINDER_OBJECT_SIZE);
    /* Write the object */
    n = data->io->encode_local_object(buf->data + offset, obj, data->protocol);
    /* Fix the data size */
    g_byte_array_set_size(buf, offset + n);
    /* Record the offset */
    gbinder_writer_data_record_offset(data, offset);
}

void
gbinder_writer_append_remote_object(
    GBinderWriter* self,
    GBinderRemoteObject* obj)
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        gbinder_writer_data_append_remote_object(data, obj);
    }
}

void
gbinder_writer_append_byte_array(
    GBinderWriter* self,
    const void* byte_array,
    gint32 len) /* since 1.0.12 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    GASSERT(len >= 0);
    if (G_LIKELY(data)) {
        GByteArray* buf = data->bytes;
        gsize padded_len;
        guint8* ptr;

        if (!byte_array) {
            len = 0;
        }

        /*
         * Android aligns byte array reads and writes to 4 bytes and
         * pads with 0xFF.
         */
        padded_len = G_ALIGN4(len);
        g_byte_array_set_size(buf, buf->len + sizeof(len) + padded_len);
        ptr = buf->data + (buf->len - sizeof(len) - padded_len);

        if (len > 0) {
            *((gint32*)ptr) = len;
            ptr += sizeof(len);
            memcpy(ptr, byte_array, len);
            /* FF padding */
            if (padded_len > len) {
                memset(ptr + len, 0xff, padded_len - len);
            }
        } else {
            *((gint32*)ptr) = -1;
        }
    }
}

#if GBINDER_FMQ_SUPPORTED

static
void
gbinder_writer_data_append_fmq_descriptor(
    GBinderWriterData* data,
    const GBinderFmq* queue)
{
    GBinderParent parent;
    GBinderMQDescriptor* desc = gbinder_fmq_get_descriptor(queue);
    GBinderMQDescriptor* mqdesc = gutil_memdup(desc,
        sizeof(GBinderMQDescriptor));

    const gsize vec_total =
        desc->grantors.count * sizeof(GBinderFmqGrantorDescriptor);
    void* vec_buf = gutil_memdup(desc->grantors.data.ptr, vec_total);

    const gsize fds_total = sizeof(GBinderFds) +
        sizeof(int) * (desc->data.fds->num_fds + desc->data.fds->num_ints);
    GBinderFds* fds = gutil_memdup(desc->data.fds, fds_total);

    mqdesc->data.fds = fds;
    data->cleanup = gbinder_cleanup_add(data->cleanup, g_free, fds);

    /* Fill in the grantor vector descriptor */
    if (vec_buf) {
        mqdesc->grantors.count = desc->grantors.count;
        mqdesc->grantors.data.ptr = vec_buf;
        mqdesc->grantors.owns_buffer = TRUE;
        data->cleanup = gbinder_cleanup_add(data->cleanup, g_free, vec_buf);
    }
    data->cleanup = gbinder_cleanup_add(data->cleanup, g_free, mqdesc);

    /* Write the FMQ descriptor object */
    parent.index = gbinder_writer_data_append_buffer_object(data,
        mqdesc, sizeof(*mqdesc), NULL);

    /* Write the vector data buffer */
    parent.offset = GBINDER_MQ_DESCRIPTOR_GRANTORS_OFFSET;
    gbinder_writer_data_append_buffer_object(data, vec_buf, vec_total,
        &parent);

    /* Write the fds */
    parent.offset = GBINDER_MQ_DESCRIPTOR_FDS_OFFSET;
    gbinder_writer_data_append_fds(data, mqdesc->data.fds, &parent);
}

void
gbinder_writer_append_fmq_descriptor(
    GBinderWriter* self,
    const GBinderFmq* queue) /* since 1.1.14 */
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data) && G_LIKELY(queue)) {
        gbinder_writer_data_append_fmq_descriptor(data, queue);
    }
}

#endif /* GBINDER_FMQ_SUPPORTED */

void
gbinder_writer_data_append_remote_object(
    GBinderWriterData* data,
    GBinderRemoteObject* obj)
{
    GByteArray* buf = data->bytes;
    const guint offset = buf->len;
    guint n;

    /* Preallocate enough space */
    g_byte_array_set_size(buf, offset + GBINDER_MAX_BINDER_OBJECT_SIZE);
    /* Write the object */
    n = data->io->encode_remote_object(buf->data + offset, obj);
    /* Fix the data size */
    g_byte_array_set_size(buf, offset + n);
    /* Record the offset */
    gbinder_writer_data_record_offset(data, offset);
}

static
void*
gbinder_writer_alloc(
    GBinderWriter* self,
    gsize size,
    gpointer (*alloc)(gsize),
    void (*dealloc)())
{
    GBinderWriterData* data = gbinder_writer_data(self);

    if (G_LIKELY(data)) {
        void* ptr = alloc(size);

        data->cleanup = gbinder_cleanup_add(data->cleanup, dealloc, ptr);
        return ptr;
    }
    return NULL;
}

void*
gbinder_writer_malloc(
    GBinderWriter* self,
    gsize size) /* since 1.0.19 */
{
    return gbinder_writer_alloc(self, size, g_malloc, g_free);
}

void*
gbinder_writer_malloc0(
    GBinderWriter* self,
    gsize size) /* since 1.0.19 */
{
    return gbinder_writer_alloc(self, size, g_malloc0, g_free);
}

char*
gbinder_writer_strdup(
    GBinderWriter* writer,
    const char* str) /* since 1.1.13 */
{
    return str ? gbinder_writer_memdup(writer, str, strlen(str) + 1) : NULL;
}

void*
gbinder_writer_memdup(
    GBinderWriter* self,
    const void* buf,
    gsize size) /* since 1.0.19 */
{
    if (buf) {
        void* ptr = gbinder_writer_malloc(self, size);

        if (ptr) {
            memcpy(ptr, buf, size);
            return ptr;
        }
    }
    return NULL;
}

void
gbinder_writer_add_cleanup(
    GBinderWriter* self,
    GDestroyNotify destroy,
    gpointer ptr) /* since 1.0.19 */
{
    if (G_LIKELY(destroy)) {
        GBinderWriterData* data = gbinder_writer_data(self);

        if (G_LIKELY(data)) {
            data->cleanup = gbinder_cleanup_add(data->cleanup, destroy, ptr);
        }
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
