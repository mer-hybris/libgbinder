/*
 * Copyright (C) 2018-2022 Jolla Ltd.
 * Copyright (C) 2018-2022 Slava Monich <slava.monich@jolla.com>
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

/*
 * Note that gbinder_writer_append_struct() doesn't copy the data, it writes
 * buffer objects pointing to whatever was passed in. The caller must make
 * sure that those pointers outlive the transaction. That's most commonly
 * done with by using gbinder_writer_malloc() and friends for allocating
 * memory for the transaction.
 *
 * Below is an example of initializing GBinderWriterType which can then
 * be passed to gbinder_writer_append_struct(). Fields have to be listed
 * in the order in which they appear in the structure.
 *
 *    typedef struct data {
 *        int x;
 *    } Data;
 *
 *    typedef struct data2 {
 *        int y;
 *        GBinderHidlString str;
 *        GBinderHidlVec vec; // vec<Data>
 *    } Data2;
 *
 *    static const GBinderWriterType data_t = {
 *        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(Data), NULL
 *    };
 *
 *    static const GBinderWriterField data2_f[] = {
 *        GBINDER_WRITER_FIELD_HIDL_STRING(Data2,str),
 *        GBINDER_WRITER_FIELD_HIDL_VEC(Data2, vec, &data_t),
 *        GBINDER_WRITER_FIELD_END()
 *    };
 *
 *    static const GBinderWriterType data2_t = {
 *        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(Data2), data2_f
 *    };
 */

typedef struct gbinder_writer_type {
    const char* name;
    gsize size;
    const struct gbinder_writer_field* fields;
} GBinderWriterType; /* Since 1.1.27 */

typedef struct gbinder_writer_field {
    const char* name;
    gsize offset;
    const GBinderWriterType* type;
    void (*write_buf)(GBinderWriter* writer, const void* ptr,
      const struct gbinder_writer_field* field, const GBinderParent* parent);
    gpointer reserved;
} GBinderWriterField; /* Since 1.1.27 */

#define GBINDER_WRITER_STRUCT_NAME_AND_SIZE(type) \
    #type, sizeof(type)
#define GBINDER_WRITER_FIELD_NAME_AND_OFFSET(type,field) \
    #type "." #field, G_STRUCT_OFFSET(type,field)
#define GBINDER_WRITER_FIELD_POINTER(type,field,field_type) { \
    GBINDER_WRITER_FIELD_NAME_AND_OFFSET(type,field), field_type, NULL, NULL }
#define GBINDER_WRITER_FIELD_HIDL_VEC(type,field,elem) { \
    GBINDER_WRITER_FIELD_NAME_AND_OFFSET(type,field), elem, \
    gbinder_writer_field_hidl_vec_write_buf, NULL }
#define GBINDER_WRITER_FIELD_HIDL_VEC_INT32(type,field) \
    GBINDER_WRITER_FIELD_HIDL_VEC(type,field, &gbinder_writer_type_int32)
#define GBINDER_WRITER_FIELD_HIDL_VEC_BYTE(type,field) \
    GBINDER_WRITER_FIELD_HIDL_VEC(type,field, &gbinder_writer_type_byte)
#define GBINDER_WRITER_FIELD_HIDL_VEC_STRING(type,field) \
    GBINDER_WRITER_FIELD_HIDL_VEC(type,field, &gbinder_writer_type_hidl_string)
#define GBINDER_WRITER_FIELD_HIDL_STRING(type,field) { \
    GBINDER_WRITER_FIELD_NAME_AND_OFFSET(type,field), NULL, \
    gbinder_writer_field_hidl_string_write_buf, NULL }
#define GBINDER_WRITER_FIELD_END() { NULL, 0, NULL, NULL, NULL }

extern const GBinderWriterType gbinder_writer_type_byte; /* Since 1.1.27 */
extern const GBinderWriterType gbinder_writer_type_int32; /* Since 1.1.27 */
extern const GBinderWriterType gbinder_writer_type_hidl_string; /* 1.1.27 */

