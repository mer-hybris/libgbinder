/*
 * Copyright (C) 2018-2024 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2022 Jolla Ltd.
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

#include "test_common.h"
#include "test_binder.h"

#include "gbinder_buffer_p.h"
#include "gbinder_config.h"
#include "gbinder_fmq_p.h"
#include "gbinder_local_request_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_output_data.h"
#include "gbinder_reader_p.h"
#include "gbinder_writer_p.h"
#include "gbinder_ipc.h"
#include "gbinder_io.h"

#include <gutil_intarray.h>
#include <gutil_macros.h>
#include <gutil_misc.h>
#include <gutil_log.h>

#include <unistd.h>
#include <errno.h>

static TestOpt test_opt;
static const char TMP_DIR_TEMPLATE[] = "gbinder-test-writer-XXXXXX";

static
GBinderLocalRequest*
test_local_request_new_with_io(
    const GBinderIo* io)
{
    return gbinder_local_request_new(io,
      gbinder_rpc_protocol_for_device(GBINDER_DEFAULT_BINDER), NULL);
}

static
GBinderLocalRequest*
test_local_request_new()
{
    return test_local_request_new_with_io(&gbinder_io_32);
}

static
GBinderLocalRequest*
test_local_request_new_64()
{
    return test_local_request_new_with_io(&gbinder_io_64);
}

/*==========================================================================*
 * Test context
 *==========================================================================*/

typedef struct test_context {
    TestConfig config;
    char* config_file;
} TestContext;

static
void
test_context_init(
    TestContext* test,
    const char* prot)
{
    memset(test, 0, sizeof(*test));
    test_config_init(&test->config, TMP_DIR_TEMPLATE);
    if (prot) {
        char* config = g_strdup_printf("[Protocol]\n"
            GBINDER_DEFAULT_BINDER " = %s", prot);

        test->config_file = g_build_filename(test->config.config_dir,
            "test.conf", NULL);
        GDEBUG("Config file %s", test->config_file);
        g_assert(g_file_set_contents(test->config_file, config, -1, NULL));
        g_free(config);
        gbinder_config_file = test->config_file;
    }
}

static
void
test_context_deinit(
    TestContext* test)
{
    if (test->config_file) {
        remove(test->config_file);
        g_free(test->config_file);
    }
    test_config_cleanup(&test->config);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    GBinderWriter writer;
    gsize size = 1;

    gbinder_local_request_init_writer(NULL, &writer);
    gbinder_writer_append_int8(NULL, 0);
    gbinder_writer_append_int8(&writer, 0);
    gbinder_writer_append_int16(NULL, 0);
    gbinder_writer_append_int16(&writer, 0);
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
    gbinder_writer_append_string16_utf16(NULL, NULL, 0);
    gbinder_writer_append_bool(NULL, FALSE);
    gbinder_writer_append_bool(&writer, FALSE);
    gbinder_writer_append_fd(NULL, 0);
    gbinder_writer_append_bytes(NULL, NULL, 0);
    gbinder_writer_append_bytes(&writer, NULL, 0);
    gbinder_writer_append_hidl_vec(NULL, NULL, 0, 0);
    gbinder_writer_append_hidl_string(NULL, NULL);
    gbinder_writer_append_hidl_string_copy(NULL, NULL);
    gbinder_writer_append_hidl_string(&writer, NULL);
    gbinder_writer_append_hidl_string_vec(NULL, NULL, 0);
    gbinder_writer_append_hidl_string_vec(&writer, NULL, 0);
    gbinder_writer_append_buffer_object(NULL, NULL, 0);
    gbinder_writer_append_buffer_object(&writer, NULL, 0);
    gbinder_writer_append_buffer_object_with_parent(NULL, NULL, 0, NULL);
    gbinder_writer_append_buffer_object_with_parent(&writer, NULL, 0, NULL);
    gbinder_writer_append_parcelable(NULL, NULL, 0);
    gbinder_writer_append_local_object(NULL, NULL);
    gbinder_writer_append_local_object(&writer, NULL);
    gbinder_writer_append_remote_object(NULL, NULL);
    gbinder_writer_append_remote_object(&writer, NULL);
    gbinder_writer_append_byte_array(NULL, NULL, 0);
    gbinder_writer_append_byte_array(&writer, NULL, 0);
    gbinder_writer_add_cleanup(NULL, NULL, 0);
    gbinder_writer_add_cleanup(NULL, g_free, 0);
    gbinder_writer_overwrite_int32(NULL, 0, 0);

#if GBINDER_FMQ_SUPPORTED
    gbinder_writer_append_fmq_descriptor(NULL, NULL);
    gbinder_writer_append_fmq_descriptor(&writer, NULL);
#endif

    g_assert(!gbinder_writer_bytes_written(NULL));
    g_assert(!gbinder_writer_get_data(NULL, NULL));
    g_assert(!gbinder_writer_get_data(NULL, &size));
    g_assert_cmpuint(size, ==, 0);
    g_assert(!gbinder_output_data_offsets(NULL));
    g_assert(!gbinder_output_data_buffers_size(NULL));
    g_assert(!gbinder_writer_malloc(NULL, 0));
    g_assert(!gbinder_writer_malloc0(NULL, 0));
    g_assert(!gbinder_writer_memdup(&writer, NULL, 0));
    g_assert(!gbinder_writer_memdup(NULL, &writer, 0));
    g_assert(!gbinder_writer_strdup(&writer, NULL));
    g_assert(!gbinder_writer_strdup(NULL, ""));
}

