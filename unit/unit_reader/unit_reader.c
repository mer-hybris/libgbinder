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

#include "test_common.h"

#include "gbinder_buffer_p.h"
#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_reader_p.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_io.h"

#include <unistd.h>
#include <fcntl.h>

static TestOpt test_opt;

typedef struct binder_buffer_object_64 {
    guint32 type;
    guint32 flags;
    union {
        const void* ptr;
        guint64 value;
    } buffer;
    guint64 length;
    guint64 parent;
    guint64 parent_offset;
} BinderObject64;

#define BINDER_TYPE_(c1,c2,c3) GBINDER_FOURCC(c1,c2,c3,0x85)
#define BINDER_TYPE_HANDLE BINDER_TYPE_('s','h','*')
#define BINDER_TYPE_PTR BINDER_TYPE_('p','t','*')
#define BINDER_TYPE_FD BINDER_TYPE_('f', 'd', '*')
#define BINDER_BUFFER_FLAG_HAS_PARENT 0x01
#define BINDER_FLAG_ACCEPTS_FDS 0x100
#define BUFFER_OBJECT_SIZE_64 (GBINDER_MAX_BUFFER_OBJECT_SIZE)
G_STATIC_ASSERT(sizeof(BinderObject64) == BUFFER_OBJECT_SIZE_64);

/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    GBinderReader reader;
    gsize count = 1, elemsize = 1;
    gsize len;

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
    g_assert(!gbinder_reader_read_hidl_struct1(&reader, 1));
    g_assert(!gbinder_reader_read_hidl_vec(&reader, NULL, NULL));
    g_assert(!gbinder_reader_read_hidl_vec(&reader, &count, &elemsize));
    g_assert(!gbinder_reader_read_hidl_vec1(&reader, NULL, 1));
    g_assert(!gbinder_reader_read_hidl_vec1(&reader, &count, 1));
    g_assert(!count);
    g_assert(!elemsize);
    g_assert(!gbinder_reader_skip_hidl_string(&reader));
    g_assert(!gbinder_reader_read_hidl_string(&reader));
    g_assert(!gbinder_reader_read_hidl_string_vec(&reader));
    g_assert(!gbinder_reader_skip_buffer(&reader));
    g_assert(!gbinder_reader_read_string8(&reader));
    g_assert(!gbinder_reader_read_string16(&reader));
    g_assert(!gbinder_reader_skip_string16(&reader));
    g_assert(!gbinder_reader_read_byte_array(&reader, &len));
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
        sizeof(in), NULL);

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
        g_memdup(&in_true, sizeof(in_true)), sizeof(in_true), NULL);

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
        g_memdup(&in_false, sizeof(in_false)), sizeof(in_false), NULL);

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
        sizeof(in), NULL);

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
        sizeof(in), NULL);

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
        sizeof(in), NULL);

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
        sizeof(in), NULL);

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
        test->in_size, NULL);

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
    const gunichar2* out2 = NULL;
    gsize len = 1;
    char dummy;
    char* out = &dummy;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver,
        g_memdup(TEST_ARRAY_AND_SIZE(test_string16_in_null)),
        sizeof(test_string16_in_null), NULL);

    gbinder_reader_init(&reader, &data, 0, sizeof(test_string16_in_null));
    g_assert(gbinder_reader_read_nullable_string16_utf16(&reader, NULL, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    len = 1;
    gbinder_reader_init(&reader, &data, 0, sizeof(test_string16_in_null));
    g_assert(gbinder_reader_read_nullable_string16_utf16(&reader, &out2, &len));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(!out2);
    g_assert(!len);

    gbinder_reader_init(&reader, &data, 0, sizeof(test_string16_in_null));
    g_assert(!gbinder_reader_read_string16_utf16(&reader, NULL));
    g_assert(gbinder_reader_at_end(&reader));

    len = 1;
    gbinder_reader_init(&reader, &data, 0, sizeof(test_string16_in_null));
    g_assert(!gbinder_reader_read_string16_utf16(&reader, &len));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(!len);

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
    const gunichar2* out2 = NULL;
    gsize len = 0;
    char* str = NULL;

    g_assert(driver);
    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, g_memdup(test->in, test->in_size),
        test->in_size, NULL);

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    if (valid) {
        out2 = gbinder_reader_read_string16_utf16(&reader, &len);
        g_assert(out2);
        g_assert((gsize)len == strlen(test->out));
        g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    } else {
        g_assert(!gbinder_reader_read_string16_utf16(&reader, NULL));
    }
    g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    g_assert(gbinder_reader_bytes_remaining(&reader) == test->remaining);

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    g_assert(gbinder_reader_read_nullable_string16_utf16(&reader, NULL,
        NULL) == valid);
    g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    g_assert(gbinder_reader_bytes_remaining(&reader) == test->remaining);

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    g_assert(gbinder_reader_read_nullable_string16_utf16(&reader, &out2,
        &len) == valid);
    g_assert(gbinder_reader_at_end(&reader) == (!test->remaining));
    g_assert(gbinder_reader_bytes_remaining(&reader) == test->remaining);
    if (valid) {
        g_assert(out2);
        g_assert((gsize)len == strlen(test->out));
    }

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
 * hidl_struct
 *==========================================================================*/
typedef struct test_hidl_struct {
    const char* name;
    const void* in;
    guint in_size;
    guint struct_size;
    const void* data;
} TestHidlStruct;

typedef struct test_hidl_struct_type {
    guint32 x;
} TestHidlStructType;

static const TestHidlStructType test_hidl_struct_data = { 0 };
static const BinderObject64 test_hidl_struct_ok_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_struct_data },
        sizeof(test_hidl_struct_data), 0, 0
    }
};
static const BinderObject64 test_hidl_struct_big_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_struct_data },
        2 * sizeof(test_hidl_struct_data), 0, 0
    }
};

