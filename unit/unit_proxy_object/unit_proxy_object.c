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

#include "gbinder_ipc.h"
#include "gbinder_client.h"
#include "gbinder_config.h"
#include "gbinder_driver.h"
#include "gbinder_proxy_object.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_remote_request.h"
#include "gbinder_remote_reply.h"
#include "gbinder_local_request.h"
#include "gbinder_local_reply.h"

#include <gutil_log.h>

#include <errno.h>

static TestOpt test_opt;

#define DEV "/dev/xbinder"
#define DEV_PRIV  DEV "-private"
#define DEV2 "/dev/ybinder"
#define DEV2_PRIV  DEV2 "-private"

enum test_tx_codes {
    TX_CODE = GBINDER_FIRST_CALL_TRANSACTION,
    TX_CODE2,
    TX_CODE3
};
#define TX_PARAM1 0x11111111
#define TX_PARAM2 0x22222222
#define TX_PARAM3 0x33333333
#define TX_RESULT1 0x01010101
#define TX_RESULT2 0x02020202
#define TX_PARAM_REPLY 0x11110000
#define TX_PARAM_DONT_REPLY 0x22220000
#define TX_RESULT 0x03030303

static const char TMP_DIR_TEMPLATE[] = "gbinder-test-proxy-XXXXXX";
const char TEST_IFACE[] = "test@1.0::ITest";
const char TEST_IFACE2[] = "test@1.0::ITest2";
static const char* TEST_IFACES[] =  { TEST_IFACE, NULL };
static const char* TEST_IFACES2[] =  { TEST_IFACE2, NULL };
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

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!gbinder_proxy_object_new(NULL, NULL));
}

/*==========================================================================*
 * basic
 *==========================================================================*/

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

    GDEBUG("Request handled");
    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_IFACE));
    g_assert(code == TX_CODE);

    /* No parameters are expected */
    gbinder_remote_request_init_reader(req, &reader);
    g_assert(gbinder_reader_at_end(&reader));

    *status = GBINDER_STATUS_OK;
    (*count)++;
    return gbinder_local_object_new_reply(obj);
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

    GDEBUG("Reply received");

    /* No parameters are expected */
    gbinder_remote_reply_init_reader(reply, &reader);
    g_assert(gbinder_reader_at_end(&reader));

    g_main_loop_quit((GMainLoop*)loop);
}

static
void
test_basic_run(
    void)
{
    TestConfig config;
    GBinderLocalObject* obj;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderRemoteObject* remote_proxy;
    GBinderClient* proxy_client;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_proxy;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd_obj, fd_proxy, n = 0;

    test_config_init(&config, NULL);
    ipc_proxy = gbinder_ipc_new(DEV);
    ipc_obj = gbinder_ipc_new(DEV_PRIV);
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);
    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES, test_basic_cb, &n);
    remote_obj = gbinder_remote_object_new(ipc_proxy,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);

    /* remote_proxy(DEV_PRIV) => proxy (DEV) => obj (DEV) => DEV_PRIV */
    g_assert(!gbinder_proxy_object_new(NULL, remote_obj));
    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    remote_proxy = gbinder_remote_object_new(ipc_obj,
        test_binder_register_object(fd_proxy, &proxy->parent, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    proxy_client = gbinder_client_new(remote_proxy, TEST_IFACE);

    test_binder_set_passthrough(fd_obj, TRUE);
    test_binder_set_passthrough(fd_proxy, TRUE);
    test_binder_set_looper_enabled(fd_obj, TEST_LOOPER_ENABLE);
    test_binder_set_looper_enabled(fd_proxy, TEST_LOOPER_ENABLE);

    /* Perform a transaction via proxy */
    g_assert(gbinder_client_transact(proxy_client, TX_CODE, 0, NULL,
        test_basic_reply, NULL, loop));

    test_run(&test_opt, loop);
    g_assert_cmpint(n, == ,1);

    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);
    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(remote_obj);
    gbinder_remote_object_unref(remote_proxy);
    gbinder_client_unref(proxy_client);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_proxy);
    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, loop);
    test_config_deinit(&config);
    g_main_loop_unref(loop);
}