/*==========================================================================*
 * cleanup
 *==========================================================================*/

static
void
test_cleanup_fn(
    gpointer ptr)
{
    (*((int*)ptr))++;
}

static
void
test_cleanup(
    void)
{
    GBinderLocalRequest* req = test_local_request_new();
    GBinderWriter writer;
    const int value = 42;
    const char* str = "foo";
    int cleanup_count = 0;
    int* zero;
    int* copy;
    char* scopy;

    gbinder_local_request_init_writer(req, &writer);
    zero = gbinder_writer_new0(&writer, int);
    copy = gbinder_writer_memdup(&writer, &value, sizeof(value));
    scopy = gbinder_writer_strdup(&writer, str);
    g_assert_cmpint(*zero, == ,0);
    g_assert_cmpint(*copy, == ,value);
    g_assert_cmpstr(scopy, == ,str);
    gbinder_writer_add_cleanup(&writer, test_cleanup_fn, &cleanup_count);
    gbinder_writer_add_cleanup(&writer, test_cleanup_fn, &cleanup_count);
    gbinder_local_request_unref(req);
    g_assert_cmpint(cleanup_count, == ,2);
}

/*==========================================================================*
 * int8
 *==========================================================================*/

static
void
test_int8(
    void)
{
    const char encoded[] = { 0x80, 0x00, 0x00, 0x00 };
    GBinderLocalRequest* req = test_local_request_new();
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_int8(&writer, 0x80);

    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert_cmpuint(data->bytes->len, == ,sizeof(encoded));
    g_assert(!memcmp(data->bytes->data, encoded, data->bytes->len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * int16
 *==========================================================================*/

static
void
test_int16(
    void)
{
    const char encoded[] = { TEST_INT16_BYTES(0x80ff), 0x00, 0x00 };
    GBinderLocalRequest* req = test_local_request_new();
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_int16(&writer, 0x80ff);

    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert_cmpuint(data->bytes->len, == ,sizeof(encoded));
    g_assert(!memcmp(data->bytes->data, encoded, data->bytes->len));
    gbinder_local_request_unref(req);
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
    GBinderLocalRequest* req = test_local_request_new();
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_int32(&writer, value);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));

    const guint32 value2 = 2345678;
    gbinder_writer_overwrite_int32(&writer, 0, value2);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value2));
    g_assert(!memcmp(data->bytes->data, &value2, data->bytes->len));

    // test overlap over the end of the buffer
    gbinder_writer_overwrite_int32(&writer, 2, value2);
    g_assert(data->bytes->len == sizeof(value2));

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
    GBinderLocalRequest* req = test_local_request_new();
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
    GBinderLocalRequest* req = test_local_request_new();
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
    GBinderLocalRequest* req = test_local_request_new();
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
        TEST_INT8_BYTES_4(0),
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00
    };
    GBinderLocalRequest* req = test_local_request_new();
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
    GBinderLocalRequest* req = test_local_request_new();
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
    GBinderLocalRequest* req = test_local_request_new();
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
    req = test_local_request_new();
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

static const guint8 string16_tests_data_surrogates[] = {
    TEST_INT32_BYTES(8),
    TEST_INT16_BYTES(0xd83d), TEST_INT16_BYTES(0xde00),
    TEST_INT16_BYTES(0xd83d), TEST_INT16_BYTES(0xde01),
    TEST_INT16_BYTES(0xd83d), TEST_INT16_BYTES(0xde02),
    TEST_INT16_BYTES(0xd83d), TEST_INT16_BYTES(0xde03),
    0x00, 0x00, 0x00, 0x00
};

static const TestString16Data test_string16_tests[] = {
    { "null", NULL, TEST_ARRAY_AND_SIZE(string16_tests_data_null) },
    { "empty", "", TEST_ARRAY_AND_SIZE(string16_tests_data_empty) },
    { "1", "x", TEST_ARRAY_AND_SIZE(string16_tests_data_x) },
    { "2", "xy", TEST_ARRAY_AND_SIZE(string16_tests_data_xy) },
    { "surrogates", "\xF0\x9F\x98\x80" "\xF0\x9F\x98\x81"
      "\xF0\x9F\x98\x82" "\xF0\x9F\x98\x83",
      TEST_ARRAY_AND_SIZE(string16_tests_data_surrogates) }
};

