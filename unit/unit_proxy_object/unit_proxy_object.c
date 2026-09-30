/*
 * Copyright (C) 2023-2026 Slava Monich <slava@monich.com>
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

#include "gbinder_ipc.h"
#include "gbinder_client.h"
#include "gbinder_config.h"
#include "gbinder_driver.h"
#include "gbinder_proxy_object.h"
#include "gbinder_client_p.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_remote_request.h"
#include "gbinder_remote_reply.h"
#include "gbinder_local_request.h"
#include "gbinder_local_reply.h"
#include "gbinder_buffer_p.h"
#include "gbinder_local_request_p.h"
#include "gbinder_output_data.h"
#include "gbinder_object_registry.h"
#include "gbinder_remote_request_p.h"

#include <gutil_intarray.h>
#include <gutil_misc.h>

#include <gutil_log.h>

#include <errno.h>

static TestOpt test_opt;

#define DEV "/dev/xbinder"
#define DEV2 "/dev/ybinder"

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

static const char TMP_DIR_TEMPLATE[] = "gbinder-test-proxy_object-XXXXXX";
static const char TEST_IFACE[] = "test@1.0::ITest";
static const char TEST_IFACE2[] = "test@1.0::ITest2";
static const char* TEST_IFACES[] =  { TEST_IFACE, NULL };
static const char* TEST_IFACES2[] =  { TEST_IFACE2, NULL };
static const char DEFAULT_CONFIG_DATA[] =
    "[Protocol]\n"
    "Default = hidl\n"
    "[ServiceManager]\n"
    "Default = hidl\n";

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
    g_assert_cmpint(status, == ,GBINDER_STATUS_OK);
    g_assert(reply);

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
    GBinderLocalObject* obj;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderClient* client;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_proxy;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd_obj, fd_proxy, n = 0;

    ipc_proxy = gbinder_ipc_new(DEV, NULL);
    ipc_obj = gbinder_ipc_new(DEV2, NULL);
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);
    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES, test_basic_cb, &n);
    remote_obj = gbinder_remote_object_new(ipc_obj,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    remote_obj->stability = GBINDER_STABILITY_VINTF;

    g_assert(!gbinder_proxy_object_new(NULL, remote_obj));
    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    g_assert_cmpint(proxy->parent.stability, == ,GBINDER_STABILITY_VINTF);
    client = gbinder_client_new(proxy->remote, TEST_IFACE);

    /* Perform a transaction via proxy */
    g_assert(gbinder_client_transact(client, TX_CODE, 0, NULL,
        test_basic_reply, NULL, loop));

    test_run(&test_opt, loop);
    g_assert_cmpint(n, == ,1);

    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);
    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(remote_obj);
    gbinder_client_unref(client);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_proxy);
    test_binder_exit_wait(&test_opt, loop);
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
 * empty_reply
 *==========================================================================*/

typedef struct test_empty_reply {
    GMainLoop* loop;
    GThread* main_thread;
    gboolean handled;
} TestEmptyReply;

static
GBinderLocalReply*
test_empty_reply_cb(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestEmptyReply* test = user_data;
    GBinderReader reader;

    g_assert(test->main_thread == g_thread_self());
    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_IFACE));
    g_assert(code == TX_CODE);

    gbinder_remote_request_init_reader(req, &reader);
    g_assert(gbinder_reader_at_end(&reader));

    *status = GBINDER_STATUS_OK;
    test->handled = TRUE;
    return gbinder_local_object_new_reply(obj);
}

static
void
test_empty_reply_done(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    TestEmptyReply* test = user_data;
    GBinderReader reader;

    g_assert_cmpint(status, == ,GBINDER_STATUS_OK);
    g_assert(reply);
    gbinder_remote_reply_init_reader(reply, &reader);
    g_assert(gbinder_reader_at_end(&reader));

    g_main_loop_quit(test->loop);
}

