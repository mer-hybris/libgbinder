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
#include "test_binder.h"

#include "gbinder_local_object.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_output_data.h"
#include "gbinder_buffer_p.h"
#include "gbinder_driver.h"
#include "gbinder_writer.h"
#include "gbinder_io.h"
#include "gbinder_ipc.h"

#include <gutil_intarray.h>

static TestOpt test_opt;

#define BUFFER_OBJECT_SIZE_32 (24)
#define BUFFER_OBJECT_SIZE_64 (GBINDER_MAX_BUFFER_OBJECT_SIZE)
#define BINDER_OBJECT_SIZE_32 (16)
#define BINDER_OBJECT_SIZE_64 (GBINDER_MAX_BINDER_OBJECT_SIZE)

static
void
test_int_inc(
    void* data)
{
    (*((int*)data))++;
}

static
GBinderBuffer*
test_buffer_from_bytes(
    GBinderDriver* driver,
    const GByteArray* bytes)
{
    /* Prevent double free */
    test_binder_set_destroy(gbinder_driver_fd(driver), bytes->data, NULL);
    return gbinder_buffer_new(driver, bytes->data, bytes->len, NULL);
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
    int count = 0;

    g_assert(!gbinder_local_reply_new(NULL));
    g_assert(!gbinder_local_reply_ref(NULL));
    gbinder_local_reply_unref(NULL);
    gbinder_local_reply_init_writer(NULL, NULL);
    gbinder_local_reply_init_writer(NULL, &writer);
    g_assert(!gbinder_local_reply_data(NULL));
    g_assert(!gbinder_local_reply_contents(NULL));
    g_assert(!gbinder_local_reply_set_contents(NULL, NULL, NULL));

    gbinder_local_reply_cleanup(NULL, NULL, &count);
    gbinder_local_reply_cleanup(NULL, test_int_inc, &count);
    g_assert(count == 1);

    g_assert(!gbinder_local_reply_append_bool(NULL, FALSE));
    g_assert(!gbinder_local_reply_append_int32(NULL, 0));
    g_assert(!gbinder_local_reply_append_int64(NULL, 0));
    g_assert(!gbinder_local_reply_append_float(NULL, 0));
    g_assert(!gbinder_local_reply_append_double(NULL, 0));
    g_assert(!gbinder_local_reply_append_string8(NULL, NULL));
    g_assert(!gbinder_local_reply_append_string16(NULL, NULL));
    g_assert(!gbinder_local_reply_append_hidl_string(NULL, NULL));
    g_assert(!gbinder_local_reply_append_hidl_string_vec(NULL, NULL, 0));
    g_assert(!gbinder_local_reply_append_local_object(NULL, NULL));
    g_assert(!gbinder_local_reply_append_remote_object(NULL, NULL));
}

/*==========================================================================*
 * cleanup
 *==========================================================================*/

static
void
test_cleanup(
    void)
{
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    int count = 0;

    gbinder_local_reply_cleanup(reply, NULL, &count);
    gbinder_local_reply_cleanup(reply, test_int_inc, &count);
    gbinder_local_reply_cleanup(reply, test_int_inc, &count);
    g_assert(!count);

    gbinder_local_reply_unref(reply);
    g_assert(count == 2);
}

/*==========================================================================*
 * bool
 *==========================================================================*/

static
void
test_bool(
    void)
{
    static const guint8 output_true[] = { 0x01, 0x00, 0x00, 0x00 };
    static const guint8 output_false[] = { 0x00, 0x00, 0x00, 0x00 };
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;

    gbinder_local_reply_append_bool(reply, FALSE);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(output_false));
    g_assert(!memcmp(data->bytes->data, output_false, data->bytes->len));
    gbinder_local_reply_unref(reply);

    reply = gbinder_local_reply_new(&gbinder_io_32);
    gbinder_local_reply_append_bool(reply, TRUE);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(output_true));
    g_assert(!memcmp(data->bytes->data, output_true, data->bytes->len));
    gbinder_local_reply_unref(reply);

    reply = gbinder_local_reply_new(&gbinder_io_32);
    gbinder_local_reply_append_bool(reply, 42);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(output_true));
    g_assert(!memcmp(data->bytes->data, output_true, data->bytes->len));
    gbinder_local_reply_unref(reply);
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
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_reply_append_int32(reply, value);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    g_assert(gbinder_local_reply_ref(reply) == reply);
    gbinder_local_reply_unref(reply);
    gbinder_local_reply_unref(reply);

    /* Same with writer */
    reply = gbinder_local_reply_new(&gbinder_io_32);
    gbinder_local_reply_init_writer(reply, &writer);
    gbinder_writer_append_int32(&writer, value);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * int64
 *==========================================================================*/

