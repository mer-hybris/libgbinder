/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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
#include "gbinder_reader.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_remote_reply_p.h"
#include "gbinder_object_registry.h"
#include "gbinder_output_data.h"

#include <gutil_intarray.h>

static TestOpt test_opt;

#define BINDER_TYPE_BINDER GBINDER_FOURCC('s', 'b', '*', 0x85)
#define BINDER_OBJECT_SIZE_64 (GBINDER_MAX_BINDER_OBJECT_SIZE)

/*==========================================================================*
 * Dummy GBinderObjectRegistry functions
 *==========================================================================*/

static
void
reg_dummy_ref_unref(
    GBinderObjectRegistry* reg)
{
}

static
GBinderLocalObject*
reg_dummy_get_local(
    GBinderObjectRegistry* reg,
    void* pointer)
{
    return NULL;
}

static
GBinderRemoteObject*
reg_dummy_get_remote(
    GBinderObjectRegistry* reg,
    guint32 handle,
    REMOTE_REGISTRY_CREATE create)
{
    return NULL;
}

static GBinderObjectRegistryFunctions reg_dummy_fn = {
    .ref = reg_dummy_ref_unref,
    .unref = reg_dummy_ref_unref,
    .get_local = reg_dummy_get_local,
    .get_remote = reg_dummy_get_remote
};

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    GBinderReader reader;

    g_assert(!gbinder_remote_reply_ref(NULL));
    gbinder_remote_reply_unref(NULL);
    gbinder_remote_reply_set_data(NULL, NULL);
    gbinder_remote_reply_init_reader(NULL, &reader);
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(gbinder_remote_reply_is_empty(NULL));
    g_assert(!gbinder_remote_reply_copy_to_local(NULL));
    g_assert(!gbinder_remote_reply_read_int32(NULL, NULL));
    g_assert(!gbinder_remote_reply_read_uint32(NULL, NULL));
    g_assert(!gbinder_remote_reply_read_int64(NULL, NULL));
    g_assert(!gbinder_remote_reply_read_uint64(NULL, NULL));
    g_assert(!gbinder_remote_reply_read_string8(NULL));
    g_assert(!gbinder_remote_reply_read_string16(NULL));
    g_assert(!gbinder_remote_reply_read_object(NULL));
}

/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry reg = { &reg_dummy_fn, gbinder_driver_io(driver) };
    GBinderRemoteReply* reply = gbinder_remote_reply_new(&reg);

    gbinder_remote_reply_set_data(reply,
        gbinder_buffer_new(driver, NULL, 0, NULL));

    g_assert(gbinder_remote_reply_is_empty(reply));
    gbinder_remote_reply_unref(reply);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    GBinderReader reader;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry reg = { &reg_dummy_fn, gbinder_driver_io(driver) };
    GBinderRemoteReply* reply = gbinder_remote_reply_new(&reg);

    gbinder_remote_reply_init_reader(reply, &reader);
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(gbinder_remote_reply_is_empty(reply));
    g_assert(gbinder_remote_reply_ref(reply) == reply);
    g_assert(!gbinder_remote_reply_read_object(reply));
    gbinder_remote_reply_unref(reply);
    gbinder_remote_reply_unref(reply);
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
    static const guint8 reply_data [] = {
        TEST_INT32_BYTES(42)
    };
    guint32 out1 = 0;
    gint32 out2 = 0;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry reg = { &reg_dummy_fn, gbinder_driver_io(driver) };
    GBinderRemoteReply* reply = gbinder_remote_reply_new(&reg);

    gbinder_remote_reply_set_data(reply, gbinder_buffer_new(driver,
        g_memdup(reply_data, sizeof(reply_data)), sizeof(reply_data), NULL));

    g_assert(!gbinder_remote_reply_is_empty(reply));
    g_assert(gbinder_remote_reply_read_uint32(reply, &out1));
    g_assert(gbinder_remote_reply_read_int32(reply, &out2));
    g_assert(out1 == 42);
    g_assert(out2 == 42u);

    gbinder_remote_reply_unref(reply);
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
    static const guint8 reply_data [] = {
        TEST_INT64_BYTES(42)
    };
    guint64 out1 = 0;
    gint64 out2 = 0;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry reg = { &reg_dummy_fn, gbinder_driver_io(driver) };
    GBinderRemoteReply* reply = gbinder_remote_reply_new(&reg);

    gbinder_remote_reply_set_data(reply, gbinder_buffer_new(driver,
        g_memdup(reply_data, sizeof(reply_data)), sizeof(reply_data), NULL));

    g_assert(!gbinder_remote_reply_is_empty(reply));
    g_assert(gbinder_remote_reply_read_uint64(reply, &out1));
    g_assert(gbinder_remote_reply_read_int64(reply, &out2));
    g_assert(out1 == 42);
    g_assert(out2 == 42u);

    gbinder_remote_reply_unref(reply);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * string8
 *==========================================================================*/