static const TestHidlStruct test_hidl_struct_tests[] = {
    { "ok", TEST_ARRAY_AND_SIZE(test_hidl_struct_ok_buf),
      sizeof(TestHidlStructType), &test_hidl_struct_data },
    { "badsize",  TEST_ARRAY_AND_SIZE(test_hidl_struct_big_buf),
      sizeof(TestHidlStructType), NULL }
};

static
void
test_hidl_struct(
    gconstpointer test_data)
{
    const TestHidlStruct* test = test_data;
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(test->in, test->in_size), test->in_size, NULL);
    GBinderReaderData data;
    GBinderReader reader;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);
    data.objects = g_new0(void*, 2);
    data.objects[0] = buf->data;

    gbinder_reader_init(&reader, &data, 0, test->in_size);
    g_assert(gbinder_reader_read_hidl_struct1(&reader, test->struct_size) ==
        test->data);

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * hidl_vec
 *==========================================================================*/

typedef struct test_hidl_vec {
    const char* name;
    const void* in;
    guint in_size;
    const guint* offset;
    guint offset_count;
    const void* data;
    guint count;
    guint elemsize;
} TestHidlVec;

static const guint test_hidl_vec_2offsets [] = { 0, BUFFER_OBJECT_SIZE_64 };
static const guint8 test_hidl_vec_2bytes_data [] = { 0x01, 0x02 };
static const GBinderHidlVec test_hidl_vec_2bytes = {
    .data.ptr = test_hidl_vec_2bytes_data,
    sizeof(test_hidl_vec_2bytes_data),
    TRUE
};
static const BinderObject64 test_hidl_vec_2bytes_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_2bytes },
        sizeof(GBinderHidlVec), 0, 0
    },{
        BINDER_TYPE_PTR, BINDER_BUFFER_FLAG_HAS_PARENT,
        { test_hidl_vec_2bytes_data },
        sizeof(test_hidl_vec_2bytes_data), 0,
        GBINDER_HIDL_VEC_BUFFER_OFFSET
    }
};

static const GBinderHidlVec test_hidl_vec_empty = {
    .data.ptr = test_hidl_vec_2bytes_data, 0, TRUE
};
static const BinderObject64 test_hidl_vec_empty_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_empty },
        sizeof(GBinderHidlVec), 0, 0
    },{
        BINDER_TYPE_PTR, BINDER_BUFFER_FLAG_HAS_PARENT,
        { test_hidl_vec_2bytes_data },
        0, 0, GBINDER_HIDL_VEC_BUFFER_OFFSET
    }
};

static const guint test_hidl_vec_1offset [] = {0};
static const GBinderHidlVec test_hidl_vec_null = {{0}, 0, TRUE};
static const BinderObject64 test_hidl_vec_null_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_null },
        sizeof(GBinderHidlVec), 0, 0
    }
};

/* Buffer smaller than GBinderHidlVec */
static const BinderObject64 test_hidl_vec_short_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_empty },
        sizeof(GBinderHidlVec) - 1, 0, 0
    }
};

/* NULL buffer with size 1 */
static const GBinderHidlVec test_hidl_vec_badnull = {{0}, 1, TRUE};
static const BinderObject64 test_hidl_vec_badnull_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_badnull },
        sizeof(GBinderHidlVec), 0, 0
    }
};

/* Buffer size not divisible by count */
static const guint8 test_hidl_vec_badsize_data [] = { 0x01, 0x02, 0x03 };
static const GBinderHidlVec test_hidl_vec_badsize = {
    .data.ptr = test_hidl_vec_badsize_data, 2, TRUE
};
static const BinderObject64 test_hidl_vec_badsize_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_badsize },
        sizeof(GBinderHidlVec), 0, 0
    },{
        BINDER_TYPE_PTR, BINDER_BUFFER_FLAG_HAS_PARENT,
        { test_hidl_vec_badsize_data },
        sizeof(test_hidl_vec_badsize_data), 0,
        GBINDER_HIDL_VEC_BUFFER_OFFSET
    }
};

/* Bad buffer address */
static const guint8 test_hidl_vec_badbuf_data [] = { 0x01, 0x02, 0x03 };
static const GBinderHidlVec test_hidl_vec_badbuf = {
    .data.ptr = test_hidl_vec_badbuf_data,
    sizeof(test_hidl_vec_badbuf_data), TRUE
};
static const BinderObject64 test_hidl_vec_badbuf_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_badbuf },
        sizeof(GBinderHidlVec), 0, 0
    },{
        BINDER_TYPE_PTR, BINDER_BUFFER_FLAG_HAS_PARENT,
        { test_hidl_vec_badsize_data },
        sizeof(test_hidl_vec_badsize_data), 0,
        GBINDER_HIDL_VEC_BUFFER_OFFSET
    }
};