static
void
test_string16(
    gconstpointer test_data)
{
    const TestString16Data* test = test_data;
    GBinderLocalRequest* req = test_local_request_new();
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_string16(&writer, test->input);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert_cmpuint(data->bytes->len, == ,test->output_len);
    g_assert(!memcmp(data->bytes->data, test->output, test->output_len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * utf16
 *==========================================================================*/

typedef struct test_utf16_data {
    const char* name;
    const gunichar2* in;
    gssize in_len;
    const guint8* out;
    gssize out_len;
} TestUtf16Data;

static const guint8 utf16_tests_data_null[] = {
    TEST_INT32_BYTES(-1)
};

static const guint8 utf16_tests_data_empty[] = {
    TEST_INT32_BYTES(0),
    0x00, 0x00, 0xff, 0xff
};

static const guint8 utf16_tests_data_x[] = {
    TEST_INT32_BYTES(1),
    TEST_INT16_BYTES('x'), 0x00, 0x00
};

static const guint8 utf16_tests_data_xy[] = {
    TEST_INT32_BYTES(2),
    TEST_INT16_BYTES('x'), TEST_INT16_BYTES('y'),
    0x00, 0x00, 0x00, 0x00
};

static const gunichar2 utf16_tests_input_empty[] = { 0 };
static const gunichar2 utf16_tests_input_x[] = { 'x', 0 };
static const gunichar2 utf16_tests_input_xy[] = { 'x', 'y', 0 };

static const TestUtf16Data test_utf16_tests[] = {
    { "null", NULL, -1,
      TEST_ARRAY_AND_SIZE(utf16_tests_data_null) },
    { "empty", utf16_tests_input_empty, -1,
      TEST_ARRAY_AND_SIZE(utf16_tests_data_empty) },
    { "1", utf16_tests_input_x, -1,
      TEST_ARRAY_AND_SIZE(utf16_tests_data_x) },
    { "2", utf16_tests_input_xy, 1,
      TEST_ARRAY_AND_SIZE(utf16_tests_data_x) },
    { "3", utf16_tests_input_xy, -1,
      TEST_ARRAY_AND_SIZE(utf16_tests_data_xy) }
};

static
void
test_utf16(
    gconstpointer test_data)
{
    const TestUtf16Data* test = test_data;
    GBinderLocalRequest* req = test_local_request_new();
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_string16_utf16(&writer, test->in, test->in_len);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == test->out_len);
    g_assert(!memcmp(data->bytes->data, test->out, test->out_len));
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

static guint test_hidl_vec_offsets_32[] =
    { 0, BUFFER_OBJECT_SIZE_32 };
static guint test_hidl_vec_offsets_64[] =
    { 0, BUFFER_OBJECT_SIZE_64 };

static const TestHidlVecData test_hidl_vec_tests[] = {
    { "32/null", &gbinder_io_32, NULL, 0, 0,
      TEST_ARRAY_AND_COUNT(test_hidl_vec_offsets_32), sizeof(GBinderHidlVec) },
    { "32/2x1", &gbinder_io_32, "xy", 2, 1,
      TEST_ARRAY_AND_COUNT(test_hidl_vec_offsets_32),
      sizeof(GBinderHidlVec) + 8 /* vec data aligned at 8 bytes boundary */ },
    { "64/null", &gbinder_io_64, NULL, 0, 0,
      TEST_ARRAY_AND_COUNT(test_hidl_vec_offsets_64), sizeof(GBinderHidlVec) },
    { "64/2x2", &gbinder_io_64, "xxyy", 2, 2,
      TEST_ARRAY_AND_COUNT(test_hidl_vec_offsets_64),
      sizeof(GBinderHidlVec) + 8 /* vec data aligned at 8 bytes boundary */ }
};

static
void
test_hidl_vec(
    gconstpointer test_data)
{
    const TestHidlVecData* test = test_data;
    GBinderLocalRequest* req = test_local_request_new_with_io(test->io);
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

static guint test_hidl_string_offsets_32[] =
    { 0, BUFFER_OBJECT_SIZE_32 };
static guint test_hidl_string_offsets_64[] =
    { 0, BUFFER_OBJECT_SIZE_64 };

static const TestHidlStringData test_hidl_string_tests[] = {
    { "32/null", &gbinder_io_32, NULL,
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_32),
      sizeof(GBinderHidlString) },
    { "32/empty", &gbinder_io_32, "",
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_32),
      sizeof(GBinderHidlString) + 8 /* string data aligned at 8 bytes */ },
    { "32/xxx", &gbinder_io_32, "xxx",
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_32),
      sizeof(GBinderHidlString) + 8 /* string data aligned at 8 bytes */ },
    { "64/null", &gbinder_io_64, NULL,
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_64),
      sizeof(GBinderHidlString) },
    { "64/empty", &gbinder_io_64, "",
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_64),
      sizeof(GBinderHidlString) + 8 /* string data aligned at 8 bytes */ },
    { "64/xxxxxxx", &gbinder_io_64, "xxxxxxx",
      TEST_ARRAY_AND_COUNT(test_hidl_string_offsets_64),
      sizeof(GBinderHidlString) + 8 /* string data aligned at 8 bytes */ }
};

