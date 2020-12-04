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
#include "gbinder_config.h"
#include "gbinder_driver.h"
#include "gbinder_io.h"
#include "gbinder_local_request_p.h"
#include "gbinder_output_data.h"
#include "gbinder_reader.h"
#include "gbinder_remote_request_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_writer.h"

static TestOpt test_opt;
static const char TMP_DIR_TEMPLATE[] = "gbinder-test-protocol-XXXXXX";

#define STRICT_MODE_PENALTY_GATHER (0x40 << 16)
#define BINDER_RPC_FLAGS (STRICT_MODE_PENALTY_GATHER)
#define UNSET_WORK_SOURCE (-1)

typedef struct test_data {
    const char* name;
    const char* prot;
    const char* dev;
} TestData;

typedef struct test_header_data {
    const char* name;
    const char* prot;
    const char* dev;
    const char* iface;
    const guint8* header;
    guint header_size;
} TestHeaderData;

static const guint8 test_header_aidl [] = {
    TEST_INT32_BYTES(BINDER_RPC_FLAGS),
    TEST_INT32_BYTES(3),
    TEST_INT16_BYTES('f'), TEST_INT16_BYTES('o'),
    TEST_INT16_BYTES('o'), 0x00, 0x00
};

static const guint8 test_header_aidl2 [] = {
    TEST_INT32_BYTES(BINDER_RPC_FLAGS),
    TEST_INT32_BYTES(UNSET_WORK_SOURCE),
    TEST_INT32_BYTES(3),
    TEST_INT16_BYTES('f'), TEST_INT16_BYTES('o'),
    TEST_INT16_BYTES('o'), 0x00, 0x00
};

static const guint8 test_header_hidl [] = {
    'f', 'o', 'o', 0x00
};

static const TestHeaderData test_header_tests[] = {
    { "aidl/ok", "aidl", GBINDER_DEFAULT_BINDER, "foo",
      TEST_ARRAY_AND_SIZE(test_header_aidl) },
    { "aidl/short", "aidl", GBINDER_DEFAULT_BINDER, NULL,
      test_header_aidl, 8 }, /* Short packet */
    { "aidl2/ok", "aidl2", GBINDER_DEFAULT_BINDER, "foo",
      TEST_ARRAY_AND_SIZE(test_header_aidl2) },
    { "aidl2/short/1", "aidl2", GBINDER_DEFAULT_BINDER, NULL,
      test_header_aidl2, 1 }, /* Short packet */
    { "aidl2/short/2", "aidl2", GBINDER_DEFAULT_BINDER, NULL,
      test_header_aidl2, 5 }, /* Short packet */
    { "aidl2/short/3", "adl2", GBINDER_DEFAULT_BINDER, NULL,
      test_header_aidl2, 9 }, /* Short packet */
    { "hidl/ok", "hidl", GBINDER_DEFAULT_HWBINDER, "foo",
      TEST_ARRAY_AND_SIZE(test_header_hidl) },
    { "hidl/short", "hidl", GBINDER_DEFAULT_HWBINDER, NULL,
      test_header_hidl, 1 }
};

typedef struct test_config {
    char* dir;
    char* file;
} TestConfig;

static
void
test_config_init(
    TestConfig* test,
    const char* config)
{
    test->dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    test->file = g_build_filename(test->dir, "test.conf", NULL);

    /* Reset the state */
    gbinder_rpc_protocol_exit();
    gbinder_config_exit();

    /* Write the config */
    g_assert(g_file_set_contents(test->file, config, -1, NULL));
    gbinder_config_file = test->file;
}

static
void
test_config_init2(
    TestConfig* test,
    const char* dev,
    const char* prot)
{
    char* config = g_strconcat("[Protocol]\n", dev, " = ", prot, "\n", NULL);

    test_config_init(test, config);
    g_free(config);
}

static
void
test_config_cleanup(
    TestConfig* test)
{
    /* Undo the damage */
    gbinder_rpc_protocol_exit();
    gbinder_config_exit();
    gbinder_config_file = NULL;

    remove(test->file);
    g_free(test->file);

    remove(test->dir);
    g_free(test->dir);
}

/*==========================================================================*
 * device
 *==========================================================================*/

static
void
test_device(
    void)
{
    const GBinderRpcProtocol* p;

    p = gbinder_rpc_protocol_for_device(NULL);
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"aidl");

    p = gbinder_rpc_protocol_for_device(GBINDER_DEFAULT_BINDER);
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"aidl");

    p = gbinder_rpc_protocol_for_device(GBINDER_DEFAULT_HWBINDER);
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"hidl");
}

/*==========================================================================*
 * config1
 *==========================================================================*/