static
void
test_basic(
    void)
{
    test_run_in_context(&test_opt, test_basic_run);
}

/*==========================================================================*
 * param
 *==========================================================================*/

static
gboolean
test_param_cancel(
    gpointer req)
{
    gbinder_remote_request_complete(req, NULL, -ECANCELED);
    return G_SOURCE_REMOVE;
}

static
GBinderLocalReply*
test_param_cb(
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

    *status = GBINDER_STATUS_OK;
    (*count)++;
    if (param == TX_PARAM_REPLY) {
        GDEBUG("Replying to request 0x%08x", param);
        return gbinder_local_reply_append_int32
            (gbinder_local_object_new_reply(obj), TX_RESULT);
    } else {
        g_assert_cmpint(param, == ,TX_PARAM_DONT_REPLY);
        GDEBUG("Suspending request 0x%08x", param);
        gbinder_remote_request_block(req);
        g_timeout_add_full(G_PRIORITY_DEFAULT, 50, test_param_cancel,
             gbinder_remote_request_ref(req), (GDestroyNotify)
             gbinder_remote_request_unref);
        return NULL;
    }
}

static
void
test_param_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* loop)
{
    /*
     * Due to limitations of our binder simulation, the result can be
     * delivered to a wrong thread. As a result, we only known that one
     * of the callbacks get NULL result and one gets NULL loop, but we
     * don't really know which one gets what, i.e. we have to be ready
     * for any combination of these parameters.
     *
     * It's too difficult to fix (without writing almost a full-blown
     * binder implementation), let's just live with it for now :/
     */
    if (reply) {
        GBinderReader reader;
        gint32 result = 0;

        GDEBUG("Reply received");

        /* Make sure that result got delivered intact */
        gbinder_remote_reply_init_reader(reply, &reader);
        g_assert(gbinder_reader_read_int32(&reader, &result));
        g_assert(gbinder_reader_at_end(&reader));
        g_assert_cmpint(result, == ,TX_RESULT);
    } else {
        /* The cancelled one */
        GDEBUG("Transaction cancelled");
    }

    if (loop) {
        g_main_loop_quit((GMainLoop*)loop);
    }
}

static
void
test_param_run(
    void)
{
    TestConfig config;
    GBinderLocalObject* obj;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderRemoteObject* remote_proxy;
    GBinderClient* proxy_client;
    GBinderLocalRequest* req;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_remote_obj;
    GBinderIpc* ipc_proxy;
    GBinderIpc* ipc_remote_proxy;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd_obj, fd_proxy, n = 0;

    test_config_init(&config, NULL);
    ipc_obj = gbinder_ipc_new(DEV);
    ipc_remote_obj = gbinder_ipc_new(DEV_PRIV);
    ipc_proxy = gbinder_ipc_new(DEV2);
    ipc_remote_proxy = gbinder_ipc_new(DEV2_PRIV);
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);
    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES, test_param_cb, &n);
    remote_obj = gbinder_remote_object_new(ipc_remote_obj,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);

    /* remote_proxy(DEV2_PRIV) => proxy (DEV2) => obj (DEV) => DEV_PRIV */
    g_assert(!gbinder_proxy_object_new(NULL, remote_obj));
    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    remote_proxy = gbinder_remote_object_new(ipc_remote_proxy,
        test_binder_register_object(fd_proxy, &proxy->parent, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    proxy_client = gbinder_client_new(remote_proxy, TEST_IFACE);

    test_binder_set_passthrough(fd_obj, TRUE);
    test_binder_set_passthrough(fd_proxy, TRUE);
    test_binder_set_looper_enabled(fd_obj, TEST_LOOPER_ENABLE);
    test_binder_set_looper_enabled(fd_proxy, TEST_LOOPER_ENABLE);

    /*
     * Perform two transactions via proxy. First one never gets completed
     * and eventually is cancelled, and the second one is replied to.
     */
    req = gbinder_client_new_request(proxy_client);
    gbinder_local_request_append_int32(req, TX_PARAM_DONT_REPLY);
    gbinder_client_transact(proxy_client, TX_CODE, 0, req, test_param_reply,
        NULL, NULL);
    gbinder_local_request_unref(req);

    req = gbinder_client_new_request(proxy_client);
    gbinder_local_request_append_int32(req, TX_PARAM_REPLY);
    g_assert(gbinder_client_transact(proxy_client, TX_CODE, 0, req,
        test_param_reply, NULL, loop));
    gbinder_local_request_unref(req);

    test_run(&test_opt, loop);

    /* Depending on how callbacks are scheduled, n could be 1 or 2 */
    g_assert_cmpint(n, > ,0);

    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);
    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(remote_obj);
    gbinder_remote_object_unref(remote_proxy);
    gbinder_client_unref(proxy_client);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_remote_obj);
    gbinder_ipc_unref(ipc_proxy);
    gbinder_ipc_unref(ipc_remote_proxy);
    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, loop);
    test_config_deinit(&config);
    g_main_loop_unref(loop);
}

