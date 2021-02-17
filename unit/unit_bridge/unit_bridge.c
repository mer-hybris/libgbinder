/*
 * Copyright (C) 2021 Jolla Ltd.
 * Copyright (C) 2021 Slava Monich <slava.monich@jolla.com>
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
#define SRC_PRIV_DEV  SRC_DEV "-private"
#define DEST_DEV "/dev/dstbinder"
#define DEST_PRIV_DEV  DEST_DEV "-private"
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

typedef struct test_config {
    char* dir;
    char* file;
} TestConfig;

/*==========================================================================*
 * Test object (registered with two GBinderIpc's)
 *==========================================================================*/

typedef GBinderLocalObjectClass TestLocalObjectClass;
typedef struct test_local_object {
    GBinderLocalObject parent;
    GBinderIpc* ipc2;
} TestLocalObject;
G_DEFINE_TYPE(TestLocalObject, test_local_object, GBINDER_TYPE_LOCAL_OBJECT)
#define TEST_TYPE_LOCAL_OBJECT test_local_object_get_type()
#define TEST_LOCAL_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        TEST_TYPE_LOCAL_OBJECT, TestLocalObject))

TestLocalObject*
test_local_object_new(
    GBinderIpc* ipc,
    GBinderIpc* ipc2,
    const char* const* ifaces,
    GBinderLocalTransactFunc txproc,
    void* user_data)
{
    TestLocalObject* self = TEST_LOCAL_OBJECT
        (gbinder_local_object_new_with_type(TEST_TYPE_LOCAL_OBJECT,
            ipc, ifaces, txproc, user_data));

    self->ipc2 = gbinder_ipc_ref(ipc2);
    gbinder_ipc_register_local_object(ipc2, &self->parent);
    return self;
}

static
void
test_local_object_dispose(
    GObject* object)
{
    TestLocalObject* self = TEST_LOCAL_OBJECT(object);

    gbinder_ipc_local_object_disposed(self->ipc2, &self->parent);
    G_OBJECT_CLASS(test_local_object_parent_class)->dispose(object);
}

static
void
test_local_object_finalize(
    GObject* object)
{
    gbinder_ipc_unref(TEST_LOCAL_OBJECT(object)->ipc2);
    G_OBJECT_CLASS(test_local_object_parent_class)->finalize(object);
}

static
void
test_local_object_init(
    TestLocalObject* self)
{
}

static
void
test_local_object_class_init(
    TestLocalObjectClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = test_local_object_dispose;
    object_class->finalize = test_local_object_finalize;
}

/*==========================================================================*
 * Common
 *==========================================================================*/

static
void
test_config_init(
    TestConfig* config,
    char* config_data)
{
    config->dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    config->file = g_build_filename(config->dir, "test.conf", NULL);
    g_assert(g_file_set_contents(config->file, config_data ? config_data :
        DEFAULT_CONFIG_DATA, -1, NULL));

    gbinder_config_exit();
    gbinder_config_dir = config->dir;
    gbinder_config_file = config->file;
    GDEBUG("Wrote config to %s", config->file);
}

static
void
test_config_deinit(
    TestConfig* config)
{
    gbinder_config_exit();

    remove(config->file);
    g_free(config->file);

    remove(config->dir);
    g_free(config->dir);
}