static
void
test_hidl_string_xxx(
    const TestHidlStringData* test,
    void (*append)(GBinderWriter* writer, const char* str))
{
    GBinderLocalRequest* req = test_local_request_new_with_io(test->io);
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;
    guint i;

    gbinder_local_request_init_writer(req, &writer);
    append(&writer, test->str);
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
test_hidl_string(
    gconstpointer test_data)
{
    test_hidl_string_xxx(test_data, gbinder_writer_append_hidl_string);
}

static
void
test_hidl_string_copy(
    gconstpointer test_data)
{
    test_hidl_string_xxx(test_data, gbinder_writer_append_hidl_string_copy);
}

static
void
test_hidl_string2(
    void)
{
    GBinderLocalRequest* req = test_local_request_new();
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_hidl_string(&writer, "foo");
    gbinder_writer_append_hidl_string(&writer, NULL);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 4);
    g_assert(offsets->data[0] == 0);
    g_assert(offsets->data[1] == BUFFER_OBJECT_SIZE_32);
    g_assert(offsets->data[2] == 2*BUFFER_OBJECT_SIZE_32);
    /* 2 GBinderHidlStrings + "foo" aligned at 8 bytes boundary */
    g_assert(gbinder_output_data_buffers_size(data) ==
        (2 * sizeof(GBinderHidlString) + 8));

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

static guint test_hidl_string_vec_offsets_empty_32[] =
    { 0, BUFFER_OBJECT_SIZE_32 };
static guint test_hidl_string_vec_offsets_empty_64[] =
    { 0, BUFFER_OBJECT_SIZE_64 };
static guint test_hidl_string_vec_offsets_1_32[] =
    { 0, BUFFER_OBJECT_SIZE_32, 2*BUFFER_OBJECT_SIZE_32 };
static guint test_hidl_string_vec_offsets_1_64[] =
    { 0, BUFFER_OBJECT_SIZE_64, 2*BUFFER_OBJECT_SIZE_64 };

static const TestHidlStringVecData test_hidl_string_vec_tests[] = {
    { "32/null", &gbinder_io_32, NULL, -1,
      TEST_ARRAY_AND_COUNT(test_hidl_string_vec_offsets_empty_32),
      sizeof(GBinderHidlVec) },
    { "32/1", &gbinder_io_32,
      (const char**)TEST_ARRAY_AND_COUNT(test_hidl_string_vec_data_1),
      TEST_ARRAY_AND_COUNT(test_hidl_string_vec_offsets_1_32),
      sizeof(GBinderHidlVec) + sizeof(GBinderHidlString) + 8 },
    { "64/null", &gbinder_io_64, NULL, -1,
      TEST_ARRAY_AND_COUNT(test_hidl_string_vec_offsets_empty_64),
      sizeof(GBinderHidlVec) },
    { "64/1", &gbinder_io_64,
      (const char**)TEST_ARRAY_AND_COUNT(test_hidl_string_vec_data_1),
      TEST_ARRAY_AND_COUNT(test_hidl_string_vec_offsets_1_64),
      sizeof(GBinderHidlVec) + sizeof(GBinderHidlString) + 8 },
};

static
void
test_hidl_string_vec(
    gconstpointer test_data)
{
    const TestHidlStringVecData* test = test_data;
    GBinderLocalRequest* req = test_local_request_new_with_io(test->io);
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

static
void
test_buffer(
    void)
{
    GBinderLocalRequest* req = test_local_request_new();
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

typedef struct test_data {
    guint32 x;
} TestData;

typedef struct test_data_pointer {
    TestData* ptr;
} TestDataPointer;

static
void
test_parent(
    void)
{
    GBinderLocalRequest* req = test_local_request_new();
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
 * parcelable
 *==========================================================================*/

static
void
test_parcelable(
    void)
{
    const guint8 encoded_non_null[] = {
        TEST_INT32_BYTES(1),
        TEST_INT32_BYTES(sizeof(TestData) + sizeof(gint32)),
        TEST_INT32_BYTES(10)
    };
    const gint32 test_null_value = 0;
    GBinderLocalRequest* req;
    GBinderOutputData* data;
    GBinderWriter writer;
    TestData test_data;

    test_data.x = 10;

    /* Non-null */
    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_parcelable(&writer, &test_data, sizeof(test_data));

    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(encoded_non_null));
    g_assert(!memcmp(data->bytes->data, &encoded_non_null, data->bytes->len));

    gbinder_local_request_unref(req);

    /* Null */
    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_parcelable(&writer, NULL, 0);

    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(test_null_value));
    g_assert(!memcmp(data->bytes->data, &test_null_value, data->bytes->len));

    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * struct
 *==========================================================================*/

typedef struct test_struct_data {
    int x;
    GBinderHidlString str1;
    GBinderHidlString str2;
    GBinderHidlVec vec; /* vec<TestData> */
} TestStruct;

static
void
test_struct(
    void)
{
    static const GBinderWriterType test_data_t = {
        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(TestData), NULL
    };
    static const GBinderWriterField test_data_pointer_f[] = {
        GBINDER_WRITER_FIELD_POINTER(TestDataPointer,ptr, &test_data_t),
        GBINDER_WRITER_FIELD_END()
    };
    static const GBinderWriterField test_struct_f[] = {
        GBINDER_WRITER_FIELD_HIDL_STRING(TestStruct,str1),
        GBINDER_WRITER_FIELD_HIDL_STRING(TestStruct,str2),
        GBINDER_WRITER_FIELD_HIDL_VEC(TestStruct, vec, &test_data_t),
        GBINDER_WRITER_FIELD_END()
    };
    static const GBinderWriterType test_struct_t = {
        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(TestStruct), test_struct_f
    };
    static const GBinderWriterType test_data_pointer_t = {
        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(TestDataPointer),
        test_data_pointer_f
    };
    static const GBinderWriterField test_struct_vec_f[] = {
        {
            "vec", GBINDER_HIDL_VEC_BUFFER_OFFSET, &test_struct_t,
            gbinder_writer_field_hidl_vec_write_buf, NULL
        },
        GBINDER_WRITER_FIELD_END()
    };
    static const GBinderWriterType test_struct_vec_t = {
        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(GBinderHidlVec), test_struct_vec_f
    };
    /* Vector with no type information is handled as empty vector */
    static const GBinderWriterField test_struct_vec2_f[] = {
        {
            "vec", GBINDER_HIDL_VEC_BUFFER_OFFSET, NULL,
            gbinder_writer_field_hidl_vec_write_buf, NULL
        },
        GBINDER_WRITER_FIELD_END()
    };
    static const GBinderWriterType test_struct_vec2_t = {
        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(GBinderHidlVec), test_struct_vec2_f
    };
    GBinderLocalRequest* req = test_local_request_new();
    GBinderOutputData* data;
    GBinderWriter writer;
    GUtilIntArray* offsets;
    TestData test_data;
    TestDataPointer test_data_ptr;
    TestStruct test_struct;
    GBinderHidlVec vec;

    test_data.x = 42;
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_struct(&writer, &test_data, &test_data_t, NULL);

    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,1);
    g_assert_cmpuint(offsets->data[0], == ,0);
    /* Buffers are aligned at 8 bytes boundary */
    g_assert_cmpuint(gbinder_output_data_buffers_size(data), == ,8);
    gbinder_local_request_unref(req);

    /* Write TestStruct */
    memset(&test_struct, 0, sizeof(test_struct));
    test_struct.x = 42;
    test_struct.str1.data.str = "test";
    test_struct.str1.len = strlen(test_struct.str1.data.str);
    test_struct.vec.data.ptr = &test_data;
    test_struct.vec.count = 1;

    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_struct(&writer, &test_struct, &test_struct_t, NULL);

    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,4);
    g_assert_cmpuint(offsets->data[0], == ,0);
    gbinder_local_request_unref(req);

    /* Write TestDataPointer */
    test_data_ptr.ptr = &test_data;

    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_struct(&writer, &test_data_ptr,
        &test_data_pointer_t, NULL);

    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,2);
    gbinder_local_request_unref(req);

    /* Write vec<TestStruct> */
    memset(&vec, 0, sizeof(vec));
    vec.data.ptr = &test_struct;
    vec.count = 1;

    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_struct(&writer, &vec, &test_struct_vec_t, NULL);

    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,5);
    g_assert_cmpuint(offsets->data[0], == ,0);
    gbinder_local_request_unref(req);

    /* Write vec<TestStruct> in a different way */
    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_struct_vec(&writer, &test_struct, 1, &test_struct_t);

    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,5);
    g_assert_cmpuint(offsets->data[0], == ,0);
    gbinder_local_request_unref(req);

    /* Corner cases */
    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_struct(&writer, &vec, NULL, NULL);

    /* Without the type information, an empty buffer is written */
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,1);
    g_assert_cmpuint(offsets->data[0], == ,0);
    gbinder_local_request_unref(req);

    /* Vector without type information */
    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_struct(&writer, &vec, &test_struct_vec2_t, NULL);

    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,2);
    g_assert_cmpuint(offsets->data[0], == ,0);
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * struct_vec
 *==========================================================================*/

