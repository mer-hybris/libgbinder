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

#include "test_binder.h"

#include "gbinder_client_p.h"
#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_local_request.h"
#include "gbinder_object_registry.h"
#include "gbinder_output_data.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_remote_reply.h"

#include <gutil_log.h>

#include <errno.h>

static TestOpt test_opt;

static
GBinderClient*
test_client_new(
    guint h,
    const char* iface)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteObject* obj = gbinder_object_registry_get_remote(reg, h, TRUE);
    GBinderClient* client = gbinder_client_new(obj, iface);

    g_assert(client);
    gbinder_remote_object_unref(obj);
    gbinder_ipc_unref(ipc);
    return client;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!gbinder_client_new(NULL, NULL));
    g_assert(!gbinder_client_new2(NULL, NULL, 0));
    g_assert(!gbinder_client_ref(NULL));
    g_assert(!gbinder_client_interface(NULL));
    g_assert(!gbinder_client_interface2(NULL, 0));
    gbinder_client_unref(NULL);
    g_assert(!gbinder_client_new_request(NULL));
    g_assert(!gbinder_client_new_request2(NULL, 0));
    g_assert(!gbinder_client_transact_sync_reply(NULL, 0, NULL, NULL));
    g_assert(gbinder_client_transact_sync_oneway(NULL, 0, NULL) == (-EINVAL));
    g_assert(!gbinder_client_transact(NULL, 0, 0, NULL, NULL, NULL, NULL));
    gbinder_client_cancel(NULL, 0);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteObject* obj = gbinder_object_registry_get_remote(reg, 0, TRUE);
    const char* iface = "foo";
    GBinderClient* client = gbinder_client_new(obj, iface);

    g_assert(client);
    g_assert(gbinder_client_ref(client) == client);
    g_assert_cmpstr(gbinder_client_interface(client), == ,iface);
    gbinder_client_unref(client);
    gbinder_client_cancel(client, 0); /* does nothing */

    gbinder_client_unref(client);
    gbinder_remote_object_unref(obj);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * interfaces
 *==========================================================================*/