static
TestServiceManagerHidl*
test_servicemanager_impl_new(
    const char* dev)
{
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    const int fd = gbinder_driver_fd(ipc->driver);
    TestServiceManagerHidl* sm = test_servicemanager_hidl_new(ipc);

    test_binder_set_looper_enabled(fd, TRUE);
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
    TestConfig config;
    TestServiceManagerHidl* dest_impl;
    GBinderServiceManager* src;
    GBinderServiceManager* dest;
    GBinderIpc* src_ipc;
    GBinderIpc* src_priv_ipc;
    GBinderIpc* dest_ipc;
    GBinderIpc* dest_priv_ipc;
    GBinderBridge* bridge;
    TestLocalObject* obj;
    GBinderRemoteObject* br_src_obj;
    GBinderRemoteObject* src_obj;
    GBinderLocalRequest* req;
    GBinderClient* src_client;
    const char* name = "test";
    const char* fqname = TEST_IFACE "/test";
    int src_fd, dest_fd, h, n = 0;
    gulong id;

    test_config_init(&config, NULL);
    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    /* obj (DEST) <=> bridge <=> (SRC) mirror */
    src_ipc = gbinder_ipc_new(SRC_DEV);
    src_priv_ipc = gbinder_ipc_new(SRC_PRIV_DEV);
    dest_ipc = gbinder_ipc_new(DEST_DEV);
    dest_priv_ipc = gbinder_ipc_new(DEST_PRIV_DEV);
    test.src_impl = test_servicemanager_impl_new(SRC_PRIV_DEV);
    dest_impl = test_servicemanager_impl_new(DEST_PRIV_DEV);
    src_fd = gbinder_driver_fd(src_ipc->driver);
    dest_fd = gbinder_driver_fd(dest_ipc->driver);
    obj = test_local_object_new(dest_ipc, dest_priv_ipc, TEST_IFACES,
        test_basic_cb, &n);

    /* Set up binder simulator */
    test_binder_set_passthrough(src_fd, TRUE);
    test_binder_set_passthrough(dest_fd, TRUE);
    test_binder_set_looper_enabled(src_fd, TEST_LOOPER_ENABLE);
    test_binder_set_looper_enabled(dest_fd, TEST_LOOPER_ENABLE);
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
    g_assert(gbinder_servicemanager_add_service(dest, name, &obj->parent,
        test_basic_add_cb, &test));

    /* This loop quits after the name is added and notification is received */
    test_run(&test_opt, test.loop);

    GDEBUG("Bridge object has been registered on source");
    g_assert_cmpint(test.src_notify_count, == ,1);
    gbinder_servicemanager_remove_handler(src, id);

    /* Get a remote reference to the object created by the bridge */
    br_src_obj = gbinder_servicemanager_get_service_sync(src, fqname, NULL);
    g_assert(gbinder_remote_object_ref(br_src_obj)); /* autoreleased */
    g_assert(!br_src_obj->dead);

    /*
     * This is a trick specific to test_binder simulation. We need to
     * associate src_obj with the other side of the socket, so that the
     * call goes like this:
     *
     * src_obj (src_priv) => (src) bridge (dest) => (dest_priv) => obj
     *
     * Note that the original src_obj gets autoreleased and doesn't need
     * to be explicitly unreferenced.
     */
    src_obj = gbinder_remote_object_new(src_priv_ipc,
        br_src_obj->handle, REMOTE_OBJECT_CREATE_ALIVE);

    /* Make a call */
    GDEBUG("Submitting a call");
    src_client = gbinder_client_new(src_obj, TEST_IFACE);
    req = gbinder_client_new_request(src_client);
    gbinder_local_request_append_int32(req, TX_PARAM);
    g_assert(gbinder_client_transact(src_client, TX_CODE, 0, req,
        test_basic_reply, NULL, test.loop));
    gbinder_local_request_unref(req);

    /* Wait for completion */
    test_run(&test_opt, test.loop);

    /* Kill the destination object and wait for auto-created object to die */
    g_assert(!br_src_obj->dead);
    id = gbinder_remote_object_add_death_handler(br_src_obj, test_basic_death,
        test.loop);
    h = test_binder_handle(dest_fd, &obj->parent);
    g_assert_cmpint(h, > ,0); /* Zero is servicemanager */

    GDEBUG("Killing destination object, handle %d", h);
    gbinder_local_object_drop(&obj->parent);
    test_binder_br_dead_binder(dest_fd, h);

    /* Wait for the auto-created object to die */
    test_run(&test_opt, test.loop);
    g_assert(br_src_obj->dead);
    gbinder_remote_object_remove_handler(br_src_obj, id);

    GDEBUG("Done");

    gbinder_bridge_free(bridge);
    gbinder_remote_object_unref(src_obj);
    gbinder_remote_object_unref(br_src_obj);
    test_servicemanager_hidl_free(test.src_impl);
    test_servicemanager_hidl_free(dest_impl);
    gbinder_servicemanager_unref(src);
    gbinder_servicemanager_unref(dest);
    gbinder_client_unref(src_client);
    test_binder_unregister_objects(src_fd);
    test_binder_unregister_objects(dest_fd);
    gbinder_ipc_unref(src_ipc);
    gbinder_ipc_unref(src_priv_ipc);
    gbinder_ipc_unref(dest_ipc);
    gbinder_ipc_unref(dest_priv_ipc);

    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, test.loop);
    test_config_deinit(&config);
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
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
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
