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

#include "test_common.h"

#include "gbinder_local_request_p.h"
#include "gbinder_output_data.h"
#include "gbinder_writer_p.h"
#include "gbinder_io.h"

#include <gutil_intarray.h>

static TestOpt test_opt;

#define BUFFER_OBJECT_SIZE_32 (24)
#define BUFFER_OBJECT_SIZE_64 (GBINDER_MAX_BUFFER_OBJECT_SIZE)
#define BINDER_OBJECT_SIZE_32 (16)
#define BINDER_OBJECT_SIZE_64 (GBINDER_MAX_BINDER_OBJECT_SIZE)

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    GBinderWriter writer;

    gbinder_local_request_init_writer(NULL, &writer);
    gbinder_writer_append_int32(NULL, 0);
    gbinder_writer_append_int32(&writer, 0);
    gbinder_writer_append_int64(NULL, 0);
    gbinder_writer_append_int64(&writer, 0);
    gbinder_writer_append_float(NULL, 0);
    gbinder_writer_append_float(&writer, 0);
    gbinder_writer_append_double(NULL, 0);
    gbinder_writer_append_double(&writer, 0);
    gbinder_writer_append_string8(NULL, NULL);
    gbinder_writer_append_string8(&writer, NULL);
    gbinder_writer_append_string8_len(NULL, NULL, 0);
    gbinder_writer_append_string8_len(&writer, NULL, 0);
    gbinder_writer_append_string16(NULL, NULL);
    gbinder_writer_append_string16(&writer, NULL);
    gbinder_writer_append_string16_len(NULL, NULL, 0);
    gbinder_writer_append_string16_len(&writer, NULL, 0);
    gbinder_writer_append_bool(NULL, FALSE);
    gbinder_writer_append_bool(&writer, FALSE);
    gbinder_writer_append_bytes(NULL, NULL, 0);
    gbinder_writer_append_bytes(&writer, NULL, 0);
    gbinder_writer_append_hidl_vec(NULL, NULL, 0, 0);
    gbinder_writer_append_hidl_string(NULL, NULL);
    gbinder_writer_append_hidl_string(&writer, NULL);
    gbinder_writer_append_hidl_string_vec(NULL, NULL, 0);
    gbinder_writer_append_hidl_string_vec(&writer, NULL, 0);
    gbinder_writer_append_buffer_object(NULL, NULL, 0);
    gbinder_writer_append_buffer_object(&writer, NULL, 0);
    gbinder_writer_append_buffer_object_with_parent(NULL, NULL, 0, NULL);
    gbinder_writer_append_buffer_object_with_parent(&writer, NULL, 0, NULL);
    gbinder_writer_append_local_object(NULL, NULL);
    gbinder_writer_append_local_object(&writer, NULL);
    gbinder_writer_append_remote_object(NULL, NULL);
    gbinder_writer_append_remote_object(&writer, NULL);
    gbinder_writer_append_byte_array(NULL, NULL, 0);
    gbinder_writer_append_byte_array(&writer, NULL, 0);

    g_assert(!gbinder_output_data_offsets(NULL));
    g_assert(!gbinder_output_data_buffers_size(NULL));
}

/*==========================================================================*
 * int32
 *==========================================================================*/