static
void
test_struct_vec(
    void)
{
    /* vec<byte> */
    static const GBinderWriterField vec_byte_ptr_f[] = {
        {
            "ptr", GBINDER_HIDL_VEC_BUFFER_OFFSET, &gbinder_writer_type_byte,
            gbinder_writer_field_hidl_vec_write_buf, NULL
        },
        GBINDER_WRITER_FIELD_END()
    };

    static const GBinderWriterType vec_byte_t = {
        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(GBinderHidlVec), vec_byte_ptr_f
    };

    /* vec<int32> */
    static const GBinderWriterField vec_int32_ptr_f[] = {
        {
            "ptr", GBINDER_HIDL_VEC_BUFFER_OFFSET, &gbinder_writer_type_int32,
            gbinder_writer_field_hidl_vec_write_buf, NULL
        },
        GBINDER_WRITER_FIELD_END()
    };

    static const GBinderWriterType vec_int32_t = {
        GBINDER_WRITER_STRUCT_NAME_AND_SIZE(GBinderHidlVec), vec_int32_ptr_f
    };

    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req =  gbinder_local_request_new(gbinder_ipc_io(ipc),
          gbinder_ipc_protocol(ipc), NULL);
    GBinderOutputData* writer_data;
    GBinderReaderData reader_data;
    GBinderWriter writer;
    GBinderReader reader;
    GUtilIntArray* offsets;
    GBinderHidlVec vec_byte, vec_int32;
    gsize count, elemsize;
    const void* vec_data;
    guint i;

    static const guint8 vec_byte_data[] = { 0x01, 0x02 };
    static const guint32 vec_int32_data[] = { 42 };

    memset(&vec_byte, 0, sizeof(vec_byte));
    vec_byte.data.ptr = vec_byte_data;
    vec_byte.count = G_N_ELEMENTS(vec_byte_data);

    memset(&vec_int32, 0, sizeof(vec_int32));
    vec_int32.data.ptr = vec_int32_data;
    vec_int32.count = G_N_ELEMENTS(vec_int32_data);

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_struct(&writer, &vec_byte, &vec_byte_t, NULL);
    gbinder_writer_append_struct(&writer, &vec_int32, &vec_int32_t, NULL);

    writer_data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(writer_data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,4);

    /* Set up the reader */
    memset(&reader_data, 0, sizeof(reader_data));
    reader_data.reg = gbinder_ipc_object_registry(ipc);
    reader_data.objects = g_new0(void*, offsets->count + 1);
    reader_data.buffer = gbinder_buffer_new(ipc->driver,
        gutil_memdup(writer_data->bytes->data, writer_data->bytes->len),
        writer_data->bytes->len, reader_data.objects);
    for (i = 0; i < offsets->count; i++) {
        reader_data.objects[i] =  reader_data.buffer->data + offsets->data[i];
    }

    /* Read those vectors back */
    gbinder_reader_init(&reader, &reader_data, 0, writer_data->bytes->len);

    /* vec<byte> */
    vec_data = gbinder_reader_read_hidl_vec(&reader, &count, &elemsize);
    g_assert(vec_data);
    g_assert_cmpuint(count, == ,G_N_ELEMENTS(vec_byte_data));
    g_assert_cmpuint(elemsize, == ,sizeof(vec_byte_data[0]));
    g_assert(!memcmp(vec_data, vec_byte_data, sizeof(vec_byte_data)));

    /* vec<int32> */
    vec_data = gbinder_reader_read_hidl_vec(&reader, &count, &elemsize);
    g_assert(vec_data);
    g_assert_cmpuint(count, == ,G_N_ELEMENTS(vec_int32_data));
    g_assert_cmpuint(elemsize, == ,sizeof(vec_int32_data[0]));
    g_assert(!memcmp(vec_data, vec_int32_data, sizeof(vec_int32_data)));

    gbinder_buffer_free(reader_data.buffer);
    gbinder_local_request_unref(req);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
}

