/*
 * Copyright (C) 2021-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2021-2022 Jolla Ltd.
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

#include "test_binder.h"
#include "test_servicemanager_hidl.h"

#include "gbinder_ipc.h"
#include "gbinder_client.h"
#include "gbinder_config.h"
#include "gbinder_driver.h"
#include "gbinder_bridge.h"
#include "gbinder_reader.h"
#include "gbinder_local_reply.h"
#include "gbinder_local_request.h"
#include "gbinder_proxy_object.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_remote_reply.h"
#include "gbinder_remote_request.h"
#include "gbinder_servicemanager_p.h"

#include <gutil_log.h>

static TestOpt test_opt;

#define SRC_DEV "/dev/srcbinder"
#define DEST_DEV "/dev/dstbinder"
#define TEST_IFACE "gbinder@1.0::ITest"

#define TX_CODE   GBINDER_FIRST_CALL_TRANSACTION
#define TX_PARAM  0x11111111
#define TX_RESULT 0x22222222

static const char TMP_DIR_TEMPLATE[] = "gbinder-test-bridge-XXXXXX";
static const char* TEST_IFACES[] =  { TEST_IFACE, NULL };
static const char DEFAULT_CONFIG_DATA[] =
    "[Protocol]\n"
    "Default = hidl\n"
    "[ServiceManager]\n"
    "Default = hidl\n";

/*==========================================================================*
 * Common
 *==========================================================================*/

static
TestServiceManagerHidl*
test_servicemanager_impl_new(
    const char* dev)
{
    GBinderIpc* ipc = gbinder_ipc_new(dev, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    TestServiceManagerHidl* sm = test_servicemanager_hidl_new(ipc);

    test_binder_register_object(fd, GBINDER_LOCAL_OBJECT(sm),
        GBINDER_SERVICEMANAGER_HANDLE);
    gbinder_ipc_unref(ipc);
    return sm;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    static const char* ifaces[] = { "foo", "bar", NULL};

    g_assert(!gbinder_bridge_new2(NULL, NULL, NULL, NULL, NULL));
    g_assert(!gbinder_bridge_new(NULL, NULL, NULL, NULL));
    g_assert(!gbinder_bridge_new("foo", NULL, NULL, NULL));
    g_assert(!gbinder_bridge_new("foo", ifaces, NULL, NULL));
    gbinder_bridge_free(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

typedef struct test_basic {
    GMainLoop* loop;
    TestServiceManagerHidl* src_impl;
    int src_notify_count;
    gboolean dest_name_added;
} TestBasic;

static
GBinderLocalReply*
test_basic_cb(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    int* count = user_data;
    GBinderReader reader;
    gint32 param = 0;

    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_IFACE));
    g_assert(code == TX_CODE);

    /* Make sure that parameter got delivered intact */
    gbinder_remote_request_init_reader(req, &reader);
    g_assert(gbinder_reader_read_int32(&reader, &param));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert_cmpint(param, == ,TX_PARAM);

    *status = GBINDER_STATUS_OK;
    (*count)++;
    GDEBUG("Got a request, replying");
    return gbinder_local_reply_append_int32
        (gbinder_local_object_new_reply(obj), TX_RESULT);
}

static
void
test_basic_add_cb(
    GBinderServiceManager* sm,
    int status,
    void* user_data)
{
    TestBasic* test = user_data;

    GDEBUG("Name added");
    g_assert(status == GBINDER_STATUS_OK);
    g_assert(!test->dest_name_added);
    test->dest_name_added = TRUE;
    if (test->src_notify_count) {
        g_main_loop_quit(test->loop);
    }
}

static
void
test_basic_notify_cb(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data)
{
    TestBasic* test = user_data;

    g_assert(name);
    GDEBUG("'%s' is registered", name);
    g_assert_cmpint(test->src_notify_count, == ,0);
    test->src_notify_count++;
    /* Exit the loop after both things happen */
    if (test->dest_name_added) {
        g_main_loop_quit(test->loop);
    }
}

static
void
test_basic_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* loop)
{
    GBinderReader reader;
    gint32 result = 0;

    g_assert(reply);
    GDEBUG("Reply received");

    /* Make sure that result got delivered intact */
    gbinder_remote_reply_init_reader(reply, &reader);
    g_assert(gbinder_reader_read_int32(&reader, &result));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert_cmpint(result, == ,TX_RESULT);

    /* Exit the loop */
    g_main_loop_quit((GMainLoop*)loop);
}

static
void
test_basic_death(
    GBinderRemoteObject* obj,
    void* loop)
{
    GDEBUG("Source object died");

    /* Exit the loop */
    g_main_loop_quit((GMainLoop*)loop);
}