/* Non-zero count and zero size */
static const GBinderHidlVec test_hidl_vec_badcount1 = {
    .data.ptr = test_hidl_vec_badsize_data, 1, TRUE
};
static const BinderObject64 test_hidl_vec_badcount1_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_badcount1 },
        sizeof(GBinderHidlVec), 0, 0
    },{
        BINDER_TYPE_PTR, BINDER_BUFFER_FLAG_HAS_PARENT,
        { test_hidl_vec_badsize_data }, 0, 0,
        GBINDER_HIDL_VEC_BUFFER_OFFSET
    }
};

/* Zero count0 and non-zero size */
static const GBinderHidlVec test_hidl_vec_badcount2 = {
    .data.ptr = test_hidl_vec_badsize_data, 0, TRUE
};
static const BinderObject64 test_hidl_vec_badcount2_buf [] = {
    {
        BINDER_TYPE_PTR, 0,
        { &test_hidl_vec_badcount2 },
        sizeof(GBinderHidlVec), 0, 0
    },{
        BINDER_TYPE_PTR, BINDER_BUFFER_FLAG_HAS_PARENT,
        { test_hidl_vec_badsize_data },
        sizeof(test_hidl_vec_badsize_data), 0,
        GBINDER_HIDL_VEC_BUFFER_OFFSET
    }
};

static const TestHidlVec test_hidl_vec_tests[] = {
    { "2bytes", TEST_ARRAY_AND_SIZE(test_hidl_vec_2bytes_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_2offsets),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_2bytes_data), 1 },
    { "empty", TEST_ARRAY_AND_SIZE(test_hidl_vec_empty_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_2offsets),
      test_hidl_vec_2bytes_data, 0, 0 },
    { "null", TEST_ARRAY_AND_SIZE(test_hidl_vec_null_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_1offset),
      &test_hidl_vec_null, 0, 0 },
    { "missingbuf", test_hidl_vec_2bytes_buf,
      sizeof(test_hidl_vec_2bytes_buf[0]),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_1offset), NULL, 0, 0 },
    { "shortbuf", TEST_ARRAY_AND_SIZE(test_hidl_vec_short_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_1offset), NULL, 0, 0 },
    { "badnull", TEST_ARRAY_AND_SIZE(test_hidl_vec_badnull_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_1offset), NULL, 0, 0 },
    { "badsize", TEST_ARRAY_AND_SIZE(test_hidl_vec_badsize_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_2offsets), NULL, 0, 0 },
    { "badbuf", TEST_ARRAY_AND_SIZE(test_hidl_vec_badbuf_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_2offsets), NULL, 0, 0 },
    { "badcount1", TEST_ARRAY_AND_SIZE(test_hidl_vec_badcount1_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_2offsets), NULL, 0, 0 },
    { "badcount2", TEST_ARRAY_AND_SIZE(test_hidl_vec_badcount2_buf),
      TEST_ARRAY_AND_SIZE(test_hidl_vec_2offsets), NULL, 0, 0 }
};