static
void
test_empty_reply_run(
    void)
{
    GBinderLocalObject* obj;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderRemoteObject* proxy_remote;
    GBinderClient* client;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_proxy;
    TestEmptyReply test;
    int fd_obj, fd_proxy;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);
    test.main_thread = g_thread_self();

    ipc_proxy = gbinder_ipc_new(DEV, NULL);
    ipc_obj = gbinder_ipc_new(DEV2, NULL);
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);
    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES,
        test_empty_reply_cb, &test);
    remote_obj = gbinder_remote_object_new(ipc_obj,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);

    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    proxy_remote = gbinder_remote_object_new(ipc_proxy,
        test_binder_register_object(fd_proxy, &proxy->parent, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    client = gbinder_client_new(proxy_remote, TEST_IFACE);

    g_assert(gbinder_client_transact(client, TX_CODE, 0, NULL,
        test_empty_reply_done, NULL, &test));

    test_run(&test_opt, test.loop);
    g_assert(test.handled);

    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);
    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(proxy_remote);
    gbinder_remote_object_unref(remote_obj);
    gbinder_client_unref(client);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_proxy);
    test_binder_exit_wait(&test_opt, test.loop);
    g_main_loop_unref(test.loop);
}

static
void
test_empty_reply(
    void)
{
    test_run_in_context(&test_opt, test_empty_reply_run);
}

/*==========================================================================*
 * interface
 *==========================================================================*/

typedef struct test_interface {
    GMainLoop* loop;
    GBinderRemoteReply* reply;
} TestInterface;

static
void
test_interface_reply(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    TestInterface* test = user_data;

    g_assert_cmpint(status, == ,GBINDER_STATUS_OK);
    g_assert(reply);
    g_assert(!test->reply);
    test->reply = gbinder_remote_reply_ref(reply);
    g_main_loop_quit(test->loop);
}

static
void
test_interface_run(
    void)
{
    GBinderLocalObject* obj;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderRemoteObject* proxy_remote;
    GBinderLocalRequest* req;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_proxy;
    char* iface;
    gint32 result;
    int fd_obj, fd_proxy;
    TestInterface test;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    ipc_proxy = gbinder_ipc_new(DEV, "aidl");
    ipc_obj = gbinder_ipc_new(DEV2, "aidl");
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);
    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES, NULL, NULL);
    remote_obj = gbinder_remote_object_new(ipc_obj,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);

    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    proxy_remote = gbinder_remote_object_new(ipc_proxy,
        test_binder_register_object(fd_proxy, &proxy->parent, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);

    /*
     * INTERFACE_TRANSACTION has no request payload. The proxy has to turn
     * such bufferless incoming request into an empty local request.
     */
    req = gbinder_driver_local_request_new(ipc_proxy->driver, NULL);
    g_assert(gbinder_ipc_transact(ipc_proxy, proxy_remote->handle,
        GBINDER_INTERFACE_TRANSACTION, 0, req, test_interface_reply, NULL,
        &test));
    gbinder_local_request_unref(req);
    test_run(&test_opt, test.loop);
    iface = gbinder_remote_reply_read_string16(test.reply);
    g_assert_cmpstr(iface, == ,TEST_IFACE);

    g_free(iface);
    gbinder_remote_reply_unref(test.reply);
    test.reply = NULL;

    req = gbinder_driver_local_request_new(ipc_proxy->driver, NULL);
    g_assert(gbinder_ipc_transact(ipc_proxy, proxy_remote->handle,
        GBINDER_PING_TRANSACTION, 0, req, test_interface_reply, NULL, &test));
    gbinder_local_request_unref(req);
    test_run(&test_opt, test.loop);
    g_assert(gbinder_remote_reply_read_int32(test.reply, &result));
    g_assert_cmpint(result, == ,GBINDER_STATUS_OK);

    gbinder_remote_reply_unref(test.reply);
    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);
    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(proxy_remote);
    gbinder_remote_object_unref(remote_obj);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_proxy);
    test_binder_exit_wait(&test_opt, test.loop);
    g_main_loop_unref(test.loop);
}

static
void
test_interface(
    void)
{
    test_run_in_context(&test_opt, test_interface_run);
}

/*==========================================================================*
 * param
 *==========================================================================*/

typedef struct test_param_data {
    GMainLoop* loop;
    int stop;
    int n;
} TestParamData;

