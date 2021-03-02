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
#include "gbinder_remote_request_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_local_request_p.h"
#include "gbinder_object_converter.h"
#include "gbinder_output_data.h"
#include "gbinder_io.h"

#include <gutil_intarray.h>

static TestOpt test_opt;

#define STRICT_MODE_PENALTY_GATHER (0x40 << 16)
#define BINDER_RPC_FLAGS (STRICT_MODE_PENALTY_GATHER)

#define TEST_RPC_IFACE "foo"
#define TEST_RPC_HEADER \
    TEST_INT32_BYTES(BINDER_RPC_FLAGS), \
    TEST_INT32_BYTES(3), \
    TEST_INT16_BYTES('f'), TEST_INT16_BYTES('o'), \
    TEST_INT16_BYTES('o'), 0x00, 0x00
#define HIDL_RPC_HEADER \
    'f', 'o', 'o', 0x00

#define BINDER_TYPE_BINDER GBINDER_FOURCC('s', 'b', '*', 0x85)
#define BINDER_OBJECT_SIZE_64 (GBINDER_MAX_BINDER_OBJECT_SIZE)

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    GBinderReader reader;

    g_assert(!gbinder_remote_request_ref(NULL));
    gbinder_remote_request_unref(NULL);
    gbinder_remote_request_set_data(NULL, 0, NULL);
    gbinder_remote_request_init_reader(NULL, &reader);
    gbinder_remote_request_block(NULL);
    gbinder_remote_request_complete(NULL, NULL, 0);
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(!gbinder_remote_request_interface(NULL));
    g_assert(!gbinder_remote_request_copy_to_local(NULL));
    g_assert(!gbinder_remote_request_convert_to_local(NULL, NULL));
    g_assert(gbinder_remote_request_sender_pid(NULL) == (pid_t)(-1));
    g_assert(gbinder_remote_request_sender_euid(NULL) == (uid_t)(-1));
    g_assert(!gbinder_remote_request_read_int32(NULL, NULL));
    g_assert(!gbinder_remote_request_read_uint32(NULL, NULL));
    g_assert(!gbinder_remote_request_read_int64(NULL, NULL));
    g_assert(!gbinder_remote_request_read_uint64(NULL, NULL));
    g_assert(!gbinder_remote_request_read_string8(NULL));
    g_assert(!gbinder_remote_request_read_string16(NULL));
    g_assert(!gbinder_remote_request_read_object(NULL));
    g_assert(!gbinder_object_converter_handle_to_local(NULL, 0));
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
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL,
        gbinder_rpc_protocol_for_device(NULL), 0, 0);

    /* These two calls are wrong but won't cause problems: */
    gbinder_remote_request_block(req);
    gbinder_remote_request_complete(req, NULL, 0);

    gbinder_remote_request_init_reader(req, &reader);
    g_assert(gbinder_reader_at_end(&reader));
    g_assert(!gbinder_remote_request_interface(req));
    g_assert(gbinder_remote_request_ref(req) == req);
    g_assert(!gbinder_remote_request_read_object(req));
    gbinder_remote_request_unref(req);
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * int32
 *==========================================================================*/

static
void
test_int32(
    void)
{
    static const guint8 req_data [] = {
        TEST_RPC_HEADER,
        TEST_INT32_BYTES(42)
    };
    guint32 out1 = 0;
    gint32 out2 = 0;
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderDriver* driver = gbinder_driver_new(dev, NULL);
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL,
        gbinder_rpc_protocol_for_device(dev), 0, 0);

    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION,
        gbinder_buffer_new(driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));

    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_RPC_IFACE));
    g_assert(gbinder_remote_request_read_uint32(req, &out1));
    g_assert(gbinder_remote_request_read_int32(req, &out2));
    g_assert(out1 == 42);
    g_assert(out2 == 42u);

    gbinder_remote_request_unref(req);
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
    static const guint8 req_data [] = {
        TEST_RPC_HEADER,
        TEST_INT64_BYTES(42)
    };
    guint64 out1 = 0;
    gint64 out2 = 0;
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderDriver* driver = gbinder_driver_new(dev, NULL);
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL,
        gbinder_rpc_protocol_for_device(dev), 0, 0);

    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION,
        gbinder_buffer_new(driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));

    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_RPC_IFACE));
    g_assert(gbinder_remote_request_read_uint64(req, &out1));
    g_assert(gbinder_remote_request_read_int64(req, &out2));
    g_assert(out1 == 42);
    g_assert(out2 == 42u);

    gbinder_remote_request_unref(req);
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
    static const guint8 req_data [] = {
        TEST_RPC_HEADER,
        'b', 'a', 'r', 0x00
    };
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderDriver* driver = gbinder_driver_new(dev, NULL);
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL,
        gbinder_rpc_protocol_for_device(dev), 0, 0);

    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION,
        gbinder_buffer_new(driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));

    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_RPC_IFACE));
    g_assert(!g_strcmp0(gbinder_remote_request_read_string8(req), "bar"));

    gbinder_remote_request_unref(req);
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
    static const guint8 req_data [] = {
        TEST_RPC_HEADER,
        TEST_INT32_BYTES(3),
        TEST_INT16_BYTES('b'), TEST_INT16_BYTES('a'),
        TEST_INT16_BYTES('r'), 0x00, 0x00
    };
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderDriver* driver = gbinder_driver_new(dev, NULL);
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL,
        gbinder_rpc_protocol_for_device(dev), 0, 0);
    char* str;

    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION,
        gbinder_buffer_new(driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));

    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_RPC_IFACE));
    str = gbinder_remote_request_read_string16(req);
    g_assert(!g_strcmp0(str, "bar"));
    g_free(str);

    gbinder_remote_request_unref(req);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * to_local
 *==========================================================================*/