static
void
test_hidl_vec(
    gconstpointer test_data)
{
    const TestHidlVec* test = test_data;
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(test->in, test->in_size), test->in_size, NULL);
    GBinderReaderData data;
    GBinderReader reader;
    gsize n = 0, elem = 0;

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
    g_assert(gbinder_reader_read_hidl_vec(&reader, &n, &elem) == test->data);
    g_assert(n == test->count);
    g_assert(elem == test->elemsize);

    if (test->data) {
        n = 42;
        gbinder_reader_init(&reader, &data, 0, test->in_size);
        g_assert(gbinder_reader_read_hidl_vec1(&reader, &n, test->elemsize) ==
            test->data);
        g_assert(n == test->count);

        /* Test invalid expected size */
        gbinder_reader_init(&reader, &data, 0, test->in_size);
        if (test->count) {
            g_assert(!gbinder_reader_read_hidl_vec1(&reader, NULL,
                test->elemsize + 1));
        } else {
            /* If total size is zero, we can't really check the element size */
            g_assert(gbinder_reader_read_hidl_vec1(&reader, NULL,
                test->elemsize + 1) == test->data);
        }
    } else {
        gbinder_reader_init(&reader, &data, 0, test->in_size);
        g_assert(!gbinder_reader_read_hidl_vec1(&reader, &n, test->elemsize));
    }

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
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
static const guint8 test_hidl_string_err_bad_obj [] = {
    TEST_INT32_BYTES(BINDER_TYPE_HANDLE),
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
static const guint8 test_hidl_string_err_empty [] = {
    TEST_INT32_BYTES(BINDER_TYPE_PTR),
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
    { "no-object", TEST_ARRAY_AND_SIZE(test_hidl_string_err_bad_obj), NULL },
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
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(test->in, test->in_size), test->in_size, NULL);
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

static
void
test_hidl_string_err_skip(
    gconstpointer test_data)
{
    const TestHidlStringErr* test = test_data;
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(test->in, test->in_size), test->in_size, NULL);
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
    g_assert(!gbinder_reader_skip_hidl_string(&reader));

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * fd_ok
 *==========================================================================*/

static
void
test_fd_ok(
    void)
{
    /* Using 64-bit I/O */
    const int fd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    const guint8 input[] = {
        TEST_INT32_BYTES(BINDER_TYPE_FD),
        TEST_INT32_BYTES(0x7f | BINDER_FLAG_ACCEPTS_FDS),
        TEST_INT32_BYTES(fd), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES(0)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
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

    g_assert(gbinder_reader_read_fd(&reader) == fd);
    gbinder_driver_close_fds(ipc->driver, data.objects,
        (guint8*)buf->data + buf->size);
    /* The above call must have closed the descriptor */
    g_assert(close(fd) < 0);

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * fd_shortbuf
 *==========================================================================*/

static
void
test_fd_shortbuf(
    void)
{
    /* Using 64-bit I/O */
    const guint8 input[] = {
        TEST_INT32_BYTES(BINDER_TYPE_FD),
        TEST_INT32_BYTES(0x7f | BINDER_FLAG_ACCEPTS_FDS)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
    GBinderReaderData data;
    GBinderReader reader;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);
    gbinder_reader_init(&reader, &data, 0, buf->size);

    g_assert(gbinder_reader_read_fd(&reader) < 0);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * fd_badtype
 *==========================================================================*/

static
void
test_fd_badtype(
    void)
{
    /* Using 64-bit I/O */
    const int fd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    const guint8 input[] = {
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(0x7f | BINDER_FLAG_ACCEPTS_FDS),
        TEST_INT32_BYTES(fd), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES(0)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
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

    g_assert(gbinder_reader_read_fd(&reader) < 0);
    gbinder_driver_close_fds(ipc->driver, data.objects,
        (guint8*)buf->data + buf->size);
    /* The above call doesn't close the descriptor */
    g_assert(close(fd) == 0);

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * dupfd_ok
 *==========================================================================*/

static
void
test_dupfd_ok(
    void)
{
    /* Using 64-bit I/O */
    const int fd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    const guint8 input[] = {
        TEST_INT32_BYTES(BINDER_TYPE_FD),
        TEST_INT32_BYTES(0x7f | BINDER_FLAG_ACCEPTS_FDS),
        TEST_INT32_BYTES(fd), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES(0)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
    GBinderReaderData data;
    GBinderReader reader;
    int fd2;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);
    data.objects = g_new(void*, 2);
    data.objects[0] = buf->data;
    data.objects[1] = NULL;
    gbinder_reader_init(&reader, &data, 0, buf->size);

    fd2 = gbinder_reader_read_dup_fd(&reader);
    g_assert(fd2 >= 0);
    g_assert(fd2 != fd);
    gbinder_driver_close_fds(ipc->driver, data.objects,
        (guint8*)buf->data + buf->size);
    /* The above call closes fd*/
    g_assert(close(fd) < 0);
    g_assert(close(fd2) == 0);

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * dupfd_badtype
 *==========================================================================*/

static
void
test_dupfd_badtype(
    void)
{
    /* Using 64-bit I/O */
    const int fd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    const guint8 input[] = {
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(0x7f | BINDER_FLAG_ACCEPTS_FDS),
        TEST_INT32_BYTES(fd), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES(0)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
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

    g_assert(gbinder_reader_read_dup_fd(&reader) < 0);
    gbinder_driver_close_fds(ipc->driver, data.objects,
        (guint8*)buf->data + buf->size);
    /* The above call doesn't close fd*/
    g_assert(close(fd) == 0);

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * dupfd_badfd
 *==========================================================================*/

static
void
test_dupfd_badfd(
    void)
{
    /* Using 64-bit I/O */
    const int fd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    const guint8 input[] = {
        TEST_INT32_BYTES(BINDER_TYPE_FD),
        TEST_INT32_BYTES(0x7f | BINDER_FLAG_ACCEPTS_FDS),
        TEST_INT32_BYTES(fd), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES(0)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
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

    /* Invalidate the descriptor by closing it */
    g_assert(close(fd) == 0);
    g_assert(gbinder_reader_read_dup_fd(&reader) < 0);
    gbinder_driver_close_fds(ipc->driver, data.objects,
        (guint8*)buf->data + buf->size);

    g_free(data.objects);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * hidl_string
 *==========================================================================*/

static
void
test_hidl_string(
    const guint8* input,
    gsize size,
    const guint* offsets,
    guint bufcount,
    const char* result)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver, g_memdup(input, size),
        size, NULL);
    GBinderRemoteObject* obj = NULL;
    GBinderReaderData data;
    GBinderReader reader;
    guint i;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);
    data.objects = g_new(void*, bufcount + 1);
    for (i = 0; i < bufcount; i++) {
        data.objects[i] = buf->data + offsets[i];
    }
    data.objects[i] = NULL;
    gbinder_reader_init(&reader, &data, 0, buf->size);

    g_assert(gbinder_reader_read_hidl_string_c(&reader) == result);

    g_free(data.objects);
    gbinder_remote_object_unref(obj);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

static
void
test_hidl_string1(
    void)
{
    const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&str), TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Buffer object #2 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)contents),
        TEST_INT64_BYTES(sizeof(contents)),
        TEST_INT64_BYTES(0),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET)
    };
    static const guint offsets[] = { 0, BUFFER_OBJECT_SIZE_64 };

    test_hidl_string(TEST_ARRAY_AND_SIZE(input),
        TEST_ARRAY_AND_COUNT(offsets), contents);
}

static
void
test_hidl_string2(
    void)
{
    const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&str), TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Invalid object type */
        TEST_INT32_BYTES(BINDER_TYPE_HANDLE),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)contents),
        TEST_INT64_BYTES(sizeof(contents)),
        TEST_INT64_BYTES(0),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET)
    };
    static const guint offsets[] = { 0, BUFFER_OBJECT_SIZE_64 };

    test_hidl_string(TEST_ARRAY_AND_SIZE(input),
        TEST_ARRAY_AND_COUNT(offsets), NULL);
}

static
void
test_hidl_string3(
    void)
{
    const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&str), TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* No parent */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)contents),
        TEST_INT64_BYTES(sizeof(contents)),
        TEST_INT64_BYTES(0),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET)
    };
    static const guint offsets[] = { 0, BUFFER_OBJECT_SIZE_64 };

    test_hidl_string(TEST_ARRAY_AND_SIZE(input),
        TEST_ARRAY_AND_COUNT(offsets), NULL);
}