static
gboolean
test_param_cancel(
    gpointer req)
{
    GDEBUG("Cancelling request");
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
test_param_canceled(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* data)
{
    TestParamData* test = data;

    g_assert(!reply);
    g_assert_cmpint(status, == ,-ECANCELED);
    test->n++;
    GDEBUG("Transaction cancelled (%d)", test->n);
    if (test->n == test->stop) {
        g_main_loop_quit(test->loop);
    }
}

static
void
test_param_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* data)
{
    TestParamData* test = data;
    GBinderReader reader;
    gint32 result = 0;

    g_assert(reply);
    g_assert_cmpint(status, == ,0);
    test->n++;
    GDEBUG("Reply received (%d)", test->n);

    /* Make sure that result got delivered intact */
    gbinder_remote_reply_init_reader(reply, &reader);
    g_assert(gbinder_reader_read_int32(&reader, &result));
    g_assert(gbinder_reader_at_end(&reader));
    g_assert_cmpint(result, == ,TX_RESULT);
    if (test->n == test->stop) {
        g_main_loop_quit(test->loop);
    }
}

static
void
test_param_run(
    void)
{
    GBinderLocalObject* obj;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderClient* client;
    GBinderLocalRequest* req;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_proxy;
    TestParamData test;
    int fd_obj, fd_proxy, n = 0;

    memset(&test, 0, sizeof(test));
    test.stop = 2;
    test.loop = g_main_loop_new(NULL, FALSE);

    ipc_proxy = gbinder_ipc_new(DEV2, NULL);
    ipc_obj = gbinder_ipc_new(DEV, NULL);
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);
    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES, test_param_cb, &n);
    remote_obj = gbinder_remote_object_new(ipc_obj,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);

    g_assert(!gbinder_proxy_object_new(NULL, remote_obj));
    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    client = gbinder_client_new(proxy->remote, TEST_IFACE);

    /*
     * Perform two transactions via proxy. First one never gets completed
     * and eventually is cancelled, and the second one is replied to.
     */
    req = gbinder_client_new_request(client);
    gbinder_local_request_append_int32(req, TX_PARAM_DONT_REPLY);
    gbinder_client_transact(client, TX_CODE, 0, req, test_param_canceled,
        NULL, &test);
    gbinder_local_request_unref(req);

    req = gbinder_client_new_request(client);
    gbinder_local_request_append_int32(req, TX_PARAM_REPLY);
    g_assert(gbinder_client_transact(client, TX_CODE, 0, req,
        test_param_reply, NULL, &test));
    gbinder_local_request_unref(req);

    test_run(&test_opt, test.loop);
    g_assert_cmpint(test.n, == ,2);
    g_assert_cmpint(n, == ,2);

    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);
    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(remote_obj);
    gbinder_client_unref(client);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_proxy);
    test_binder_exit_wait(&test_opt, test.loop);
    g_main_loop_unref(test.loop);
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
    GBinderLocalObject* obj2;
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
    test->obj2 = test_binder_object(gbinder_driver_fd(obj->ipc->driver),
        obj2->handle);
    g_assert(test->obj2);

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
    TestObj test;
    GBinderLocalObject* obj;
    GBinderLocalObject* obj2;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderClient* client;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_proxy;
    GBinderLocalRequest* req;
    int fd_obj, fd_proxy;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    ipc_proxy = gbinder_ipc_new(DEV2, NULL);
    ipc_obj = gbinder_ipc_new(DEV, NULL);
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);

    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES, test_obj_cb, &test);
    GDEBUG("obj %p", obj);
    remote_obj = gbinder_remote_object_new(ipc_obj,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);

    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    GDEBUG("proxy %p", proxy);
    client = gbinder_client_new(proxy->remote, TEST_IFACE);

    /* Pass object reference via proxy */
    obj2 = gbinder_local_object_new(ipc_obj, TEST_IFACES2, test_obj2_cb, &test);
    GDEBUG("obj2 %p", obj2);
    req = gbinder_client_new_request(client);
    gbinder_local_request_append_int32(req, TX_PARAM1);
    gbinder_local_request_append_local_object(req, obj2);
    gbinder_local_request_append_int32(req, TX_PARAM2);
    gbinder_client_transact(client, TX_CODE, 0, req, test_obj_reply,
        NULL, &test);
    gbinder_local_request_unref(req);

    test_run(&test_opt, test.loop);

    g_assert(test.obj_call_handled);
    g_assert(test.obj_call_finished);
    g_assert(test.obj2_call_handled);
    g_assert(test.obj2_call_finished);
    g_assert(test.obj2);
    gbinder_local_object_unref(test.obj2);

    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);

    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(obj2);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(remote_obj);
    gbinder_client_unref(client);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_proxy);
    test_binder_exit_wait(&test_opt, test.loop);
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
 * nested_sync
 *==========================================================================*/

