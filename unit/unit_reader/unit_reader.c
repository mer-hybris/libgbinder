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

#include "gbinder_buffer_p.h"
#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_reader_p.h"
#include "gbinder_remote_object_p.h"

static TestOpt test_opt;

typedef struct binder_buffer_object_64 {
    guint32 type;
    guint32 flags;
    guint64 buffer;
    guint64 length;
    guint64 parent;
    guint64 parent_offset;
} BinderObject64;

#define BINDER_TYPE_HANDLE GBINDER_FOURCC('s','h','*',0x85)
#define BINDER_TYPE_PTR GBINDER_FOURCC('p','t','*',0x85)

/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    GBinderReader reader;

    gbinder_reader_init(&reader, NULL, 0, 0);
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(!gbinder_reader_bytes_read(&reader));
    g_assert(!gbinder_reader_bytes_remaining(&reader));
    g_assert(!gbinder_reader_read_byte(&reader, NULL));
    g_assert(!gbinder_reader_read_bool(&reader, NULL));
    g_assert(!gbinder_reader_read_int32(&reader, NULL));
    g_assert(!gbinder_reader_read_uint32(&reader, NULL));
    g_assert(!gbinder_reader_read_int64(&reader, NULL));
    g_assert(!gbinder_reader_read_uint64(&reader, NULL));
    g_assert(!gbinder_reader_read_float(&reader, NULL));
    g_assert(!gbinder_reader_read_double(&reader, NULL));
    g_assert(!gbinder_reader_read_object(&reader));
    g_assert(!gbinder_reader_read_nullable_object(&reader, NULL));
    g_assert(!gbinder_reader_read_buffer(&reader));
    g_assert(!gbinder_reader_read_hidl_string(&reader));
    g_assert(!gbinder_reader_read_hidl_string_vec(&reader));
    g_assert(!gbinder_reader_skip_buffer(&reader));
    g_assert(!gbinder_reader_read_string8(&reader));
    g_assert(!gbinder_reader_read_string16(&reader));
    g_assert(!gbinder_reader_skip_string16(&reader));
}

/*==========================================================================*
 * byte
 *==========================================================================*/