/*==========================================================================*
 * fd
 * fd_invalid
 *==========================================================================*/

static
void
test_fd2(
    int fd)
{
    GBinderLocalRequest* req = test_local_request_new();
    GBinderOutputData* data;
    GUtilIntArray* offsets;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_fd(&writer, fd);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 0);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == BINDER_OBJECT_SIZE_32);
    gbinder_local_request_unref(req);
}

static
void
test_fd(
    void)
{
    test_fd2(0);
}

static
void
test_fd_invalid(
    void)
{
    test_fd2(-1);
}

/*==========================================================================*
 * fd_close_error
 *==========================================================================*/

static
void
test_fd_close_error(
    void)
{
    const GBinderIo* io = &gbinder_io_32;
    GBinderLocalRequest* req = test_local_request_new_with_io(io);
    GBinderOutputData* data;
    GBinderWriter writer;
    int fd = -1;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_fd(&writer, STDOUT_FILENO);
    data = gbinder_local_request_data(req);
    g_assert(data->bytes->len == BINDER_OBJECT_SIZE_32);

    /* Fetch duplicated fd and close it. That makes the second close
     * done by gbinder_writer_data_close_fd() fail. */
    g_assert(io->decode_fd_object(data->bytes->data, data->bytes->len, &fd));
    g_assert(close(fd) == 0);
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * local_object
 *==========================================================================*/

typedef struct test_local_object_data {
    const char* name;
    const char* protocol;
    const guint objsize;
} TestLocalObjectData;

static const TestLocalObjectData local_object_tests [] = {
    { "default", NULL, BINDER_OBJECT_SIZE_32 },
    { "aidl", "aidl", BINDER_OBJECT_SIZE_32 },
    { "aidl2", "aidl2", BINDER_OBJECT_SIZE_32 },
    { "aidl3", "aidl3", BINDER_OBJECT_SIZE_32 + 4 }
};

static
void
test_local_object(
    gconstpointer test_data)
{
    const TestLocalObjectData* test = test_data;
    GBinderLocalRequest* req;
    GBinderOutputData* data;
    GUtilIntArray* offsets;
    GBinderWriter writer;
    TestContext context;

    test_context_init(&context, test->protocol);
    req = test_local_request_new();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_local_object(&writer, NULL);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,1);
    g_assert_cmpuint(offsets->data[0], == ,0);
    g_assert_cmpuint(gbinder_output_data_buffers_size(data), == ,0);
    g_assert_cmpuint(data->bytes->len, == ,test->objsize);
    gbinder_local_request_unref(req);
    test_context_deinit(&context);
}