static
void
test_int64(
    void)
{
    const guint64 value = 123456789;
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;

    gbinder_local_reply_append_int64(reply, value);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * float
 *==========================================================================*/

static
void
test_float(
    void)
{
    const gfloat value = 123456789;
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;

    gbinder_local_reply_append_float(reply, value);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * double
 *==========================================================================*/

static
void
test_double(
    void)
{
    const gdouble value = 123456789;
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;

    gbinder_local_reply_append_double(reply, value);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(value));
    g_assert(!memcmp(data->bytes->data, &value, data->bytes->len));
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * string8
 *==========================================================================*/

static
void
test_string8(
    void)
{
    /* The size of the string gets aligned at 4-byte boundary */
    static const char input[] = "test";
    static const guint8 output[] = { 't', 'e', 's', 't', 0, 0, 0, 0 };
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;

    gbinder_local_reply_append_string8(reply, input);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(output));
    g_assert(!memcmp(data->bytes->data, output, data->bytes->len));
    gbinder_local_reply_unref(reply);

    /* NULL string doesn't get encoded at all (should it be?) */
    reply = gbinder_local_reply_new(&gbinder_io_32);
    gbinder_local_reply_append_string8(reply, NULL);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(!data->bytes->len);
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * string16
 *==========================================================================*/

static
void
test_string16(
    void)
{
    static const char input[] = "x";
    static const guint8 output[] = {
        TEST_INT32_BYTES(1),
        TEST_INT16_BYTES('x'), 0x00, 0x00
    };
    const gint32 null_output = -1;
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;

    gbinder_local_reply_append_string16(reply, input);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(output));
    g_assert(!memcmp(data->bytes->data, output, data->bytes->len));
    gbinder_local_reply_unref(reply);

    /* NULL string gets encoded as -1 */
    reply = gbinder_local_reply_new(&gbinder_io_32);
    gbinder_local_reply_append_string16(reply, NULL);
    data = gbinder_local_reply_data(reply);
    g_assert(!gbinder_output_data_offsets(data));
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == sizeof(null_output));
    g_assert(!memcmp(data->bytes->data, &null_output, data->bytes->len));
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * hidl_string
 *==========================================================================*/

static
void
test_hidl_string(
    void)
{
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;
    GUtilIntArray* offsets;

    gbinder_local_reply_append_hidl_string(reply, NULL);
    data = gbinder_local_reply_data(reply);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets->count == 2);
    g_assert(offsets->data[0] == 0);
    g_assert(gbinder_output_data_buffers_size(data)==sizeof(GBinderHidlString));
    g_assert(data->bytes->len == 2*BUFFER_OBJECT_SIZE_32);
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * hidl_string_vec
 *==========================================================================*/

static
void
test_hidl_string_vec(
    void)
{
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;
    GUtilIntArray* offsets;

    gbinder_local_reply_append_hidl_string_vec(reply, NULL, 0);
    data = gbinder_local_reply_data(reply);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets->count == 2);
    g_assert(offsets->data[0] == 0);
    g_assert(gbinder_output_data_buffers_size(data) == sizeof(GBinderHidlVec));
    g_assert(data->bytes->len == 2*BUFFER_OBJECT_SIZE_32);
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * local_object
 *==========================================================================*/

static
void
test_local_object(
    void)
{
    GBinderLocalReply* reply;
    GBinderOutputData* data;
    GUtilIntArray* offsets;
    GBinderIpc* ipc = gbinder_ipc_new(NULL);
    const char* const ifaces[] = { "android.hidl.base@1.0::IBase", NULL };
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, ifaces, NULL, NULL);

    /* Append a real object (64-bit I/O is used by test_binder.c) */
    reply = gbinder_local_object_new_reply(obj);
    gbinder_local_reply_append_local_object(reply, obj);
    data = gbinder_local_reply_data(reply);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 0);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == BINDER_OBJECT_SIZE_64);
    gbinder_local_reply_unref(reply);

    /* Append NULL object (with 32-bit I/O module) */
    reply = gbinder_local_reply_new(&gbinder_io_32);
    gbinder_local_reply_append_local_object(reply, NULL);
    data = gbinder_local_reply_data(reply);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 0);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == BINDER_OBJECT_SIZE_32);
    gbinder_local_reply_unref(reply);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * remote_object
 *==========================================================================*/