static
void
test_byte(
    void)
{
    const guint8 in = 42;
    guint8 out = 0;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, g_memdup(&in, sizeof(in)),
        sizeof(in));

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_byte(&reader, &out));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == out);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_byte(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * bool
 *==========================================================================*/

static
void
test_bool(
    void)
{
    const guint8 in_true[4] = { 0x01, 0xff, 0xff, 0xff };
    const guint8 in_false[4] = { 0x00, 0xff, 0xff, 0xff };
    gboolean out = FALSE;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver,
        g_memdup(&in_true, sizeof(in_true)), sizeof(in_true));

    /* true */
    gbinder_reader_init(&reader, &data, 0, data.buffer->size);
    g_assert(gbinder_reader_read_bool(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_reader_init(&reader, &data, 0, data.buffer->size);
    g_assert(gbinder_reader_read_bool(&reader, &out));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(out == TRUE);

    /* false */
    gbinder_buffer_free(data.buffer);
    data.buffer = gbinder_buffer_new(driver,
        g_memdup(&in_false, sizeof(in_false)), sizeof(in_false));

    gbinder_reader_init(&reader, &data, 0, data.buffer->size);
    g_assert(gbinder_reader_read_bool(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_reader_init(&reader, &data, 0, data.buffer->size);
    g_assert(gbinder_reader_read_bool(&reader, &out));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(out == FALSE);

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * int32
 *==========================================================================*/

static
void
test_int32(
    void)
{
    const guint32 in = 42;
    guint32 out1 = 0;
    gint32 out2 = 0;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, g_memdup(&in, sizeof(in)),
        sizeof(in));

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_uint32(&reader, &out1));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == out1);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_int32(&reader, &out2));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == (guint32)out2);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_int32(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * int64
 *==========================================================================*/

static
void
test_int64(
    void)
{
    const guint64 in = 42;
    guint64 out1 = 0;
    gint64 out2 = 0;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, g_memdup(&in, sizeof(in)),
        sizeof(in));

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_uint64(&reader, &out1));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == out1);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_int64(&reader, &out2));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == (guint64)out2);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_int64(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * float
 *==========================================================================*/

static
void
test_float(
    void)
{
    const gfloat in = 42;
    gfloat out1 = 0;
    gfloat out2 = 0;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, g_memdup(&in, sizeof(in)),
        sizeof(in));

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_float(&reader, &out1));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == out1);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_float(&reader, &out2));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == (gfloat)out2);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_float(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * double
 *==========================================================================*/

static
void
test_double(
    void)
{
    const gdouble in = 42;
    gdouble out1 = 0;
    gdouble out2 = 0;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, g_memdup(&in, sizeof(in)),
        sizeof(in));

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_double(&reader, &out1));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == out1);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_double(&reader, &out2));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(in == (gdouble)out2);

    gbinder_reader_init(&reader, &data, 0, sizeof(in));
    g_assert(gbinder_reader_read_double(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * string8
 *==========================================================================*/

typedef struct test_string_data {
    const char* name;
    const guint8* in;
    guint in_size;
    const char* out;
    gboolean remaining;
} TestStringData;

static const guint8 test_string8_in_short [] = {
    't', 'e', 's', 't', 0, 0, 0
};

static const guint8 test_string8_in_basic1 [] = {
    't', 'e', 's', 't', 0, 0, 0, 0
};

static const guint8 test_string8_in_basic2 [] = {
    't', 'e', 's', 't', 0, 0, 0, 0, 0
};

static const TestStringData test_string8_tests [] = {
    { "short", TEST_ARRAY_AND_SIZE(test_string8_in_short), NULL,
       sizeof(test_string8_in_short)},
    { "ok1", TEST_ARRAY_AND_SIZE(test_string8_in_basic1), "test", 0 },
    { "ok2", TEST_ARRAY_AND_SIZE(test_string8_in_basic2), "test", 1 }
};

static
void
test_string8(
    gconstpointer test_data)
{
    const TestStringData* test = test_data;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;
    const char* str;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, g_memdup(test->in, test->in_size),
        test->in_size);

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    str = gbinder_reader_read_string8(&reader);
    g_assert(!g_strcmp0(str, test->out));
    g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    g_assert(gbinder_reader_bytes_remaining(&reader) == test->remaining);

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * string16
 *==========================================================================*/

static const guint8 test_string16_in_null [] = {
    TEST_INT32_BYTES(-1)
};

static const guint8 test_string16_in_invalid [] = {
    TEST_INT32_BYTES(-2)
};

static const guint8 test_string16_in_short [] = {
    TEST_INT32_BYTES(3),
    TEST_INT16_BYTES('f'), TEST_INT16_BYTES('o'),
    TEST_INT16_BYTES('o'), 0x00
};

static const guint8 test_string16_in_basic1 [] = {
    TEST_INT32_BYTES(3),
    TEST_INT16_BYTES('f'), TEST_INT16_BYTES('o'),
    TEST_INT16_BYTES('o'), 0x00, 0x00
};

static const guint8 test_string16_in_basic2 [] = {
    TEST_INT32_BYTES(3),
    TEST_INT16_BYTES('f'), TEST_INT16_BYTES('o'),
    TEST_INT16_BYTES('o'), 0x00, 0x00, 0x00
};

static const TestStringData test_string16_tests [] = {
    { "invalid", TEST_ARRAY_AND_SIZE(test_string16_in_invalid), NULL,
        sizeof(test_string16_in_invalid) },
    { "short", TEST_ARRAY_AND_SIZE(test_string16_in_short), NULL,
        sizeof(test_string16_in_short) },
    { "ok1", TEST_ARRAY_AND_SIZE(test_string16_in_basic1), "foo", 0 },
    { "ok2", TEST_ARRAY_AND_SIZE(test_string16_in_basic2), "foo", 1 }
};

static
void
test_string16_null(
    void)
{
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;
    char dummy;
    char* out = &dummy;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver,
        g_memdup(TEST_ARRAY_AND_SIZE(test_string16_in_null)),
        sizeof(test_string16_in_null));

    gbinder_reader_init(&reader, &data, 0, sizeof(test_string16_in_null));
    g_assert(gbinder_reader_read_nullable_string16(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_reader_init(&reader, &data, 0, sizeof(test_string16_in_null));
    g_assert(gbinder_reader_read_nullable_string16(&reader, &out));
    g_assert(!out);
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_reader_init(&reader, &data, 0, sizeof(test_string16_in_null));
    g_assert(!gbinder_reader_read_string16(&reader));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_reader_init(&reader, &data, 0, sizeof(test_string16_in_null));
    g_assert(gbinder_reader_skip_string16(&reader));
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

static
void
test_string16(
    gconstpointer test_data)
{
    const TestStringData* test = test_data;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderReader reader;
    GBinderReaderData data;
    const gboolean valid = (test->out != NULL);
    char* str = NULL;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, g_memdup(test->in, test->in_size),
        test->in_size);

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    g_assert(gbinder_reader_read_nullable_string16(&reader, NULL) == valid);
    g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    g_assert(gbinder_reader_bytes_remaining(&reader) == test->remaining);

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    g_assert(gbinder_reader_read_nullable_string16(&reader, &str) == valid);
    g_assert(!g_strcmp0(str, test->out));
    g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    g_assert(gbinder_reader_bytes_remaining(&reader) == test->remaining);
    g_free(str);

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    str = gbinder_reader_read_string16(&reader);
    g_assert(!g_strcmp0(str, test->out));
    g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    g_assert(gbinder_reader_bytes_remaining(&reader) == test->remaining);
    g_free(str);

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    g_assert(gbinder_reader_skip_string16(&reader) == (test->out != NULL));
    g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    g_assert(gbinder_reader_bytes_remaining(&reader) == test->remaining);

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * hidl_string_err
 *==========================================================================*/

typedef struct test_hidl_string_err {
    const char* name;
    const guint8* in;
    guint in_size;
    const guint* offset;
    guint offset_count;
} TestHidlStringErr;

static const guint8 test_hidl_string_err_short [] = { 0x00 };
static const guint8 test_hidl_string_err_empty [] = {
    TEST_INT32_BYTES(GBINDER_FOURCC('p', 't', '*', 0x85)),
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static const guint test_hidl_string_err_bad_offset [] = { 100 };
static const guint test_hidl_string_err_one_offset [] = { 0 };

static const TestHidlStringErr test_hidl_string_err_tests [] = {
    { "no-data", TEST_ARRAY_AND_SIZE(test_hidl_string_err_short), NULL },
    { "no-offset", TEST_ARRAY_AND_SIZE(test_hidl_string_err_empty), NULL },
    { "empty-offset", TEST_ARRAY_AND_SIZE(test_hidl_string_err_empty),
        test_hidl_string_err_one_offset, 0 },
    { "bad-offset", TEST_ARRAY_AND_SIZE(test_hidl_string_err_empty),
        TEST_ARRAY_AND_COUNT(test_hidl_string_err_bad_offset) },
    { "short-buffer", TEST_ARRAY_AND_SIZE(test_hidl_string_err_short),
        TEST_ARRAY_AND_COUNT(test_hidl_string_err_one_offset) },
    { "empty-buffer", TEST_ARRAY_AND_SIZE(test_hidl_string_err_empty),
        TEST_ARRAY_AND_COUNT(test_hidl_string_err_one_offset) }
};

static
void
test_hidl_string_err(
    gconstpointer test_data)
{
    const TestHidlStringErr* test = test_data;
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(test->in, test->in_size), test->in_size);
    GBinderReaderData data;
    GBinderReader reader;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);
    if (test->offset) {
        guint i;

        data.objects = g_new(void*, test->offset_count + 1);
        for (i = 0; i < test->offset_count; i++) {
            data.objects[i] = (guint8*)buf->data + test->offset[i];
        }
        data.objects[i] = NULL;
    }

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    g_assert(!gbinder_reader_read_hidl_string(&reader));

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * object
 *==========================================================================*/

static
void
test_object(
    void)
{
    /* Using 64-bit I/O */
    static const guint8 input[] = {
        TEST_INT32_BYTES(BINDER_TYPE_HANDLE), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES(1 /* handle*/), TEST_INT64_BYTES(0)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER, NULL);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input));
    GBinderRemoteObject* obj = NULL;
    GBinderReaderData data;
    GBinderReader reader;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);
    data.objects = g_new(void*, 2);
    data.objects[0] = buf->data;
    data.objects[1] = NULL;
    gbinder_reader_init(&reader, &data, 0, buf->size);

    g_assert(gbinder_reader_read_nullable_object(&reader, &obj));
    g_assert(obj);
    g_assert(obj->handle == 1);

    g_free(data.objects);
    gbinder_remote_object_unref(obj);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * hidl_object_no_reg
 *==========================================================================*/

static
void
test_object_no_reg(
    void)
{
    GBinderReaderData data;
    GBinderReader reader;

    memset(&data, 0, sizeof(data));
    gbinder_reader_init(&reader, &data, 0, 0);
    g_assert(!gbinder_reader_read_hidl_string(&reader));
    g_assert(!gbinder_reader_read_object(&reader));
}

/*==========================================================================*
 * object_invalid
 *==========================================================================*/

static
void
test_object_invalid(
    void)
{
    /* Using 64-bit I/O */
    static const guint8 input[] = {
        TEST_INT32_BYTES(42 /* invalid type */), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES(1 /* handle*/), TEST_INT64_BYTES(0)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER, NULL);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input));
    GBinderRemoteObject* obj = NULL;
    GBinderReaderData data;
    GBinderReader reader;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);
    data.objects = g_new(void*, 2);
    data.objects[0] = buf->data;
    data.objects[1] = NULL;
    gbinder_reader_init(&reader, &data, 0, buf->size);

    g_assert(!gbinder_reader_read_nullable_object(&reader, &obj));
    g_assert(!obj);

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * vec
 *==========================================================================*/