static
void
test_interfaces(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteObject* obj = gbinder_object_registry_get_remote(reg, 0, TRUE);
    static const GBinderClientIfaceInfo ifaces[] = {
        {"33", 33 }, { "11", 11 }, { "22", 22 }
    };
    GBinderClient* client = gbinder_client_new2(obj, ifaces,
        G_N_ELEMENTS(ifaces));

    g_assert(client);
    g_assert_cmpstr(gbinder_client_interface(client), == ,"11");
    g_assert_cmpstr(gbinder_client_interface2(client, 11), == ,"11");
    g_assert_cmpstr(gbinder_client_interface2(client, 22), == ,"22");
    g_assert_cmpstr(gbinder_client_interface2(client, 33), == ,"33");
    g_assert(!gbinder_client_interface2(client, 34));
    g_assert(!gbinder_client_new_request2(client, 34));
    /* Those fail to allocate default request for out-of-range codes: */
    g_assert(!gbinder_client_transact_sync_reply(client, 34, NULL, NULL));
    g_assert_cmpint(gbinder_client_transact_sync_oneway(client, 34, NULL),
        == ,-EINVAL);
    g_assert(!gbinder_client_transact(client, 34, 0, NULL, NULL, NULL, NULL));
    gbinder_client_unref(client);

    /* Client with no interface info */
    client = gbinder_client_new2(obj, NULL, 0);
    g_assert(client);
    g_assert(!gbinder_client_interface(client));
    g_assert(!gbinder_client_interface2(client, 1));
    gbinder_client_unref(client);

    gbinder_remote_object_unref(obj);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * no_header
 *==========================================================================*/

static
void
test_no_header(
    void)
{
    GBinderClient* client = test_client_new(0, NULL);
    int fd = gbinder_driver_fd(gbinder_client_ipc(client)->driver);

    test_binder_br_transaction_complete(fd);
    g_assert(gbinder_client_transact_sync_oneway(client, 0, NULL) ==
        GBINDER_STATUS_OK);

    gbinder_client_unref(client);
}

/*==========================================================================*
 * dead
 *==========================================================================*/

static
void
test_dead_done(
    GBinderRemoteObject* obj,
    void* user_data)
{
    GVERBOSE_("");
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_dead(
    void)
{
    const guint handle = 1;
    GBinderClient* client = test_client_new(handle, "foo");
    GBinderRemoteObject* obj = client->remote;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    const int fd = gbinder_driver_fd(gbinder_client_ipc(client)->driver);
    gbinder_remote_object_add_death_handler(obj, test_dead_done, loop);

    test_binder_br_dead_binder(fd, handle);
    test_binder_set_looper_enabled(fd, TEST_LOOPER_ENABLE);
    test_run(&test_opt, loop);
    g_assert(gbinder_remote_object_is_dead(obj));

    g_assert(!gbinder_client_transact_sync_reply(client, 0, NULL, NULL));
    g_assert(gbinder_client_transact_sync_oneway(client, 0, NULL) == -ESTALE);
    g_assert(!gbinder_client_transact(client, 0, 0, NULL, NULL, NULL, NULL));

    gbinder_client_unref(client);
    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * sync_oneway
 *==========================================================================*/

static
void
test_sync_oneway(
    void)
{
    GBinderClient* client = test_client_new(0, "foo");
    GBinderLocalRequest* req = gbinder_client_new_request(client);
    int fd = gbinder_driver_fd(gbinder_client_ipc(client)->driver);

    g_assert(req);
    test_binder_br_transaction_complete(fd);
    g_assert(gbinder_client_transact_sync_oneway(client, 0, req) ==
        GBINDER_STATUS_OK);
    gbinder_local_request_unref(req);

    /* Same but using the internal (empty) request */
    test_binder_br_transaction_complete(fd);
    g_assert(gbinder_client_transact_sync_oneway(client, 0, NULL) ==
        GBINDER_STATUS_OK);

    gbinder_client_unref(client);
}

/*==========================================================================*
 * sync_reply
 *==========================================================================*/

static
void
test_sync_reply_tx(
    GBinderClient* client,
    GBinderLocalRequest* req)
{
    GBinderDriver* driver = gbinder_client_ipc(client)->driver;
    int fd = gbinder_driver_fd(driver);
    const GBinderIo* io = gbinder_driver_io(driver);
    GBinderLocalReply* reply = gbinder_local_reply_new(io);
    GBinderRemoteReply* tx_reply;
    GBinderOutputData* data;
    const guint32 handle = 0;
    const guint32 code = 1;
    const char* result_in = "foo";
    char* result_out;
    int status = INT_MAX;

    g_assert(gbinder_local_reply_append_string16(reply, result_in));
    data = gbinder_local_reply_data(reply);
    g_assert(data);

    test_binder_br_noop(fd);
    test_binder_br_transaction_complete(fd);
    test_binder_br_noop(fd);
    test_binder_br_reply(fd, handle, code, data->bytes);

    tx_reply = gbinder_client_transact_sync_reply(client, 0, req, &status);
    g_assert(tx_reply);
    g_assert(status == GBINDER_STATUS_OK);

    result_out = gbinder_remote_reply_read_string16(tx_reply);
    g_assert(!g_strcmp0(result_out, result_in));
    g_free(result_out);

    gbinder_remote_reply_unref(tx_reply);
    gbinder_local_reply_unref(reply);
}

static
void
test_sync_reply(
    void)
{
    GBinderClient* client = test_client_new(0, "foo");
    GBinderLocalRequest* req = gbinder_client_new_request(client);

    test_sync_reply_tx(client, req);
    gbinder_local_request_unref(req);

    /* Same but using the internal (empty) request */
    test_sync_reply_tx(client, NULL);

    gbinder_client_unref(client);
}

/*==========================================================================*
 * reply
 *==========================================================================*/

#define TEST_INTERFACE "foo"
#define TEST_REQ_PARAM_STR "bar"

static
void
test_reply_destroy(
    void* user_data)
{
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_reply_ok_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    char* result;

    GVERBOSE_("%d", status);
    g_assert(status == GBINDER_STATUS_OK);
    g_assert(reply);
    result = gbinder_remote_reply_read_string16(reply);
    g_assert(!g_strcmp0(result, TEST_REQ_PARAM_STR));
    g_free(result);
}

static
void
test_reply_ok_quit(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    test_reply_ok_reply(client, reply, status, user_data);
    test_reply_destroy(user_data);
}

static
void
test_reply_tx(
    GBinderClient* client,
    GBinderLocalRequest* req,
    GBinderClientReplyFunc done,
    GDestroyNotify destroy)
{
    GBinderDriver* driver = gbinder_client_ipc(client)->driver;
    int fd = gbinder_driver_fd(driver);
    const GBinderIo* io = gbinder_driver_io(driver);
    GBinderLocalReply* reply = gbinder_local_reply_new(io);
    GBinderOutputData* data;
    const guint32 handle = 0;
    const guint32 code = 1;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id;

    g_assert(gbinder_local_reply_append_string16(reply, TEST_REQ_PARAM_STR));
    data = gbinder_local_reply_data(reply);
    g_assert(data);

    test_binder_br_noop(fd);
    test_binder_br_transaction_complete(fd);
    test_binder_br_noop(fd);
    test_binder_br_reply(fd, handle, code, data->bytes);

    id = gbinder_client_transact(client, 0, 0, req, done, destroy, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    gbinder_local_reply_unref(reply);
    g_main_loop_unref(loop);
}

static
void
test_reply(
    GBinderClientReplyFunc done,
    GDestroyNotify destroy)
{
    GBinderClient* client = test_client_new(0, TEST_INTERFACE);
    GBinderLocalRequest* req = gbinder_client_new_request2(client, 0);

    g_assert(req);
    test_reply_tx(client, req, done, destroy);
    gbinder_local_request_unref(req);

    /* Same but using the internal (empty) request */
    test_reply_tx(client, NULL, done, destroy);

    gbinder_client_unref(client);
}

static
void
test_reply_ok1(
    void)
{
    test_reply(test_reply_ok_reply, test_reply_destroy);
}

static
void
test_reply_ok2(
    void)
{
    test_reply(NULL, test_reply_destroy);
}

static
void
test_reply_ok3(
    void)
{
    test_reply(test_reply_ok_quit, NULL);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/client/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("interfaces"), test_interfaces);
    g_test_add_func(TEST_("dead"), test_dead);
    g_test_add_func(TEST_("no_header"), test_no_header);
    g_test_add_func(TEST_("sync_oneway"), test_sync_oneway);
    g_test_add_func(TEST_("sync_reply"), test_sync_reply);
    g_test_add_func(TEST_("reply/ok1"), test_reply_ok1);
    g_test_add_func(TEST_("reply/ok2"), test_reply_ok2);
    g_test_add_func(TEST_("reply/ok3"), test_reply_ok3);
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