static
void
test_remote_object(
    void)
{
    GBinderLocalReply* reply = gbinder_local_reply_new(&gbinder_io_32);
    GBinderOutputData* data;
    GUtilIntArray* offsets;

    gbinder_local_reply_append_remote_object(reply, NULL);
    data = gbinder_local_reply_data(reply);
    offsets = gbinder_output_data_offsets(data);
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 0);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(data->bytes->len == BINDER_OBJECT_SIZE_32);
    gbinder_local_reply_unref(reply);
}

/*==========================================================================*
 * remote_reply
 *==========================================================================*/

static
void
test_remote_reply(
    void)
{
    /* The size of the string gets aligned at 4-byte boundary */
    static const char input[] = "test";
    static const guint8 output[] = { 't', 'e', 's', 't', 0, 0, 0, 0 };
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    const GBinderIo* io = gbinder_driver_io(driver);
    GBinderLocalReply* req = gbinder_local_reply_new(io);
    GBinderLocalReply* req2;
    GBinderOutputData* data2;
    const GByteArray* bytes;
    const GByteArray* bytes2;
    GBinderBuffer* buffer;

    gbinder_local_reply_append_string8(req, input);
    bytes = gbinder_local_reply_data(req)->bytes;

    /* Copy flat structures (no binder objects) */
    buffer = test_buffer_from_bytes(driver, bytes);
    req2 = gbinder_local_reply_new(io);
    g_assert(gbinder_local_reply_set_contents(req2, buffer, NULL) == req2);
    gbinder_buffer_free(buffer);

    data2 = gbinder_local_reply_data(req2);
    bytes2 = data2->bytes;
    g_assert(!gbinder_output_data_offsets(data2));
    g_assert(!gbinder_output_data_buffers_size(data2));
    g_assert(bytes2->len == sizeof(output));
    g_assert(!memcmp(bytes2->data, output, bytes2->len));

    gbinder_local_reply_unref(req2);
    gbinder_local_reply_unref(req);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/local_reply/"

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "null", test_null);
    g_test_add_func(TEST_PREFIX "cleanup", test_cleanup);
    g_test_add_func(TEST_PREFIX "bool", test_bool);
    g_test_add_func(TEST_PREFIX "int32", test_int32);
    g_test_add_func(TEST_PREFIX "int64", test_int64);
    g_test_add_func(TEST_PREFIX "float", test_float);
    g_test_add_func(TEST_PREFIX "double", test_double);
    g_test_add_func(TEST_PREFIX "string8", test_string8);
    g_test_add_func(TEST_PREFIX "string16", test_string16);
    g_test_add_func(TEST_PREFIX "hidl_string", test_hidl_string);
    g_test_add_func(TEST_PREFIX "hidl_string_vec", test_hidl_string_vec);
    g_test_add_func(TEST_PREFIX "local_object", test_local_object);
    g_test_add_func(TEST_PREFIX "remote_object", test_remote_object);
    g_test_add_func(TEST_PREFIX "remote_reply", test_remote_reply);
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