void
gbinder_writer_append_struct(
    GBinderWriter* writer,
    const void* ptr,
    const GBinderWriterType* type,
    const GBinderParent* parent); /* Since 1.1.27 */

void
gbinder_writer_append_struct_vec(
    GBinderWriter* writer,
    const void* ptr,
    guint count,
    const GBinderWriterType* type); /* Since 1.1.29 */

void
gbinder_writer_field_hidl_vec_write_buf(
    GBinderWriter* writer,
    const void* ptr,
    const GBinderWriterField* field,
    const GBinderParent* parent); /* Since 1.1.27 */

void
gbinder_writer_field_hidl_string_write_buf(
    GBinderWriter* writer,
    const void* ptr,
    const GBinderWriterField* field,
    const GBinderParent* parent); /* Since 1.1.27 */

void
gbinder_writer_append_int8(
    GBinderWriter* writer,
    guint8 value); /* Since 1.1.15 */

void
gbinder_writer_append_int16(
    GBinderWriter* writer,
    guint16 value); /* Since 1.1.15 */

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
gbinder_writer_append_string16_utf16(
    GBinderWriter* writer,
    const gunichar2* utf16,
    gssize length); /* Since 1.0.17 */

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

void
gbinder_writer_append_fd(
    GBinderWriter* writer,
    int fd); /* Since 1.0.18 */

void
gbinder_writer_append_fds(
    GBinderWriter* writer,
    const GBinderFds* fds,
    const GBinderParent* parent); /* Since 1.1.14 */

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
gbinder_writer_append_parcelable(
    GBinderWriter* writer,
    const void* buf,
    gsize len); /* Since 1.1.19 */

void
gbinder_writer_append_hidl_vec(
    GBinderWriter* writer,
    const void* base,
    guint count,
    guint elemsize); /* Since 1.0.8 */

void
gbinder_writer_append_hidl_string(
    GBinderWriter* writer,
    const char* str);

void
gbinder_writer_append_hidl_string_copy(
    GBinderWriter* writer,
    const char* str); /* Since 1.1.13 */

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

void
gbinder_writer_append_byte_array(
    GBinderWriter* writer,
    const void* byte_array,
    gint32 len); /* Since 1.0.12 */

void
gbinder_writer_append_fmq_descriptor(
    GBinderWriter* writer,
    const GBinderFmq* queue); /* since 1.1.14 */

const void*
gbinder_writer_get_data(
    GBinderWriter* writer,
    gsize* size); /* Since 1.1.14 */

gsize
gbinder_writer_bytes_written(
    GBinderWriter* writer); /* Since 1.0.21 */

void
gbinder_writer_overwrite_int32(
    GBinderWriter* writer,
    gsize offset,
    gint32 value); /* Since 1.0.21 */

/* Note: memory allocated by GBinderWriter is owned by GBinderWriter */

void*
gbinder_writer_malloc(
    GBinderWriter* writer,
    gsize size); /* Since 1.0.19 */

void*
gbinder_writer_malloc0(
    GBinderWriter* writer,
    gsize size); /* Since 1.0.19 */

#define gbinder_writer_new(writer,type) \
    ((type*) gbinder_writer_malloc(writer, sizeof(type)))

#define gbinder_writer_new0(writer,type) \
    ((type*) gbinder_writer_malloc0(writer, sizeof(type)))

void
gbinder_writer_add_cleanup(
    GBinderWriter* writer,
    GDestroyNotify destroy,
    gpointer data); /* Since 1.0.19 */

void*
gbinder_writer_memdup(
    GBinderWriter* writer,
    const void* buf,
    gsize size); /* Since 1.0.19 */

char*
gbinder_writer_strdup(
    GBinderWriter* writer,
    const char* str); /* Since 1.1.13 */

G_END_DECLS

#endif /* GBINDER_WRITER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