typedef struct test_nested_sync_data {
    GMainLoop* loop;
    GThread* thread;
    GBinderLocalObject* obj;
    GBinderLocalObject* connection;
    GBinderRemoteObject* accessor;
    GBinderRemoteRequest* req;
    gboolean register_handled;
    gboolean connect_handled;
    gboolean register_finished;
} TestNestedSync;

static
GBinderLocalReply*
test_nested_accessor_cb(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestNestedSync* test = user_data;
    GBinderReader reader;
    GBinderRemoteObject* observer;

    GDEBUG("Nested accessor request handled");
    g_assert(!test->connect_handled);
    test->connect_handled = TRUE;
    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_IFACE2));
    g_assert(code == TX_CODE2);

    gbinder_remote_request_init_reader(req, &reader);
    g_assert((observer = gbinder_reader_read_object(&reader)));
    g_assert(gbinder_reader_at_end(&reader));
    gbinder_remote_object_unref(observer);

    test->connection = gbinder_local_object_new(obj->ipc, TEST_IFACES2,
        NULL, NULL);
    g_assert(test->connection);

    *status = GBINDER_STATUS_OK;
    return gbinder_local_reply_append_int32
        (gbinder_local_reply_append_local_object
            (gbinder_local_object_new_reply(obj), test->connection),
            TX_RESULT2);
}

static
gpointer
test_nested_register_thread(
    gpointer user_data)
{
    TestNestedSync* test = user_data;
    GBinderReader reader;
    GBinderRemoteObject* connection;
    GBinderLocalObject* observer;
    GBinderClient* client;
    GBinderLocalRequest* req2;
    GBinderRemoteReply* reply2;
    GBinderLocalReply* complete;
    gint32 result = 0;
    int status2 = -1;

    observer = gbinder_local_object_new(test->obj->ipc, TEST_IFACES2,
        NULL, NULL);
    g_assert(observer);

    client = gbinder_client_new(test->accessor, TEST_IFACE2);
    req2 = gbinder_client_new_request(client);
    gbinder_local_request_append_local_object(req2, observer);
    reply2 = gbinder_client_transact_sync_reply2(client, TX_CODE2, req2,
        &status2, &gbinder_ipc_sync_worker);
    g_assert_cmpint(status2, == ,GBINDER_STATUS_OK);
    g_assert(reply2);

    gbinder_remote_reply_init_reader(reply2, &reader);
    g_assert((connection = gbinder_reader_read_object(&reader)));
    g_assert(gbinder_reader_read_int32(&reader, &result));
    g_assert_cmpint(result, == ,TX_RESULT2);
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_remote_object_unref(connection);
    gbinder_remote_reply_unref(reply2);
    gbinder_local_request_unref(req2);
    gbinder_client_unref(client);
    gbinder_local_object_drop(observer);

    complete = gbinder_local_reply_append_int32
        (gbinder_local_object_new_reply(test->obj), TX_RESULT1);
    gbinder_remote_request_complete(test->req, complete, GBINDER_STATUS_OK);
    gbinder_local_reply_unref(complete);
    return NULL;
}

static
GBinderLocalReply*
test_nested_register_cb(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestNestedSync* test = user_data;
    GBinderReader reader;

    GDEBUG("Nested register request handled");
    g_assert(!test->register_handled);
    test->register_handled = TRUE;
    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), TEST_IFACE));
    g_assert(code == TX_CODE);

    gbinder_remote_request_init_reader(req, &reader);
    g_assert((test->accessor = gbinder_reader_read_object(&reader)));
    /* Default system stability plus outer vendor stability requires VINTF. */
    g_assert_cmpint(test->accessor->stability, == ,GBINDER_STABILITY_VINTF);
    g_assert(gbinder_reader_at_end(&reader));

    gbinder_remote_request_block(req);
    test->obj = gbinder_local_object_ref(obj);
    test->req = gbinder_remote_request_ref(req);
    test->thread = g_thread_new("nested-register",
        test_nested_register_thread, test);

    *status = GBINDER_STATUS_OK;
    return NULL;
}