static
void
test_string8(
    void)
{
    static const guint8 reply_data [] = {
        'b', 'a', 'r', 0x00
    };
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry reg = { &reg_dummy_fn, gbinder_driver_io(driver) };
    GBinderRemoteReply* reply = gbinder_remote_reply_new(&reg);

    gbinder_remote_reply_set_data(reply, gbinder_buffer_new(driver,
        g_memdup(reply_data, sizeof(reply_data)), sizeof(reply_data), NULL));

    g_assert(!gbinder_remote_reply_is_empty(reply));
    g_assert(!g_strcmp0(gbinder_remote_reply_read_string8(reply), "bar"));

    gbinder_remote_reply_unref(reply);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * string16
 *==========================================================================*/

static
void
test_string16(
    void)
{
    static const guint8 reply_data [] = {
        TEST_INT32_BYTES(3),
        TEST_INT16_BYTES('b'), TEST_INT16_BYTES('a'),
        TEST_INT16_BYTES('r'), 0x00, 0x00
    };
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry reg = { &reg_dummy_fn, gbinder_driver_io(driver) };
    GBinderRemoteReply* reply = gbinder_remote_reply_new(&reg);
    char* str;

    gbinder_remote_reply_set_data(reply, gbinder_buffer_new(driver,
        g_memdup(reply_data, sizeof(reply_data)), sizeof(reply_data), NULL));

    g_assert(!gbinder_remote_reply_is_empty(reply));
    str = gbinder_remote_reply_read_string16(reply);
    g_assert(!g_strcmp0(str, "bar"));
    g_free(str);

    gbinder_remote_reply_unref(reply);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * to_local
 *==========================================================================*/

static
void
test_to_local(
    void)
{
    static const guint8 reply_bytes [] = {
        /* 32-bit integer */
        TEST_INT32_BYTES(42),
        /* 64-bit NULL flat_binder_object */
        TEST_INT32_BYTES(BINDER_TYPE_BINDER),   /* hdr.type */
        TEST_INT32_BYTES(0x17f),                /* flags */
        TEST_INT64_BYTES(0),                    /* handle */
        TEST_INT64_BYTES(0)                     /* cookie */
    };
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderDriver* driver = gbinder_driver_new(dev, NULL);
    GBinderObjectRegistry reg = { &reg_dummy_fn, gbinder_driver_io(driver) };
    GBinderRemoteReply* rr = gbinder_remote_reply_new(&reg);
    GBinderLocalReply* lr;
    GBinderOutputData* data;
    const GByteArray* bytes;
    GUtilIntArray* offsets;
    guint8* reply_data = g_memdup(reply_bytes, sizeof(reply_bytes));
    void** objects = g_new0(void*, 2);

    /* Skip the 32-bit integer */
    objects[0] = reply_data + 4;
    gbinder_remote_reply_set_data(rr, gbinder_buffer_new(driver, reply_data,
        sizeof(reply_bytes), objects));

    /* Convert to GBinderLocalReply */
    lr = gbinder_remote_reply_copy_to_local(rr);
    data = gbinder_local_reply_data(lr);
    offsets = gbinder_output_data_offsets(data);
    bytes = data->bytes;
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 4);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(bytes->len == sizeof(reply_bytes));

    gbinder_remote_reply_unref(rr);
    gbinder_local_reply_unref(lr);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/remote_reply/"

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "null", test_null);
    g_test_add_func(TEST_PREFIX "empty", test_empty);
    g_test_add_func(TEST_PREFIX "basic", test_basic);
    g_test_add_func(TEST_PREFIX "int32", test_int32);
    g_test_add_func(TEST_PREFIX "int64", test_int64);
    g_test_add_func(TEST_PREFIX "string8", test_string8);
    g_test_add_func(TEST_PREFIX "string16", test_string16);
    g_test_add_func(TEST_PREFIX "to_local", test_to_local);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Remote Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