/*==========================================================================*
 * remote_object
 *==========================================================================*/

static
void
test_remote_object(
    void)
{
    GBinderLocalRequest* req = test_local_request_new_64();
    GBinderOutputData* data;
    GUtilIntArray* offsets;
    GBinderWriter writer;
    TestContext test;

    test_context_init(&test, NULL);
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
    test_context_deinit(&test);
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

    const char in_data[] = "abcd12";
    gint32 in_len = sizeof(in_data) - 1;
    gint32 null_len = -1;

    /* test for NULL byte array with non-zero len */
    req = test_local_request_new_64();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_byte_array(&writer, NULL, 42);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(gint32));
    g_assert(!memcmp(data->bytes->data, &null_len, data->bytes->len));
    gbinder_local_request_unref(req);

    /* test for valid array with zero len */
    req = test_local_request_new_64();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_byte_array(&writer, in_data, 0);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(gint32));
    g_assert(!memcmp(data->bytes->data, &null_len, data->bytes->len));
    gbinder_local_request_unref(req);

    /* test for valid array with correct len */
    req = test_local_request_new_64();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_byte_array(&writer, in_data, in_len);
    data = gbinder_local_request_data(req);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(in_len) + G_ALIGN4(in_len));
    g_assert(!memcmp(data->bytes->data, &in_len, sizeof(in_len)));
    g_assert(!memcmp(data->bytes->data + sizeof(in_len), in_data, in_len));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * fmq descriptor
 *==========================================================================*/

#if GBINDER_FMQ_SUPPORTED

static
void
test_fmq_descriptor(
    void)
{
    GBinderLocalRequest* req;
    GBinderOutputData* data;
    GUtilIntArray* offsets;
    GBinderWriter writer;
    const gint32 len = 3 * BUFFER_OBJECT_SIZE_64 /* Buffer objects */
        + sizeof(gint64) /* gint64 */
        + 4 * sizeof(gint64); /* binder_fd_array_object */

    GBinderFmq* fmq = gbinder_fmq_new(sizeof(guint32), 5,
        GBINDER_FMQ_TYPE_SYNC_READ_WRITE,
        GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG, -1, 0);

    g_assert(fmq);
    req = test_local_request_new_64();
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_fmq_descriptor(&writer, fmq);
    data = gbinder_local_request_data(req);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert_cmpuint(offsets->count, == ,4);
    g_assert(offsets->data[0] == 0);
    g_assert(offsets->data[1] == BUFFER_OBJECT_SIZE_64);
    g_assert(offsets->data[2] == 2 * BUFFER_OBJECT_SIZE_64 + sizeof(gint64));
    g_assert(offsets->data[3] == 3 * BUFFER_OBJECT_SIZE_64 + sizeof(gint64));
    g_assert_cmpuint(data->bytes->len, == ,len);
    gbinder_local_request_unref(req);
    gbinder_fmq_unref(fmq);
}