static
void
test_param(
    void)
{
    test_run_in_context(&test_opt, test_param_run);
}

/*==========================================================================*
 * obj
 *==========================================================================*/

typedef struct test_obj_data {
    GMainLoop* loop;
    GBinderLocalObject* tmp_proxy;
    gboolean obj_call_handled;
    gboolean obj_call_finished;
    gboolean obj2_call_handled;
    gboolean obj2_call_finished;
} TestObj;

static
GBinderLocalReply*
test_obj2_cb(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestObj* test = user_data;
    GBinderReader reader;
    gint32 param = 0;

    GDEBUG("Request 2 handled");
    g_assert(!test->obj2_call_handled);
    test->obj2_call_handled = TRUE;
    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_IFACE2));
    g_assert(code == TX_CODE2);

    /* TX_PARAM3 parameter is expected */
    gbinder_remote_request_init_reader(req, &reader);
    g_assert(gbinder_reader_read_int32(&reader, &param));
    g_assert_cmpuint(param, == ,TX_PARAM3);
    g_assert(gbinder_reader_at_end(&reader));

    *status = GBINDER_STATUS_OK;
    return gbinder_local_reply_append_int32
        (gbinder_local_object_new_reply(obj), TX_RESULT2);
}

static
void
test_obj2_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* data)
{
    GBinderReader reader;
    gint32 result = 0;
    TestObj* test = data;

    GDEBUG("Reply 2 received");

    /* Make sure that result got delivered intact */
    gbinder_remote_reply_init_reader(reply, &reader);
    g_assert(gbinder_reader_read_int32(&reader, &result));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert_cmpint(result, == ,TX_RESULT2);

    g_assert(!test->obj2_call_finished);
    test->obj2_call_finished = TRUE;
    if (test->obj_call_finished) {
        GDEBUG("Both calls are done");
        g_main_loop_quit(test->loop);
    }
}

static
GBinderLocalReply*
test_obj_cb(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestObj* test = user_data;
    GBinderReader reader;
    GBinderRemoteObject* obj2;
    GBinderClient* client2;
    GBinderLocalRequest* req2;
    gint32 param = 0;

    GDEBUG("Request 1 handled");
    g_assert(!test->obj_call_handled);
    test->obj_call_handled = TRUE;
    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_IFACE));
    g_assert(code == TX_CODE);

    /* Read parameters: TX_PARAM1, object, TX_PARAM2  */
    gbinder_remote_request_init_reader(req, &reader);
    g_assert(gbinder_reader_read_int32(&reader, &param));
    g_assert_cmpuint(param, == ,TX_PARAM1);
    g_assert((obj2 = gbinder_reader_read_object(&reader)));
    g_assert(gbinder_reader_read_int32(&reader, &param));
    g_assert_cmpuint(param, == ,TX_PARAM2);
    g_assert(gbinder_reader_at_end(&reader));

    /* Make sure temporary proxy won't get destroyed too early */
    test->tmp_proxy = test_binder_object(gbinder_driver_fd(obj->ipc->driver),
        obj2->handle);
    g_assert(test->tmp_proxy);

    /* Call remote object */
    client2 = gbinder_client_new(obj2, TEST_IFACE2);
    req2 = gbinder_client_new_request(client2);
    gbinder_local_request_append_int32(req2, TX_PARAM3);
    gbinder_client_transact(client2, TX_CODE2, 0, req2, test_obj2_reply,
        NULL, test);
    gbinder_local_request_unref(req2);
    gbinder_client_unref(client2);
    gbinder_remote_object_unref(obj2);

    *status = GBINDER_STATUS_OK;
    return gbinder_local_reply_append_int32
        (gbinder_local_object_new_reply(obj), TX_RESULT1);
}