static
void
test_hidl_string4(
    void)
{
    const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&str), TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Invalid length */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)contents),
        TEST_INT64_BYTES(sizeof(contents) - 1),
        TEST_INT64_BYTES(0),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET)
    };
    static const guint offsets[] = { 0, BUFFER_OBJECT_SIZE_64 };

    test_hidl_string(TEST_ARRAY_AND_SIZE(input),
        TEST_ARRAY_AND_COUNT(offsets), NULL);
}

static
void
test_hidl_string5(
    void)
{
    const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&str), TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Invalid pointer */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)contents + 1),
        TEST_INT64_BYTES(sizeof(contents)),
        TEST_INT64_BYTES(0),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET)
    };
    static const guint offsets[] = { 0, BUFFER_OBJECT_SIZE_64 };

    test_hidl_string(TEST_ARRAY_AND_SIZE(input),
        TEST_ARRAY_AND_COUNT(offsets), NULL);
}

static
void
test_hidl_string6(
    void)
{
    /* No NULL-terminated */
    const char contents[] = "testx";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = 4,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&str), TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Buffer object #2 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)contents),
        TEST_INT64_BYTES(5),
        TEST_INT64_BYTES(0),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET)
    };
    static const guint offsets[] = { 0, BUFFER_OBJECT_SIZE_64 };

    test_hidl_string(TEST_ARRAY_AND_SIZE(input),
        TEST_ARRAY_AND_COUNT(offsets), NULL);
}

static
void
test_hidl_string7(
    void)
{
    const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&str), TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Invalid parent offset */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)contents),
        TEST_INT64_BYTES(sizeof(contents)),
        TEST_INT64_BYTES(0),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET + 1)
    };
    static const guint offsets[] = { 0, BUFFER_OBJECT_SIZE_64 };

    test_hidl_string(TEST_ARRAY_AND_SIZE(input),
        TEST_ARRAY_AND_COUNT(offsets), NULL);
}

/*==========================================================================*
 * buffer
 *==========================================================================*/

static
void
test_buffer(
    void)
{
    /* Using 64-bit I/O */
    const int data1 = 0x1234;
    const int data2 = 0x5678;
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&data1), TEST_INT64_BYTES(sizeof(data1)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Buffer object #2 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)&data2), TEST_INT64_BYTES(sizeof(data2)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Not a buffer object */
        TEST_INT32_BYTES(BINDER_TYPE_HANDLE), TEST_INT32_BYTES(0),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0)
    };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
    GBinderRemoteObject* obj = NULL;
    GBinderReaderData data;
    GBinderReader reader;
    GBinderBuffer* res;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);
    data.objects = g_new(void*, 4);
    data.objects[0] = buf->data;
    data.objects[1] = buf->data + BUFFER_OBJECT_SIZE_64;
    data.objects[2] = buf->data + 2 * BUFFER_OBJECT_SIZE_64;
    data.objects[3] = NULL;
    gbinder_reader_init(&reader, &data, 0, buf->size);

    g_assert(gbinder_reader_skip_buffer(&reader));
    res = gbinder_reader_read_buffer(&reader);
    g_assert(res);
    g_assert(res->data == &data2);

    /* The next one is not a buffer object */
    g_assert(!gbinder_reader_skip_buffer(&reader));

    gbinder_buffer_free(res);
    g_free(data.objects);
    gbinder_remote_object_unref(obj);
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
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
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
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(input, sizeof(input)), sizeof(input), NULL);
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
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderReaderData data;
    GBinderReader reader;
    BinderObject64 obj;
    GBinderHidlVec vec;
    char** out;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    memset(&vec, 0, sizeof(vec));
    memset(&obj, 0, sizeof(obj));
    obj.type = BINDER_TYPE_PTR;
    obj.buffer.ptr = &vec;

    /* This one will fail because the buffer is one byte short */
    obj.length = sizeof(vec) - 1;
    data.buffer =  gbinder_buffer_new(ipc->driver,
        g_memdup(&obj, sizeof(obj)), sizeof(obj), NULL);
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
        g_memdup(&obj, sizeof(obj)), sizeof(obj), NULL);
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
 * hidl_string_vec
 *==========================================================================*/