static
GBinderLocalObject*
test_to_local_convert_none(
    GBinderObjectConverter* convert,
    guint32 handle)
{
    return NULL;
}

static
void
test_to_local(
    void)
{
    static const guint8 request_data [] = {
        TEST_RPC_HEADER,
        /* 32-bit integer */
        TEST_INT32_BYTES(42),
        /* 64-bit NULL flat_binder_object */
        TEST_INT32_BYTES(BINDER_TYPE_BINDER),   /* hdr.type */
        TEST_INT32_BYTES(0x17f),                /* flags */
        TEST_INT64_BYTES(0),                    /* handle */
        TEST_INT64_BYTES(0)                     /* cookie */
    };
    static const guint8 request_data_hidl [] = {
        HIDL_RPC_HEADER,
        /* 32-bit integer */
        TEST_INT32_BYTES(42),
        /* 64-bit NULL flat_binder_object */
        TEST_INT32_BYTES(BINDER_TYPE_BINDER),   /* hdr.type */
        TEST_INT32_BYTES(0x17f),                /* flags */
        TEST_INT64_BYTES(0),                    /* handle */
        TEST_INT64_BYTES(0)                     /* cookie */
    };
    static const GBinderObjectConverterFunctions convert_f = {
        .handle_to_local = test_to_local_convert_none
    };
    const char* dev = GBINDER_DEFAULT_BINDER;
    const char* dev2 = GBINDER_DEFAULT_HWBINDER;
    GBinderDriver* driver = gbinder_driver_new(dev, NULL);
    GBinderDriver* driver2 = gbinder_driver_new(dev2, NULL);
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL,
        gbinder_rpc_protocol_for_device(dev), 0, 0);
    GBinderObjectConverter convert;
    GBinderLocalRequest* req2;
    GBinderOutputData* data;
    const GByteArray* bytes;
    GUtilIntArray* offsets;
    guint8* req_data = g_memdup(request_data, sizeof(request_data));
    void** objects = g_new0(void*, 2);

    /* Skip the 32-bit integer */
    objects[0] = req_data + 4;
    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION,
        gbinder_buffer_new(driver, req_data, sizeof(request_data), objects));

    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_RPC_IFACE));

    /* Convert to GBinderLocalRequest */
    req2 = gbinder_remote_request_copy_to_local(req);
    data = gbinder_local_request_data(req2);
    offsets = gbinder_output_data_offsets(data);
    bytes = data->bytes;
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 4);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(bytes->len == sizeof(request_data));
    g_assert(!memcmp(request_data, bytes->data, bytes->len));
    gbinder_local_request_unref(req2);

    /* The same with gbinder_remote_request_translate_to_local() */
    req2 = gbinder_remote_request_convert_to_local(req, NULL);
    data = gbinder_local_request_data(req2);
    offsets = gbinder_output_data_offsets(data);
    bytes = data->bytes;
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 4);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(bytes->len == sizeof(request_data));
    g_assert(!memcmp(request_data, bytes->data, bytes->len));
    gbinder_local_request_unref(req2);

    /* Different driver actually requires translation */
    memset(&convert, 0, sizeof(convert));
    convert.f = &convert_f;
    convert.io = gbinder_driver_io(driver2);
    convert.protocol = gbinder_driver_protocol(driver2);
    req2 = gbinder_remote_request_convert_to_local(req, &convert);
    data = gbinder_local_request_data(req2);
    offsets = gbinder_output_data_offsets(data);
    bytes = data->bytes;
    g_assert(offsets);
    g_assert(offsets->count == 1);
    g_assert(offsets->data[0] == 4);
    g_assert(!gbinder_output_data_buffers_size(data));
    g_assert(bytes->len == sizeof(request_data_hidl));
    g_assert(!memcmp(request_data_hidl, bytes->data, bytes->len));
    gbinder_local_request_unref(req2);

    gbinder_remote_request_unref(req);
    gbinder_driver_unref(driver);
    gbinder_driver_unref(driver2);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/remote_request/"

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "null", test_null);
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