static
void
test_basic_run(
    void)
{
    TestBasic test;
    TestServiceManagerHidl* dest_impl;
    GBinderServiceManager* src;
    GBinderServiceManager* dest;
    GBinderIpc* src_ipc;
    GBinderIpc* dest_ipc;
    GBinderBridge* bridge;
    GBinderLocalObject* obj;
    GBinderRemoteObject* src_obj;
    GBinderLocalRequest* req;
    GBinderClient* src_client;
    const char* name = "test";
    const char* fqname = TEST_IFACE "/test";
    int src_fd, dest_fd, n = 0;
    gulong id;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    /* obj (DEST) <=> bridge <=> (SRC) mirror */
    src_ipc = gbinder_ipc_new(SRC_DEV, NULL);
    dest_ipc = gbinder_ipc_new(DEST_DEV, NULL);
    test.src_impl = test_servicemanager_impl_new(SRC_DEV);
    dest_impl = test_servicemanager_impl_new(DEST_DEV);
    src_fd = gbinder_driver_fd(src_ipc->driver);
    dest_fd = gbinder_driver_fd(dest_ipc->driver);
    obj = gbinder_local_object_new(dest_ipc, TEST_IFACES, test_basic_cb, &n);

    /* Set up binder simulator */
    src = gbinder_servicemanager_new(SRC_DEV);
    dest = gbinder_servicemanager_new(DEST_DEV);

    /* Both src and dest are required */
    g_assert(!gbinder_bridge_new(name, TEST_IFACES, src, NULL));
    bridge = gbinder_bridge_new2(NULL, name, TEST_IFACES, src, dest);

    /* Start watching the name */
    id = gbinder_servicemanager_add_registration_handler(src, fqname,
        test_basic_notify_cb, &test);
    g_assert(id);

    /* Register the object and wait for completion */
    GDEBUG("Registering object '%s' => %p", name, obj);
    g_assert(gbinder_servicemanager_add_service(dest, name, obj,
        test_basic_add_cb, &test));

    /* This loop quits after the name is added and notification is received */
    test_run(&test_opt, test.loop);

    GDEBUG("Bridge object has been registered on source");
    g_assert_cmpint(test.src_notify_count, == ,1);
    gbinder_servicemanager_remove_handler(src, id);

    /* Get a remote reference to the object created by the bridge */
    src_obj = gbinder_servicemanager_get_service_sync(src, fqname, NULL);
    g_assert(!src_obj->dead);

    /* Make a call */
    GDEBUG("Submitting a call");
    /* src_client will hold a reference to src_obj */
    src_client = gbinder_client_new(src_obj, TEST_IFACE);
    req = gbinder_client_new_request(src_client);
    gbinder_local_request_append_int32(req, TX_PARAM);
    g_assert(gbinder_client_transact(src_client, TX_CODE, 0, req,
        test_basic_reply, NULL, test.loop));
    gbinder_local_request_unref(req);

    /* Wait for completion */
    test_run(&test_opt, test.loop);

    /* Kill the objects and wait for one of them to die */
    g_assert(!src_obj->dead);
    id = gbinder_remote_object_add_death_handler(src_obj, test_basic_death,
        test.loop);

    g_assert(test_servicemanager_hidl_remove(dest_impl, fqname));
    GDEBUG("Killing destination objects");
    /*
     * Need these BR_DEAD_BINDER because both servicemanagers and the
     * bridge live inside the same process and reference the same objects.
     * BR_DEAD_BINDER forces the bridge (proxy) to drop its reference.
     */
    test_binder_br_dead_binder_obj(dest_fd, obj);
    test_binder_br_dead_binder(src_fd, ANY_THREAD, src_obj->handle);

    /* Wait for the auto-created object to die */
    test_run(&test_opt, test.loop);
    g_assert(src_obj->dead);
    gbinder_remote_object_remove_handler(src_obj, id);

    GDEBUG("Done");

    gbinder_local_object_drop(obj);
    gbinder_bridge_free(bridge);
    test_servicemanager_hidl_free(test.src_impl);
    test_servicemanager_hidl_free(dest_impl);
    gbinder_servicemanager_unref(src);
    gbinder_servicemanager_unref(dest);
    gbinder_client_unref(src_client);
    gbinder_ipc_unref(src_ipc);
    gbinder_ipc_unref(dest_ipc);

    test_binder_exit_wait(&test_opt, test.loop);
    g_main_loop_unref(test.loop);
}

static
void
test_basic(
    void)
{
    test_run_in_context(&test_opt, test_basic_run);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/bridge/" t

int main(int argc, char* argv[])
{
    TestConfig test_config;
    char* config_file;
    int result;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);

    test_init(&test_opt, argc, argv);
    test_config_init(&test_config, TMP_DIR_TEMPLATE);

    config_file = g_build_filename(test_config.config_dir, "test.conf", NULL);
    g_assert(g_file_set_contents(config_file, DEFAULT_CONFIG_DATA, -1, NULL));
    GDEBUG("Config file %s", config_file);
    gbinder_config_file = config_file;

    result = g_test_run();

    remove(config_file);
    g_free(config_file);
    test_config_cleanup(&test_config);
    return result;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