static
void
test_int32(
    void)
{
    const guint32 value = 1234567;
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_int32(&writer, value);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * int64
 *==========================================================================*/

static
void
test_int64(
    void)
{
    const guint64 value = 12345678;
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_int64(&writer, value);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * float
 *==========================================================================*/

static
void
test_float(
    void)
{
    const gfloat value = 12345678;
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_float(&writer, value);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * double
 *==========================================================================*/

static
void
test_double(
    void)
{
    const gdouble value = 12345678;
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_double(&writer, value);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * bool
 *==========================================================================*/

static
void
test_bool(
    void)
{
    const char encoded[] = {
        0x00, 0xff, 0xff, 0xff,
        0x01, 0xff, 0xff, 0xff,
        0x01, 0xff, 0xff, 0xff
    };
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_bool(&writer, FALSE);
    gbinder_writer_append_bool(&writer, TRUE);
    gbinder_writer_append_bool(&writer, 2); /* Will be normalized */

    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(encoded));
    g_assert(!memcmp(data->bytes->data, encoded, data->bytes->len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * bytes
 *==========================================================================*/

static
void
test_bytes(
    void)
{
    const char value[] = { 0x01, 0x02, 0x03 };
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_bytes(&writer, value, sizeof(value));
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, value, data->bytes->len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * string8
 *==========================================================================*/

static
void
test_string8(
    void)
{
    /* The size of the string is aligned at 4-byte boundary */
    static const char value[] = { 't', 'e', 's', 't', 0, 0, 0, 0 };
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_string8(&writer, value);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, value, data->bytes->len));
    gbinder_local_request_unref(req);

    /* NULL string writes nothing */
    req = gbinder_local_request_new(&gbinder_io_32, NULL);
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_string8(&writer, NULL);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(!data->bytes->len);
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * string16
 *==========================================================================*/

typedef struct test_string16_data {
    const char* name;
    const char* input;
    const guint8* output;
    guint output_len;
} TestString16Data;

static const guint8 string16_tests_data_null[] = {
    TEST_INT32_BYTES(-1)
};

static const guint8 string16_tests_data_empty[] = {
    TEST_INT32_BYTES(0),
    0x00, 0x00, 0xff, 0xff
};

static const guint8 string16_tests_data_x[] = {
    TEST_INT32_BYTES(1),
    TEST_INT16_BYTES('x'), 0x00, 0x00
};

static const guint8 string16_tests_data_xy[] = {
    TEST_INT32_BYTES(2),
    TEST_INT16_BYTES('x'), TEST_INT16_BYTES('y'),
    0x00, 0x00, 0x00, 0x00
};

static const TestString16Data test_string16_tests[] = {
    { "null", NULL, TEST_ARRAY_AND_SIZE(string16_tests_data_null) },
    { "empty", "", TEST_ARRAY_AND_SIZE(string16_tests_data_empty) },
    { "1", "x", TEST_ARRAY_AND_SIZE(string16_tests_data_x) },
    { "2", "xy", TEST_ARRAY_AND_SIZE(string16_tests_data_xy) }
};

static
void
test_string16(
    gconstpointer test_data)
{
    const TestString16Data* test = test_data;
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_string16(&writer, test->input);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == test->output_len);
    g_assert(!memcmp(data->bytes->data, test->output, test->output_len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * hidl_vec
 *==========================================================================*/

typedef struct test_hidl_vec_data {
    const char* name;
    const GBinderIo* io;
    const void* data;
    const gsize count;
    const gsize elemsize;
    const guint* offsets;
    guint offsets_count;
    guint buffers_size;
} TestHidlVecData;

static guint test_hidl_vec_offsets_0[] =
    { 0 };
static guint test_hidl_vec_offsets_32[] =
    { 0, BUFFER_OBJECT_SIZE_32 };
static guint test_hidl_vec_offsets_64[] =
    { 0, BUFFER_OBJECT_SIZE_64 };

static const TestHidlVecData test_hidl_vec_tests[] = {
    { "32/null", &gbinder_io_32, NULL, 0, 0,
      TEST_ARRAY_AND_COUNT(test_hidl_vec_offsets_0), sizeof(HidlVec) },
    { "32/2x1", &gbinder_io_32, "xy", 2, 1,
      TEST_ARRAY_AND_COUNT(test_hidl_vec_offsets_32),
      sizeof(HidlVec) + 8 /* vec data aligned at 8 bytes boundary */ },
    { "64/null", &gbinder_io_64, NULL, 0, 0,
      TEST_ARRAY_AND_COUNT(test_hidl_vec_offsets_0), sizeof(HidlVec) },
    { "64/2x2", &gbinder_io_64, "xxyy", 2, 2,
      TEST_ARRAY_AND_COUNT(test_hidl_vec_offsets_64),
      sizeof(HidlVec) + 8 /* vec data aligned at 8 bytes boundary */ }
};

static
void
test_hidl_vec(
    gconstpointer test_data)
{
    const TestHidlVecData* test = test_data;
    GBinderLocalRequest* req = gbinder_local_request_new(test->io, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;
    guint i;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_hidl_vec(&writer, test->data,
        test->count, test->elemsize);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == test->offsets_count);
    for (i = 0; i < offsets->count; i++) {
        g_assert(offsets->data[i] == test->offsets[i]);
    }
    g_assert(gbinder_output_data_buffers_size(data) == test->buffers_size);
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * hidl_string
 *==========================================================================*/

typedef struct test_hidl_string_data {
    const char* name;
    const GBinderIo* io;
    const char* str;
    const guint* offsets;
    guint offsets_count;
    guint buffers_size;
} TestHidlStringData;

static guint test_hidl_string_offsets_0[] =
    { 0 };
static guint test_hidl_string_offsets_32[] =
    { 0, BUFFER_OBJECT_SIZE_32 };
static guint test_hidl_string_offsets_64[] =
    { 0, BUFFER_OBJECT_SIZE_64 };

static const TestHidlStringData test_hidl_string_tests[] = {
    { "32/null", &gbinder_io_32, NULL,
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_0), sizeof(HidlString) },
    { "32/xxx", &gbinder_io_32, "xxx",
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_32),
      sizeof(HidlString) + 8 /* string data aligned at 8 bytes boundary */ },
    { "64/null", &gbinder_io_64, NULL,
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_0), sizeof(HidlString) },
    { "64/xxxxxxx", &gbinder_io_64, "xxxxxxx",
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_64),
      sizeof(HidlString) + 8 /* string data aligned at 8 bytes boundary */ }
};

static
void
test_hidl_string(
    gconstpointer test_data)
{
    const TestHidlStringData* test = test_data;
    GBinderLocalRequest* req = gbinder_local_request_new(test->io, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;
    guint i;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_hidl_string(&writer, test->str);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == test->offsets_count);
    for (i = 0; i < offsets->count; i++) {
        g_assert(offsets->data[i] == test->offsets[i]);
    }
    g_assert(gbinder_output_data_buffers_size(data) == test->buffers_size);
    gbinder_local_request_unref(req);
}

static
void
test_hidl_string2(
    void)
{
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_hidl_string(&writer, "foo");
    gbinder_writer_append_hidl_string(&writer, NULL);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 3);
    g_assert(offsets->data[0] == 0);
    g_assert(offsets->data[1] == BUFFER_OBJECT_SIZE_32);
    g_assert(offsets->data[2] == 2*BUFFER_OBJECT_SIZE_32);
    /* 2 HidlStrings + "foo" aligned at 8 bytes boundary */
    g_assert(gbinder_output_data_buffers_size(data) == 2*sizeof(HidlString)+8);

    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * hidl_string_vec
 *==========================================================================*/

typedef struct test_hidl_string_vec_data {
    const char* name;
    const GBinderIo* io;
    const char** vec;
    int count;
    const guint* offsets;
    guint offsets_count;
    guint buffers_size;
} TestHidlStringVecData;

static char* test_hidl_string_vec_data_1[] = { "test" };

static guint test_hidl_string_vec_offsets_empty[] =
    { 0 };
static guint test_hidl_string_vec_offsets_1_32[] =
    { 0, BUFFER_OBJECT_SIZE_32, 2*BUFFER_OBJECT_SIZE_32 };
static guint test_hidl_string_vec_offsets_1_64[] =
    { 0, BUFFER_OBJECT_SIZE_64, 2*BUFFER_OBJECT_SIZE_64 };

static const TestHidlStringVecData test_hidl_string_vec_tests[] = {
    { "32/null", &gbinder_io_32, NULL, -1,
      TEST_ARRAY_AND_COUNT(test_hidl_string_vec_offsets_empty),
      sizeof(HidlVec) },
    { "32/1", &gbinder_io_32,
      (const char**)TEST_ARRAY_AND_COUNT(test_hidl_string_vec_data_1),
      TEST_ARRAY_AND_COUNT(test_hidl_string_vec_offsets_1_32),
      sizeof(HidlVec) + sizeof(HidlString) + 8 },
    { "64/null", &gbinder_io_64, NULL, -1,
      TEST_ARRAY_AND_COUNT(test_hidl_string_vec_offsets_empty),
      sizeof(HidlVec) },
    { "64/1", &gbinder_io_64,
      (const char**)TEST_ARRAY_AND_COUNT(test_hidl_string_vec_data_1),
      TEST_ARRAY_AND_COUNT(test_hidl_string_vec_offsets_1_64),
      sizeof(HidlVec) + sizeof(HidlString) + 8 },
};

static
void
test_hidl_string_vec(
    gconstpointer test_data)
{
    const TestHidlStringVecData* test = test_data;
    GBinderLocalRequest* req = gbinder_local_request_new(test->io, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;
    guint i;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_hidl_string_vec(&writer, (const char**)test->vec,
        test->count);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == test->offsets_count);
    for (i = 0; i < offsets->count; i++) {
        g_assert(offsets->data[i] == test->offsets[i]);
    }
    g_assert(gbinder_output_data_buffers_size(data) == test->buffers_size);
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * buffer
 *==========================================================================*/

typedef struct test_data {
    guint32 x;
} TestData;

static
void
test_buffer(
    void)
{
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    guint32 x1 = 1;
    guint32 x2 = 2;
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_buffer_object(&writer, &x1, sizeof(x1));
    gbinder_writer_append_buffer_object(&writer, &x2, sizeof(x2));

    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 2);
    g_assert(offsets->data[0] == 0);
    g_assert(offsets->data[1] == BUFFER_OBJECT_SIZE_32);
    /* Each buffer is aligned at 8 bytes boundary */
    g_assert(gbinder_output_data_buffers_size(data) == 16);

    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * parent
 *==========================================================================*/

typedef struct test_data_pointer {
    TestData* ptr;
} TestDataPointer;

static
void
test_parent(
    void)
{
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    TestData test_data;
    TestDataPointer test;
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;
    GBinderParent parent;

    test_data.x = 1;
    test.ptr = &test_data;

    gbinder_local_request_init_writer(req, &writer);
    parent.index = gbinder_writer_append_buffer_object(&writer, &test,
        sizeof(test));
    parent.offset = 0;
    g_assert(parent.index == 0);
    gbinder_writer_append_buffer_object_with_parent(&writer, &test_data,
        sizeof(test_data), &parent);

    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 2);
    g_assert(offsets->data[0] == 0);
    g_assert(offsets->data[1] == BUFFER_OBJECT_SIZE_32);
    /* Each buffer is aligned at 8 bytes boundary */
    g_assert(gbinder_output_data_buffers_size(data) == 16);

    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * local_object
 *==========================================================================*/

static
void
test_local_object(
    void)
{
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GUtilIntArray* offsets;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_local_object(&writer, NULL);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 0);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == BINDER_OBJECT_SIZE_32);
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * remote_object
 *==========================================================================*/

static
void
test_remote_object(
    void)
{
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_64, NULL);
    GBinderOutputData* data;
    GUtilIntArray* offsets;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_remote_object(&writer, NULL);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 0);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == BINDER_OBJECT_SIZE_64);
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * byte_array
 *==========================================================================*/

static
void
test_byte_array(
    void)
{
    GBinderLocalRequest* req;
    GBinderOutputData* data;
    GBinderWriter writer;

    const char in_data[] = "abcd1234";
    gint32 in_len = sizeof(in_data) - 1;
    gint32 null_len = -1;

    /* test for NULL byte array with non-zero len */
    req = gbinder_local_request_new(&gbinder_io_64, NULL);
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_byte_array(&writer, NULL, 42);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(gint32));
    g_assert(!memcmp(data->bytes->data, &null_len, data->bytes->len));
    gbinder_local_request_unref(req);

    /* test for valid array with zero len */
    req = gbinder_local_request_new(&gbinder_io_64, NULL);
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_byte_array(&writer, in_data, 0);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(gint32));
    g_assert(!memcmp(data->bytes->data, &null_len, data->bytes->len));
    gbinder_local_request_unref(req);

    /* test for valid array with correct len */
    req = gbinder_local_request_new(&gbinder_io_64, NULL);
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_byte_array(&writer, in_data, in_len);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(in_len) + in_len);
    g_assert(!memcmp(data->bytes->data, &in_len, sizeof(in_len)));
    g_assert(!memcmp(data->bytes->data + sizeof(in_len), in_data, in_len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/writer/"

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "null", test_null);
    g_test_add_func(TEST_PREFIX "int32", test_int32);
    g_test_add_func(TEST_PREFIX "int64", test_int64);
    g_test_add_func(TEST_PREFIX "float", test_float);
    g_test_add_func(TEST_PREFIX "double", test_double);
    g_test_add_func(TEST_PREFIX "bool", test_bool);
    g_test_add_func(TEST_PREFIX "bytes", test_bytes);
    g_test_add_func(TEST_PREFIX "string8", test_string8);

    for (i = 0; i < G_N_ELEMENTS(test_string16_tests); i++) {
        const TestString16Data* test = test_string16_tests + i;
        char* path = g_strconcat(TEST_PREFIX "string16/", test->name, NULL);

        g_test_add_data_func(path, test, test_string16);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_hidl_vec_tests); i++) {
        const TestHidlVecData* test = test_hidl_vec_tests + i;
        char* path = g_strconcat(TEST_PREFIX "hidl_vec/", test->name, NULL);

        g_test_add_data_func(path, test, test_hidl_vec);
        g_free(path);
    }

    g_test_add_func(TEST_PREFIX "hidl_string/2strings", test_hidl_string2);
    for (i = 0; i < G_N_ELEMENTS(test_hidl_string_tests); i++) {
        const TestHidlStringData* test = test_hidl_string_tests + i;
        char* path = g_strconcat(TEST_PREFIX "hidl_string/", test->name, NULL);

        g_test_add_data_func(path, test, test_hidl_string);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_hidl_string_vec_tests); i++) {
        const TestHidlStringVecData* test = test_hidl_string_vec_tests + i;
        char* path = g_strconcat(TEST_PREFIX "hidl_string_vec/",
            test->name, NULL);

        g_test_add_data_func(path, test, test_hidl_string_vec);
        g_free(path);
    }

    g_test_add_func(TEST_PREFIX "buffer", test_buffer);
    g_test_add_func(TEST_PREFIX "parent", test_parent);
    g_test_add_func(TEST_PREFIX "local_object", test_local_object);
    g_test_add_func(TEST_PREFIX "remote_object", test_remote_object);
    g_test_add_func(TEST_PREFIX "byte_array", test_byte_array);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