static
void
test_hidl_string_vec(
    const guint8* input,
    gsize size,
    const char* const* result)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver, g_memdup(input, size),
        size, NULL);
    GBinderRemoteObject* obj = NULL;
    GBinderReaderData data;
    GBinderReader reader;
    char** out;
    guint i;

    g_assert(ipc);
    memset(&data, 0, sizeof(data));
    data.buffer = buf;
    data.reg = gbinder_ipc_object_registry(ipc);

    /* We assume that input consists only from buffer objects */
    g_assert(!(size % BUFFER_OBJECT_SIZE_64));
    data.objects = g_new(void*, size/BUFFER_OBJECT_SIZE_64 + 1);
    for (i = 0; i < size/BUFFER_OBJECT_SIZE_64; i++) {
        data.objects[i] = buf->data + i * BUFFER_OBJECT_SIZE_64;
    }
    data.objects[i] = NULL;

    gbinder_reader_init(&reader, &data, 0, buf->size);
    out = gbinder_reader_read_hidl_string_vec(&reader)
;
    if (out) {
        const guint n = g_strv_length(out);

        g_assert(result);
        g_assert(n == g_strv_length((char**)result));
        for (i = 0; i < n; i++) {
            g_assert(!g_strcmp0(out[i], result[i]));
        }
    } else {
        g_assert(!result);
    }

    g_strfreev(out);
    g_free(data.objects);
    gbinder_remote_object_unref(obj);
    gbinder_buffer_free(buf);
    gbinder_ipc_unref(ipc);
}

static
void
test_hidl_string_vec1(
    void)
{
    static const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    const GBinderHidlVec vec = {
        .data = { .ptr = &str },
        .count = 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&vec),
        TEST_INT64_BYTES(sizeof(vec)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Buffer object #2 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)&str),
        TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(1),
        TEST_INT64_BYTES(GBINDER_HIDL_VEC_BUFFER_OFFSET),
        /* Buffer object #3 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)&contents),
        TEST_INT64_BYTES(sizeof(contents)),
        TEST_INT64_BYTES(2),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET)
    };
    static const char* const result[] = { contents, NULL };

    test_hidl_string_vec(TEST_ARRAY_AND_SIZE(input), result);
}

static
void
test_hidl_string_vec2(
    void)
{
    static const char str1[] = "meh";
    static const char str2[] = "foobar";
    const GBinderHidlString str[] = {
        {
            .data = { (uintptr_t)str1 },
            .len = sizeof(str1) - 1,
            .owns_buffer = TRUE
        },{
            .data = { (uintptr_t)str2 },
            .len = sizeof(str2) - 1,
            .owns_buffer = TRUE
        }
    };
    const GBinderHidlVec vec = {
        .data = { .ptr = &str },
        .count = 2,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&vec),
        TEST_INT64_BYTES(sizeof(vec)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Buffer object #2 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)str),
        TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(1),
        TEST_INT64_BYTES(GBINDER_HIDL_VEC_BUFFER_OFFSET),
        /* Buffer object #3 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)str1),
        TEST_INT64_BYTES(sizeof(str1)),
        TEST_INT64_BYTES(2),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET),
        /* Buffer object #4 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)str2),
        TEST_INT64_BYTES(sizeof(str2)),
        TEST_INT64_BYTES(2),
        TEST_INT64_BYTES(sizeof(GBinderHidlString) +
            GBINDER_HIDL_STRING_BUFFER_OFFSET)
                        
    };
    static const char* const result[] = { str1, str2, NULL };

    test_hidl_string_vec(TEST_ARRAY_AND_SIZE(input), result);
}

static
void
test_hidl_string_vec3(
    void)
{
    static const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    const GBinderHidlVec vec = {
        .data = { .ptr = &str },
        .count = 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&vec),
        TEST_INT64_BYTES(sizeof(vec)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Buffer object #2 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)&str),
        TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(1),
        TEST_INT64_BYTES(GBINDER_HIDL_VEC_BUFFER_OFFSET)
        /* The next buffer is missing */
    };

    test_hidl_string_vec(TEST_ARRAY_AND_SIZE(input), NULL);
}

static
void
test_hidl_string_vec4(
    void)
{
    static const char contents[] = "test";
    const GBinderHidlString str = {
        .data = { (uintptr_t)contents },
        .len = sizeof(contents) - 1,
        .owns_buffer = TRUE
    };
    const GBinderHidlVec vec = {
        .data = { .ptr = &str },
        .count = 1,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&vec),
        TEST_INT64_BYTES(sizeof(vec)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* The next buffer is missing */
    };

    test_hidl_string_vec(TEST_ARRAY_AND_SIZE(input), NULL);
}