static
void
test_config1(
    void)
{
    const GBinderRpcProtocol* p;
    TestConfig config;

    test_config_init(&config,
        "[Protocol]\n"
        "/dev/binder = hidl\n" /* Redefined name for /dev/binder */
        "/dev/hwbinder = foo\n"); /* Invalid protocol name */

    p = gbinder_rpc_protocol_for_device(NULL);
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"aidl");

    p = gbinder_rpc_protocol_for_device("/dev/hwbinder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"hidl");

    p = gbinder_rpc_protocol_for_device("/dev/binder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"hidl"); /* Redefined by config */

    p = gbinder_rpc_protocol_for_device("/dev/someotherbinder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"aidl");

    test_config_cleanup(&config);
}

/*==========================================================================*
 * config2
 *==========================================================================*/

static
void
test_config2(
    void)
{
    const GBinderRpcProtocol* p;
    TestConfig config;

    test_config_init(&config,
        "[Protocol]\n"
        "Default = hidl\n"
        "/dev/vndbinder = hidl\n"
        "/dev/hwbinder = foo\n"); /* Invalid protocol name */

    p = gbinder_rpc_protocol_for_device(NULL);
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"aidl");

    p = gbinder_rpc_protocol_for_device("/dev/vndbinder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"hidl");

    p = gbinder_rpc_protocol_for_device("/dev/hwbinder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"hidl");

    p = gbinder_rpc_protocol_for_device("/dev/binder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"aidl");

    /* The default is redefined */
    p = gbinder_rpc_protocol_for_device("/dev/someotherbinder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"hidl");

    test_config_cleanup(&config);
}

/*==========================================================================*
 * config3
 *==========================================================================*/

static
void
test_config3(
    void)
{
    const GBinderRpcProtocol* p;
    TestConfig config;

    test_config_init(&config,
        "[Whatever]\n"
        "/dev/hwbinder = aidl\n"); /* Ignored, wrong section */

    /* Just the default config */
    p = gbinder_rpc_protocol_for_device(NULL);
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"aidl");

    p = gbinder_rpc_protocol_for_device("/dev/hwbinder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"hidl");

    p = gbinder_rpc_protocol_for_device("/dev/binder");
    g_assert(p);
    g_assert_cmpstr(p->name, == ,"aidl");

    test_config_cleanup(&config);
}

/*==========================================================================*
 * no_header1
 *==========================================================================*/

static
void
test_no_header1(
    gconstpointer test_data)
{
    const TestData* test = test_data;
    GBinderRemoteRequest* req;
    TestConfig config;

    test_config_init2(&config, test->dev, test->prot);

    req = gbinder_remote_request_new(NULL, gbinder_rpc_protocol_for_device
        (GBINDER_DEFAULT_BINDER), 0, 0);
    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION, NULL);
    g_assert(!gbinder_remote_request_interface(req));
    gbinder_remote_request_unref(req);

    test_config_cleanup(&config);
}

/*==========================================================================*
 * no_header2
 *==========================================================================*/

static
void
test_no_header2(
    gconstpointer test_data)
{
    const TestData* test = test_data;
    const GBinderRpcProtocol* p;
    GBinderDriver* driver;
    GBinderRemoteRequest* req;
    TestConfig config;

    test_config_init2(&config, test->dev, test->prot);

    p = gbinder_rpc_protocol_for_device(test->dev);
    driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, p);
    req = gbinder_remote_request_new(NULL, p, 0, 0);

    gbinder_remote_request_set_data(req, GBINDER_DUMP_TRANSACTION,
        gbinder_buffer_new(driver,
        g_memdup(TEST_ARRAY_AND_SIZE(test_header_aidl)),
        sizeof(test_header_aidl), NULL));
    g_assert(!gbinder_remote_request_interface(req));
    gbinder_remote_request_unref(req);
    gbinder_driver_unref(driver);
    test_config_cleanup(&config);
}

static const TestData test_no_header_data[] = {
    { "aidl", "aidl", GBINDER_DEFAULT_BINDER },
    { "aidl2", "aidl2", GBINDER_DEFAULT_BINDER },
};

/*==========================================================================*
 * write_header
 *==========================================================================*/

static
void
test_write_header(
    gconstpointer test_data)
{
    const TestHeaderData* test = test_data;
    const GBinderRpcProtocol* prot;
    GBinderLocalRequest* req;
    GBinderOutputData* data;
    GBinderWriter writer;
    TestConfig config;

    test_config_init2(&config, test->dev, test->prot);

    prot = gbinder_rpc_protocol_for_device(test->dev);
    req = gbinder_local_request_new(&gbinder_io_32, NULL);
    gbinder_local_request_init_writer(req, &writer);
    prot->write_rpc_header(&writer, test->iface);
    data = gbinder_local_request_data(req);
    g_assert(data->bytes->len == test->header_size);
    g_assert(!memcmp(data->bytes->data, test->header, test->header_size));
    gbinder_local_request_unref(req);

    test_config_cleanup(&config);
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
    GBinderDriver* driver;
    GBinderRemoteRequest* req;
    TestConfig config;

    test_config_init2(&config, test->dev, test->prot);

    driver = gbinder_driver_new(test->dev, NULL);
    req = gbinder_remote_request_new(NULL, gbinder_rpc_protocol_for_device
        (test->dev), 0, 0);
    gbinder_remote_request_set_data(req, GBINDER_FIRST_CALL_TRANSACTION,
        gbinder_buffer_new(driver, g_memdup(test->header, test->header_size),
        test->header_size, NULL));
    g_assert_cmpstr(gbinder_remote_request_interface(req), == ,test->iface);
    gbinder_remote_request_unref(req);
    gbinder_driver_unref(driver);

    test_config_cleanup(&config);
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
    g_test_add_func(TEST_("config1"), test_config1);
    g_test_add_func(TEST_("config2"), test_config2);
    g_test_add_func(TEST_("config3"), test_config3);

    for (i = 0; i < G_N_ELEMENTS(test_no_header_data); i++) {
        const TestData* test = test_no_header_data + i;
        char* path;

        path = g_strconcat(TEST_("no_header1/"), test->name, NULL);
        g_test_add_data_func(path, test, test_no_header1);
        g_free(path);

        path = g_strconcat(TEST_("no_header2/"), test->name, NULL);
        g_test_add_data_func(path, test, test_no_header2);
        g_free(path);
    }

    for (i = 0; i < G_N_ELEMENTS(test_header_tests); i++) {
        const TestHeaderData* test = test_header_tests + i;
        char* path;

        path = g_strconcat(TEST_("read_header/"), test->name, NULL);
        g_test_add_data_func(path, test, test_read_header);
        g_free(path);

        if (test->iface) {
            path = g_strconcat(TEST_("write_header/"), test->name, NULL);
            g_test_add_data_func(path, test, test_write_header);
            g_free(path);
        }
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
