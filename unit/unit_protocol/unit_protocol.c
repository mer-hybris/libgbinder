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
#include "gbinder_io.h"
#include "gbinder_local_request_p.h"
#include "gbinder_output_data.h"
#include "gbinder_reader.h"
#include "gbinder_remote_request_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_writer.h"

static TestOpt test_opt;

#define STRICT_MODE_PENALTY_GATHER (0x40 << 16)
#define BINDER_RPC_FLAGS (STRICT_MODE_PENALTY_GATHER)

typedef struct test_header_data {
    const char* name;
    const char* dev;
    const char* iface;
    const guint8* header;
    guint header_size;
} TestHeaderData;

static const guint8 test_header_binder [] = {
    TEST_INT32_BYTES(BINDER_RPC_FLAGS),
    TEST_INT32_BYTES(3),
    TEST_INT16_BYTES('f'), TEST_INT16_BYTES('o'),
    TEST_INT16_BYTES('o'), 0x00, 0x00
};

static const guint8 test_header_hwbinder [] = {
    'f', 'o', 'o', 0x00
};

static const TestHeaderData test_header_tests[] = {
    { "binder", GBINDER_DEFAULT_BINDER, "foo",
      TEST_ARRAY_AND_SIZE(test_header_binder) },
    { "hwbinder", GBINDER_DEFAULT_HWBINDER, "foo",
      TEST_ARRAY_AND_SIZE(test_header_hwbinder) }
};

/*==========================================================================*
 * device
 *==========================================================================*/

static
void
test_device(
    void)
{
    g_assert(gbinder_rpc_protocol_for_device(NULL) ==
        &gbinder_rpc_protocol_binder);
    g_assert(gbinder_rpc_protocol_for_device(GBINDER_DEFAULT_BINDER) ==
        &gbinder_rpc_protocol_binder);
    g_assert(gbinder_rpc_protocol_for_device(GBINDER_DEFAULT_HWBINDER) ==
        &gbinder_rpc_protocol_hwbinder);
}

/*==========================================================================*
 * no_header1
 *==========================================================================*/

static
void
test_no_header1(
    void)
{
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL,
        gbinder_rpc_protocol_for_device(GBINDER_DEFAULT_BINDER), 0, 0);

    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION, NULL);
    g_assert(!gbinder_remote_request_interface(req));
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * no_header2
 *==========================================================================*/

static
void
test_no_header2(
    void)
{
    const GBinderRpcProtocol* p = &gbinder_rpc_protocol_binder;
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, p);
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL, p, 0, 0);

    gbinder_remote_request_set_data(req, GBINDER_DUMP_TRANSACTION,
        gbinder_buffer_new(driver,
        g_memdup(TEST_ARRAY_AND_SIZE(test_header_binder)),
        sizeof(test_header_binder), NULL));
    g_assert(!gbinder_remote_request_interface(req));
    gbinder_remote_request_unref(req);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * write_header
 *==========================================================================*/

static
void
test_write_header(
    gconstpointer test_data)
{
    const TestHeaderData* test = test_data;
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(test->dev);
    GBinderLocalRequest* req = gbinder_local_request_new(&gbinder_io_32, NULL);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    prot->write_rpc_header(&writer, test->iface);
    data = gbinder_local_request_data(req);
    g_assert(data->bytes->len == test->header_size);
    g_assert(!memcmp(data->bytes->data, test->header, test->header_size));
    gbinder_local_request_unref(req);
}

/*==========================================================================*
 * read_header
 *==========================================================================*/

static
void
test_read_header(
    gconstpointer test_data)
{
    const TestHeaderData* test = test_data;
    GBinderDriver* driver = gbinder_driver_new(test->dev, NULL);
    GBinderRemoteRequest* req = gbinder_remote_request_new(NULL,
        gbinder_rpc_protocol_for_device(test->dev), 0, 0);

    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION,
        gbinder_buffer_new(driver, g_memdup(test->header, test->header_size),
        test->header_size, NULL));
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), test->iface));
    gbinder_remote_request_unref(req);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/protocol/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("device"), test_device);
    g_test_add_func(TEST_("no_header1"), test_no_header1);
    g_test_add_func(TEST_("no_header2"), test_no_header2);

    for (i = 0; i < G_N_ELEMENTS(test_header_tests); i++) {
        const TestHeaderData* test = test_header_tests + i;
        char* path;

        path = g_strconcat(TEST_PREFIX, test->name, "/read_header", NULL);
        g_test_add_data_func(path, test, test_read_header);
        g_free(path);

        path = g_strconcat(TEST_PREFIX, test->name, "/write_header", NULL);
        g_test_add_data_func(path, test, test_write_header);
        g_free(path);
    }

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