static
void
test_obj_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* data)
{
    GBinderReader reader;
    gint32 result = 0;
    TestObj* test = data;

    GDEBUG("Reply 1 received");

    /* Make sure that result got delivered intact */
    gbinder_remote_reply_init_reader(reply, &reader);
    g_assert(gbinder_reader_read_int32(&reader, &result));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert_cmpint(result, == ,TX_RESULT1);

    g_assert(!test->obj_call_finished);
    test->obj_call_finished = TRUE;
    if (test->obj2_call_finished) {
        GDEBUG("Both calls are done");
        g_main_loop_quit(test->loop);
    }
}

static
void
test_obj_run(
    void)
{
    TestConfig config;
    TestObj test;
    GBinderLocalObject* obj;
    GBinderLocalObject* obj2;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderRemoteObject* remote_proxy;
    GBinderClient* proxy_client;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_proxy;
    GBinderLocalRequest* req;
    int fd_obj, fd_proxy;

    test_config_init(&config, NULL);
    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    ipc_proxy = gbinder_ipc_new(DEV);
    ipc_obj = gbinder_ipc_new(DEV_PRIV);
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);
    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES, test_obj_cb, &test);
    obj2 = gbinder_local_object_new(ipc_obj, TEST_IFACES2, test_obj2_cb, &test);
    remote_obj = gbinder_remote_object_new(ipc_proxy,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);

    /* remote_proxy(DEV_PRIV) => proxy (DEV) => obj (DEV) => DEV_PRIV */
    g_assert(!gbinder_proxy_object_new(NULL, remote_obj));
    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    remote_proxy = gbinder_remote_object_new(ipc_obj,
        test_binder_register_object(fd_proxy, &proxy->parent, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    proxy_client = gbinder_client_new(remote_proxy, TEST_IFACE);

    test_binder_set_passthrough(fd_obj, TRUE);
    test_binder_set_passthrough(fd_proxy, TRUE);
    test_binder_set_looper_enabled(fd_obj, TEST_LOOPER_ENABLE);
    test_binder_set_looper_enabled(fd_proxy, TEST_LOOPER_ENABLE);

    /* Pass object reference via proxy */
    req = gbinder_client_new_request(proxy_client);
    gbinder_local_request_append_int32(req, TX_PARAM1);
    gbinder_local_request_append_local_object(req, obj2);
    gbinder_local_request_append_int32(req, TX_PARAM2);
    gbinder_client_transact(proxy_client, TX_CODE, 0, req, test_obj_reply,
        NULL, &test);
    gbinder_local_request_unref(req);

    test_run(&test_opt, test.loop);

    g_assert(test.obj_call_handled);
    g_assert(test.obj_call_finished);
    g_assert(test.obj2_call_handled);
    g_assert(test.obj2_call_finished);
    g_assert(test.tmp_proxy);
    gbinder_local_object_unref(test.tmp_proxy);

    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);
    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(obj2);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(remote_obj);
    gbinder_remote_object_unref(remote_proxy);
    gbinder_client_unref(proxy_client);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_proxy);
    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, test.loop);
    test_config_deinit(&config);
    g_main_loop_unref(test.loop);
}

static
void
test_obj(
    void)
{
    test_run_in_context(&test_opt, test_obj_run);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/proxy_object/" t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("param"), test_param);
    g_test_add_func(TEST_("obj"), test_obj);
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