static
void
test_hidl_string_vec5(
    void)
{
    static const char str1[] = "meh";
    static const char str2[] = "foobar";
    const GBinderHidlString str[] = {
        {
            .data = { (uintptr_t)str1 },
            .len = sizeof(str1) - 1,
            .owns_buffer = TRUE
        },{
            .data = { (uintptr_t)str2 },
            .len = sizeof(str2) - 1,
            .owns_buffer = TRUE
        }
    };
    const GBinderHidlVec vec = {
        .data = { .ptr = &str },
        .count = 2,
        .owns_buffer = TRUE
    };
    /* Using 64-bit I/O */
    const guint8 input[] = {
        /* Buffer object #1 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(0),
        TEST_INT64_BYTES((uintptr_t)&vec),
        TEST_INT64_BYTES(sizeof(vec)),
        TEST_INT64_BYTES(0), TEST_INT64_BYTES(0),
        /* Buffer object #2 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)str),
        TEST_INT64_BYTES(sizeof(str)),
        TEST_INT64_BYTES(1),
        TEST_INT64_BYTES(GBINDER_HIDL_VEC_BUFFER_OFFSET),
        /* Buffer object #3 */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)str1),
        TEST_INT64_BYTES(sizeof(str1)),
        TEST_INT64_BYTES(2),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET),
        /* Buffer object #4 (with invalid offset) */
        TEST_INT32_BYTES(BINDER_TYPE_PTR),
        TEST_INT32_BYTES(BINDER_BUFFER_FLAG_HAS_PARENT),
        TEST_INT64_BYTES((uintptr_t)str2),
        TEST_INT64_BYTES(sizeof(str2)),
        TEST_INT64_BYTES(2),
        TEST_INT64_BYTES(GBINDER_HIDL_STRING_BUFFER_OFFSET)
                        
    };

    test_hidl_string_vec(TEST_ARRAY_AND_SIZE(input), NULL);
}

/*==========================================================================*
 * byte_array
 *==========================================================================*/

static
void
test_byte_array(
    void)
{
    const char in_data[] = "1234abcd";
    gint32 in_len = sizeof(in_data) - 1;
    const void* out_data = NULL;
    gsize out_len = 0;
    void* tmp;
    gsize tmp_len = sizeof(in_len) + in_len;
    gint32 null_len = -1;

    GBinderDriver* driver;
    GBinderReader reader;
    GBinderReaderData data;

    /* test for failed read (wrong len part of byte array) */
    g_assert((driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL)));
    tmp = g_malloc0(1);

    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, tmp, 1, NULL);
    gbinder_reader_init(&reader, &data, 0, 1);

    g_assert(!gbinder_reader_read_byte_array(&reader, &out_len));
    g_assert(!gbinder_reader_at_end(&reader));
    g_assert(out_len == 0);

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);

    /* test for failed read (wrong data part of byte array) */
    g_assert((driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL)));
    tmp = g_malloc0(in_len - 1);
    memcpy(tmp, &in_len, sizeof(in_len));

    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, tmp, in_len - 1, NULL);
    gbinder_reader_init(&reader, &data, 0, in_len - 1);

    g_assert(!gbinder_reader_read_byte_array(&reader, &out_len));
    g_assert(!gbinder_reader_at_end(&reader));
    g_assert(out_len == 0);

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);

    /* test for empty (len 0) byte array */
    g_assert((driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL)));
    tmp = g_malloc0(sizeof(null_len));
    memcpy(tmp, &null_len, sizeof(null_len));

    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, tmp, sizeof(null_len), NULL);
    gbinder_reader_init(&reader, &data, 0, sizeof(null_len));

    g_assert((out_data = gbinder_reader_read_byte_array(&reader, &out_len)));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(out_len == 0);

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);

    /* test for data */
    g_assert((driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL)));
    tmp = g_malloc0(tmp_len);
    memcpy(tmp, &in_len, sizeof(in_len));
    memcpy(tmp + sizeof(in_len), in_data, in_len);

    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, tmp, tmp_len, NULL);
    gbinder_reader_init(&reader, &data, 0, tmp_len);

    g_assert((out_data = gbinder_reader_read_byte_array(&reader, &out_len)));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert((gsize)in_len == out_len);
    g_assert(memcmp(in_data, out_data, in_len) == 0);

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * copy
 *==========================================================================*/