#endif /* GBINDER_FMQ_SUPPORTED */

/*==========================================================================*
 * bytes_written
 *==========================================================================*/

static
void
test_bytes_written(
    void)
{
    const guint32 value = 1234567;
    GBinderLocalRequest* req = test_local_request_new();
    GBinderWriter writer;
    const void* data;
    gsize size = 0;

    gbinder_local_request_init_writer(req, &writer);
    g_assert(gbinder_writer_bytes_written(&writer) == 0);
    gbinder_writer_append_int32(&writer, value);
    g_assert_cmpuint(gbinder_writer_bytes_written(&writer), == ,sizeof(value));
    data = gbinder_writer_get_data(&writer, NULL);
    g_assert(data);
    g_assert(data == gbinder_writer_get_data(&writer, &size));
    g_assert_cmpuint(size, == ,sizeof(value));
    g_assert(!memcmp(data, &value, size));

    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/writer/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("cleanup"), test_cleanup);
    g_test_add_func(TEST_("int8"), test_int8);
    g_test_add_func(TEST_("int16"), test_int16);
    g_test_add_func(TEST_("int32"), test_int32);
    g_test_add_func(TEST_("int64"), test_int64);
    g_test_add_func(TEST_("float"), test_float);
    g_test_add_func(TEST_("double"), test_double);
    g_test_add_func(TEST_("bool"), test_bool);
    g_test_add_func(TEST_("bytes"), test_bytes);
    g_test_add_func(TEST_("string8"), test_string8);

    for (i = 0; i < G_N_ELEMENTS(test_string16_tests); i++) {
        const TestString16Data* test = test_string16_tests + i;
        char* path = g_strconcat(TEST_("string16/"), test->name, NULL);

        g_test_add_data_func(path, test, test_string16);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_utf16_tests); i++) {
        const TestUtf16Data* test = test_utf16_tests + i;
        char* path = g_strconcat(TEST_("utf16/"), test->name, NULL);

        g_test_add_data_func(path, test, test_utf16);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_hidl_vec_tests); i++) {
        const TestHidlVecData* test = test_hidl_vec_tests + i;
        char* path = g_strconcat(TEST_("hidl_vec/"), test->name, NULL);

        g_test_add_data_func(path, test, test_hidl_vec);
        g_free(path);
    }

    g_test_add_func(TEST_("hidl_string/2strings"), test_hidl_string2);
    for (i = 0; i < G_N_ELEMENTS(test_hidl_string_tests); i++) {
        const TestHidlStringData* test = test_hidl_string_tests + i;
        char* path = g_strconcat(TEST_("hidl_string/"), test->name, NULL);
        char* path2 = g_strconcat(TEST_("hidl_string_copy/"), test->name, NULL);

        g_test_add_data_func(path, test, test_hidl_string);
        g_test_add_data_func(path2, test, test_hidl_string_copy);
        g_free(path);
        g_free(path2);
    }

    for (i = 0; i < G_N_ELEMENTS(test_hidl_string_vec_tests); i++) {
        const TestHidlStringVecData* test = test_hidl_string_vec_tests + i;
        char* path = g_strconcat(TEST_("hidl_string_vec/"), test->name, NULL);

        g_test_add_data_func(path, test, test_hidl_string_vec);
        g_free(path);
    }

    g_test_add_func(TEST_("buffer"), test_buffer);
    g_test_add_func(TEST_("parent"), test_parent);
    g_test_add_func(TEST_("parcelable"), test_parcelable);
    g_test_add_func(TEST_("struct"), test_struct);
    g_test_add_func(TEST_("struct_vec"), test_struct_vec);
    g_test_add_func(TEST_("fd"), test_fd);
    g_test_add_func(TEST_("fd_invalid"), test_fd_invalid);
    g_test_add_func(TEST_("fd_close_error"), test_fd_close_error);

    for (i = 0; i < G_N_ELEMENTS(local_object_tests); i++) {
        const TestLocalObjectData* test = local_object_tests + i;
        char* path = g_strconcat(TEST_("local_object/"), test->name, NULL);

        g_test_add_data_func(path, test, test_local_object);
        g_free(path);
    }

    g_test_add_func(TEST_("remote_object"), test_remote_object);
    g_test_add_func(TEST_("byte_array"), test_byte_array);
    g_test_add_func(TEST_("bytes_written"), test_bytes_written);

#if GBINDER_FMQ_SUPPORTED
    {
        int test_fd = syscall(__NR_memfd_create, "test", MFD_CLOEXEC);

        if (test_fd < 0 && errno == ENOSYS) {
            GINFO("Skipping tests that rely on memfd_create");
        } else {
            close(test_fd);
            g_test_add_func(TEST_("fmq_descriptor"), test_fmq_descriptor);
        }
    }
#endif /* GBINDER_FMQ_SUPPORTED */

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