static
void
test_vec(
    void)
{
    /* Using 64-bit I/O */
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER, NULL);
    GBinderReaderData data;
    GBinderReader reader;
    BinderObject64 obj;
    HidlVec vec;
    char** out;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    memset(&vec, 0, sizeof(vec));
    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_PTR;
    obj.buffer = (gsize)&vec;

    /* This one will fail because the buffer is one byte short */
    obj.length = sizeof(vec) - 1;
    data.buffer =  gbinder_buffer_new(ipc->driver,
        g_memdup(&obj, sizeof(obj)), sizeof(obj));
    data.reg = gbinder_ipc_object_registry(ipc);
    data.objects = g_new(void*, 2);
    data.objects[0] = data.buffer->data;
    data.objects[1] = NULL;
    gbinder_reader_init(&reader, &data, 0, data.buffer->size);
    g_assert(!gbinder_reader_read_hidl_string_vec(&reader));

    /* This one will read empty array */
    obj.length = sizeof(vec);
    gbinder_buffer_free(data.buffer);
    data.buffer = gbinder_buffer_new(ipc->driver,
        g_memdup(&obj, sizeof(obj)), sizeof(obj));
    data.objects[0] = data.buffer->data;
    gbinder_reader_init(&reader, &data, 0, data.buffer->size);
    out = gbinder_reader_read_hidl_string_vec(&reader);
    g_assert(out);
    g_assert(!out[0]);
    g_strfreev(out);

    g_free(data.objects);
    gbinder_buffer_free(data.buffer);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/reader/"

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "empty", test_empty);
    g_test_add_func(TEST_PREFIX "byte", test_byte);
    g_test_add_func(TEST_PREFIX "bool", test_bool);
    g_test_add_func(TEST_PREFIX "int32", test_int32);
    g_test_add_func(TEST_PREFIX "int64", test_int64);
    g_test_add_func(TEST_PREFIX "float", test_float);
    g_test_add_func(TEST_PREFIX "double", test_double);

    for (i = 0; i < G_N_ELEMENTS(test_string8_tests); i++) {
        const TestStringData* test = test_string8_tests + i;
        char* path = g_strconcat(TEST_PREFIX "/string8/", test->name, NULL);

        g_test_add_data_func(path, test, test_string8);
        g_free(path);
    }

    g_test_add_func(TEST_PREFIX "/string16/null", test_string16_null);
    for (i = 0; i < G_N_ELEMENTS(test_string16_tests); i++) {
        const TestStringData* test = test_string16_tests + i;
        char* path = g_strconcat(TEST_PREFIX "/string16/", test->name, NULL);

        g_test_add_data_func(path, test, test_string16);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_hidl_string_err_tests); i++) {
        const TestHidlStringErr* test = test_hidl_string_err_tests + i;
        char* path = g_strconcat(TEST_PREFIX "/hidl_string/err-", test->name,
            NULL);

        g_test_add_data_func(path, test, test_hidl_string_err);
        g_free(path);
    }

    g_test_add_func(TEST_PREFIX "/object/object", test_object);
    g_test_add_func(TEST_PREFIX "/object/object/invalid", test_object_invalid);
    g_test_add_func(TEST_PREFIX "/object/object/no_reg", test_object_no_reg);
    g_test_add_func(TEST_PREFIX "/vec", test_vec);
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