static
void
test_copy(
    void)
{
    const char in_data1[] = "12345678";
    const char in_data2[] = "abcdefgh";
    gint32 in_len1 = sizeof(in_data1) - 1;
    gint32 in_len2 = sizeof(in_data2) - 1;
    const void* out_data = NULL;
    gsize out_len = 0;
    void* tmp;
    guint8* ptr;
    gsize tmp_len = 2 * sizeof(guint32) + in_len1 + in_len2;

    GBinderDriver* driver;
    GBinderReader reader;
    GBinderReader reader2;
    GBinderReaderData data;

    /* test for data */
    g_assert((driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL)));
    ptr = tmp = g_malloc0(tmp_len);
    memcpy(ptr, &in_len1, sizeof(in_len1));
    ptr += sizeof(in_len1);
    memcpy(ptr, in_data1, in_len1);
    ptr += in_len1;
    memcpy(ptr, &in_len2, sizeof(in_len2));
    ptr += sizeof(in_len2);
    memcpy(ptr, in_data2, in_len2);

    memset(&data, 0, sizeof(data));
    data.buffer = gbinder_buffer_new(driver, tmp, tmp_len, NULL);
    gbinder_reader_init(&reader, &data, 0, tmp_len);

    /* Read the first array */
    g_assert((out_data = gbinder_reader_read_byte_array(&reader, &out_len)));
    g_assert((gsize)in_len1 == out_len);
    g_assert(memcmp(in_data1, out_data, in_len1) == 0);

    /* Copy the reader */
    gbinder_reader_copy(&reader2, &reader);

    /* Read both and compare the output */
    g_assert((out_data = gbinder_reader_read_byte_array(&reader, &out_len)));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert((gsize)in_len2 == out_len);
    g_assert(memcmp(in_data2, out_data, in_len2) == 0);

    g_assert((out_data = gbinder_reader_read_byte_array(&reader2, &out_len)));
    g_assert(gbinder_reader_at_end(&reader2));
    g_assert((gsize)in_len2 == out_len);
    g_assert(memcmp(in_data2, out_data, in_len2) == 0);

    gbinder_buffer_free(data.buffer);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/reader/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("empty"), test_empty);
    g_test_add_func(TEST_("byte"), test_byte);
    g_test_add_func(TEST_("bool"), test_bool);
    g_test_add_func(TEST_("int32"), test_int32);
    g_test_add_func(TEST_("int64"), test_int64);
    g_test_add_func(TEST_("float"), test_float);
    g_test_add_func(TEST_("double"), test_double);

    for (i = 0; i < G_N_ELEMENTS(test_string8_tests); i++) {
        const TestStringData* test = test_string8_tests + i;
        char* path = g_strconcat(TEST_("string8/"), test->name, NULL);

        g_test_add_data_func(path, test, test_string8);
        g_free(path);
    }

    g_test_add_func(TEST_("string16/null"), test_string16_null);
    for (i = 0; i < G_N_ELEMENTS(test_string16_tests); i++) {
        const TestStringData* test = test_string16_tests + i;
        char* path = g_strconcat(TEST_("string16/"), test->name, NULL);

        g_test_add_data_func(path, test, test_string16);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_hidl_struct_tests); i++) {
        const TestHidlStruct* test = test_hidl_struct_tests + i;
        char* path = g_strconcat(TEST_("hidl_struct/"), test->name,
            NULL);

        g_test_add_data_func(path, test, test_hidl_struct);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_hidl_vec_tests); i++) {
        const TestHidlVec* test = test_hidl_vec_tests + i;
        char* path = g_strconcat(TEST_("hidl_vec/"), test->name,
            NULL);

        g_test_add_data_func(path, test, test_hidl_vec);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_hidl_string_err_tests); i++) {
        const TestHidlStringErr* test = test_hidl_string_err_tests + i;
        char* path = g_strconcat(TEST_("hidl_string/err-"), test->name,
            NULL);

        g_test_add_data_func(path, test, test_hidl_string_err);
        g_free(path);

        path = g_strconcat(TEST_("hidl_string/err-skip-"), test->name,
            NULL);
        g_test_add_data_func(path, test, test_hidl_string_err_skip);
        g_free(path);
    }

    g_test_add_func(TEST_("fd/ok"), test_fd_ok);
    g_test_add_func(TEST_("fd/shortbuf"), test_fd_shortbuf);
    g_test_add_func(TEST_("fd/badtype"), test_fd_badtype);
    g_test_add_func(TEST_("dupfd/ok"), test_dupfd_ok);
    g_test_add_func(TEST_("dupfd/badtype"), test_dupfd_badtype);
    g_test_add_func(TEST_("dupfd/badfd"), test_dupfd_badfd);
    g_test_add_func(TEST_("hidl_string/1"), test_hidl_string1);
    g_test_add_func(TEST_("hidl_string/2"), test_hidl_string2);
    g_test_add_func(TEST_("hidl_string/3"), test_hidl_string3);
    g_test_add_func(TEST_("hidl_string/4"), test_hidl_string4);
    g_test_add_func(TEST_("hidl_string/5"), test_hidl_string5);
    g_test_add_func(TEST_("hidl_string/6"), test_hidl_string6);
    g_test_add_func(TEST_("hidl_string/7"), test_hidl_string7);
    g_test_add_func(TEST_("buffer"), test_buffer);
    g_test_add_func(TEST_("object/valid"), test_object);
    g_test_add_func(TEST_("object/invalid"), test_object_invalid);
    g_test_add_func(TEST_("object/no_reg"), test_object_no_reg);
    g_test_add_func(TEST_("vec"), test_vec);
    g_test_add_func(TEST_("hidl_string_vec/1"), test_hidl_string_vec1);
    g_test_add_func(TEST_("hidl_string_vec/2"), test_hidl_string_vec2);
    g_test_add_func(TEST_("hidl_string_vec/3"), test_hidl_string_vec3);
    g_test_add_func(TEST_("hidl_string_vec/4"), test_hidl_string_vec4);
    g_test_add_func(TEST_("hidl_string_vec/5"), test_hidl_string_vec5);
    g_test_add_func(TEST_("byte_array"), test_byte_array);
    g_test_add_func(TEST_("copy"), test_copy);
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