static
void
test_nested_register_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* data)
{
    TestNestedSync* test = data;
    GBinderReader reader;
    gint32 result = 0;

    GDEBUG("Nested register reply received");
    g_assert_cmpint(status, == ,GBINDER_STATUS_OK);
    g_assert(reply);

    gbinder_remote_reply_init_reader(reply, &reader);
    g_assert(gbinder_reader_read_int32(&reader, &result));
    g_assert_cmpint(result, == ,TX_RESULT1);
    g_assert(gbinder_reader_at_end(&reader));

    test->register_finished = TRUE;
    g_main_loop_quit(test->loop);
}

static
void
test_nested_sync_run(
    void)
{
    TestNestedSync test;
    GBinderLocalObject* obj;
    GBinderLocalObject* accessor;
    GBinderProxyObject* proxy;
    GBinderRemoteObject* remote_obj;
    GBinderRemoteObject* proxy_remote;
    GBinderClient* client;
    GBinderLocalRequest* req;
    GBinderIpc* ipc_obj;
    GBinderIpc* ipc_proxy;
    int fd_obj, fd_proxy;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    ipc_proxy = gbinder_ipc_new(DEV, "aidl3");
    ipc_obj = gbinder_ipc_new(DEV2, "aidl3");
    fd_proxy = gbinder_driver_fd(ipc_proxy->driver);
    fd_obj = gbinder_driver_fd(ipc_obj->driver);

    obj = gbinder_local_object_new(ipc_obj, TEST_IFACES,
        test_nested_register_cb, &test);
    remote_obj = gbinder_remote_object_new(ipc_obj,
        test_binder_register_object(fd_obj, obj, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    remote_obj->stability = GBINDER_STABILITY_VENDOR;

    g_assert((proxy = gbinder_proxy_object_new(ipc_proxy, remote_obj)));
    g_assert_cmpint(proxy->parent.stability, == ,GBINDER_STABILITY_VENDOR);
    proxy_remote = gbinder_remote_object_new(ipc_proxy,
        test_binder_register_object(fd_proxy, &proxy->parent, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    client = gbinder_client_new(proxy_remote, TEST_IFACE);

    accessor = gbinder_local_object_new(ipc_proxy, TEST_IFACES2,
        test_nested_accessor_cb, &test);
    req = gbinder_client_new_request(client);
    gbinder_local_request_append_local_object(req, accessor);
    g_assert(gbinder_client_transact(client, TX_CODE, 0, req,
        test_nested_register_reply, NULL, &test));
    gbinder_local_request_unref(req);

    test_run(&test_opt, test.loop);

    g_assert(test.register_handled);
    g_assert(test.connect_handled);
    g_assert(test.register_finished);

    g_thread_join(test.thread);
    test_binder_unregister_objects(fd_obj);
    test_binder_unregister_objects(fd_proxy);

    gbinder_local_object_drop(obj);
    gbinder_local_object_drop(accessor);
    gbinder_local_object_unref(test.obj);
    gbinder_local_object_drop(test.connection);
    gbinder_remote_object_unref(test.accessor);
    gbinder_remote_request_unref(test.req);
    gbinder_local_object_drop(&proxy->parent);
    gbinder_remote_object_unref(proxy_remote);
    gbinder_remote_object_unref(remote_obj);
    gbinder_client_unref(client);
    gbinder_ipc_unref(ipc_obj);
    gbinder_ipc_unref(ipc_proxy);
    test_binder_exit_wait(&test_opt, test.loop);
    g_main_loop_unref(test.loop);
}

static
void
test_nested_sync(
    void)
{
    test_run_in_context(&test_opt, test_nested_sync_run);
}

/*==========================================================================*
 * return_object
 *==========================================================================*/

typedef struct test_return_object {
    GMainLoop* loop;
    GBinderProxyObject* proxy;
    GBinderBuffer* buffer;
    GBinderRemoteObject* original;
    GBinderLocalObject* forwarded;
    GBINDER_STABILITY_LEVEL stability;
    guint calls;
    guint replies;
} TestReturnObject;

static
GBinderLocalReply*
test_return_object_cb(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestReturnObject* test = user_data;
    GBinderReader reader;
    GBinderRemoteObject* returned;
    gint32 marker;

    g_assert_cmpuint(code, == ,TX_CODE);
    g_assert(!flags);
    gbinder_remote_request_init_reader(req, &reader);
    g_assert((returned = gbinder_reader_read_object(&reader)));
    g_assert(returned->ipc == test->original->ipc);
    g_assert_cmpuint(returned->handle, == ,test->original->handle);
    g_assert_cmpint(returned->stability, == ,test->stability);
    gbinder_remote_object_unref(returned);
    g_assert(gbinder_reader_read_int32(&reader, &marker));
    g_assert_cmpint(marker, == ,TX_PARAM1);
    g_assert(gbinder_reader_at_end(&reader));
    test->calls++;
    *status = GBINDER_STATUS_OK;
    return gbinder_local_object_new_reply(obj);
}

static
GBinderLocalReply*
test_forward_object_cb(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestReturnObject* test = user_data;
    GBinderReader reader;
    GBinderRemoteObject* forwarded;
    GBinderProxyObject* proxy;

    gbinder_remote_request_init_reader(req, &reader);
    g_assert((forwarded = gbinder_reader_read_object(&reader)));
    g_assert_cmpint(forwarded->stability, == ,GBINDER_STABILITY_VINTF);
    g_assert(gbinder_reader_at_end(&reader));
    test->forwarded = test_binder_object(gbinder_driver_fd(obj->ipc->driver),
        forwarded->handle);
    g_assert(test->forwarded);
    proxy = GBINDER_PROXY_OBJECT(test->forwarded);
    g_assert(proxy->remote == test->original);
    g_assert_cmpint(proxy->remote->stability, == ,test->stability);
    gbinder_remote_object_unref(forwarded);
    *status = GBINDER_STATUS_OK;
    return gbinder_local_object_new_reply(obj);
}

static
void
test_return_object_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    TestReturnObject* test = user_data;

    g_assert_cmpint(status, == ,GBINDER_STATUS_OK);
    g_assert(reply);
    test->replies++;
    g_main_loop_quit(test->loop);
}

static
GBinderLocalReply*
test_return_object_forward(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestReturnObject* test = user_data;

    /* Keep the live transaction so the proxy can complete asynchronously,
     * replacing only the payload with the returning local Binder object. */
    g_assert(req->tx);
    g_assert(test->buffer);
    gbinder_remote_request_set_data(req, code, test->buffer);
    test->buffer = NULL;
    return gbinder_local_object_handle_transaction(&test->proxy->parent,
        req, code, flags, status);
}

static
void
test_return_object_run(
    gconstpointer param)
{
    GBinderIpc* src = gbinder_ipc_new(DEV, "aidl3");
    GBinderIpc* dest = gbinder_ipc_new(DEV2, "aidl3");
    int src_fd = gbinder_driver_fd(src->driver);
    int dest_fd = gbinder_driver_fd(dest->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* original = gbinder_local_object_new(dest,
        TEST_IFACES2, NULL, NULL);
    GBinderRemoteObject* remote = gbinder_object_registry_get_remote
        (gbinder_ipc_object_registry(dest),
        test_binder_register_object(dest_fd, original, 11),
        REMOTE_REGISTRY_CAN_CREATE);
    TestReturnObject test;
    GBinderLocalObject* receiver;
    GBinderRemoteObject* receiver_remote;
    GBinderProxyObject* receiver_proxy;
    GBinderLocalObject* return_sender;
    GBinderLocalObject* forward_receiver;
    GBinderRemoteObject* forward_remote;
    GBinderProxyObject* forward_proxy;
    GBinderRemoteObject* client_remote;
    GBinderClient* client;
    GBinderLocalRequest* local_req;
    GBinderOutputData* output;
    GUtilIntArray* offsets;
    guint8* bytes;
    void** objects;

    memset(&test, 0, sizeof(test));
    test.loop = loop;
    test.original = remote;
    test.stability = original->stability = GPOINTER_TO_INT(param);
    g_assert_cmpint(remote->stability, == ,GBINDER_STABILITY_SYSTEM);

    /* Forward the original object through a VINTF service. The converter
     * must retain its wire stability even when the proxy is promoted. */
    forward_receiver = gbinder_local_object_new(src, TEST_IFACES,
        test_forward_object_cb, &test);
    forward_remote = gbinder_remote_object_new(src,
        test_binder_register_object(src_fd, forward_receiver, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    forward_remote->stability = GBINDER_STABILITY_VINTF;
    forward_proxy = gbinder_proxy_object_new(dest, forward_remote);
    client_remote = gbinder_remote_object_new(dest,
        test_binder_register_object(dest_fd, &forward_proxy->parent, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    client = gbinder_client_new(client_remote, TEST_IFACE);
    local_req = gbinder_client_new_request(client);
    gbinder_local_request_append_local_object(local_req, original);
    /* The mock may deliver to a looper, which needs the main context. */
    g_assert(gbinder_client_transact(client, TX_CODE, 0, local_req,
        test_return_object_reply, NULL, &test));
    gbinder_local_request_unref(local_req);
    test_run(&test_opt, loop);
    g_assert_cmpuint(test.replies, == ,1);
    g_assert(test.forwarded);
    gbinder_client_unref(client);
    gbinder_remote_object_unref(client_remote);
    receiver = gbinder_local_object_new(dest, TEST_IFACES,
        test_return_object_cb, &test);
    receiver_remote = gbinder_remote_object_new(dest,
        test_binder_register_object(dest_fd, receiver, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    test.proxy = receiver_proxy = gbinder_proxy_object_new(src, receiver_remote);

    /* Emulate the kernel returning a proxy as BINDER_TYPE_BINDER to its
     * owner. The fake driver normally only converts BINDER to HANDLE. */
    local_req = gbinder_local_request_new_iface(gbinder_ipc_io(src),
        gbinder_ipc_protocol(src), TEST_IFACE);
    gbinder_local_request_append_local_object(local_req, test.forwarded);
    gbinder_local_request_append_int32(local_req, TX_PARAM1);
    output = gbinder_local_request_data(local_req);
    offsets = gbinder_output_data_offsets(output);
    bytes = gutil_memdup(output->bytes->data, output->bytes->len);
    g_assert_cmpuint(offsets->count, == ,1);
    objects = g_new0(void*, 2);
    objects[0] = bytes + offsets->data[0];
    test.buffer = gbinder_buffer_new(src->driver, bytes, output->bytes->len,
        objects);
    gbinder_local_request_unref(local_req);

    /* Inject the return payload into an asynchronous transaction. The
     * receiver must get the original handle, not another proxy node. */
    return_sender = gbinder_local_object_new(src, TEST_IFACES,
        test_return_object_forward, &test);
    client_remote = gbinder_remote_object_new(src,
        test_binder_register_object(src_fd, return_sender, AUTO_HANDLE),
        REMOTE_OBJECT_CREATE_ALIVE);
    client = gbinder_client_new(client_remote, TEST_IFACE);
    g_assert(gbinder_client_transact(client, TX_CODE, 0, NULL,
        test_return_object_reply, NULL, &test));
    test_run(&test_opt, loop);
    g_assert_cmpuint(test.replies, == ,2);
    g_assert_cmpuint(test.calls, == ,1);
    g_assert(!test.buffer);

    test_binder_unregister_objects(src_fd);
    test_binder_unregister_objects(dest_fd);
    gbinder_local_object_drop(return_sender);
    gbinder_local_object_drop(&receiver_proxy->parent);
    gbinder_local_object_drop(test.forwarded);
    gbinder_local_object_drop(&forward_proxy->parent);
    gbinder_local_object_drop(forward_receiver);
    gbinder_remote_object_unref(forward_remote);
    gbinder_remote_object_unref(client_remote);
    gbinder_client_unref(client);
    gbinder_local_object_drop(receiver);
    gbinder_local_object_drop(original);
    gbinder_remote_object_unref(receiver_remote);
    gbinder_remote_object_unref(remote);
    gbinder_ipc_unref(src);
    gbinder_ipc_unref(dest);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_return_object(
    gconstpointer param)
{
    test_run_in_context_param(&test_opt, test_return_object_run, param);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/proxy_object/" t

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
    g_test_add_func(TEST_("empty_reply"), test_empty_reply);
    g_test_add_func(TEST_("interface"), test_interface);
    g_test_add_func(TEST_("param"), test_param);
    g_test_add_func(TEST_("obj"), test_obj);
    g_test_add_func(TEST_("nested_sync"), test_nested_sync);
    g_test_add_data_func(TEST_("return_object/system"),
        GINT_TO_POINTER(GBINDER_STABILITY_SYSTEM), test_return_object);
    g_test_add_data_func(TEST_("return_object/vendor"),
        GINT_TO_POINTER(GBINDER_STABILITY_VENDOR), test_return_object);
    g_test_add_data_func(TEST_("return_object/vintf"),
        GINT_TO_POINTER(GBINDER_STABILITY_VINTF), test_return_object);

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
