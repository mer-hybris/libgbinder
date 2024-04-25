/*
 * Copyright (C) 2018-2022 Jolla Ltd.
 * Copyright (C) 2018-2022 Slava Monich <slava.monich@jolla.com>
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
#include "gbinder_driver.h"
#include "gbinder_local_object_p.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_local_request_p.h"
#include "gbinder_object_registry.h"
#include "gbinder_output_data.h"
#include "gbinder_remote_reply.h"
#include "gbinder_remote_request.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_writer.h"

#include <gutil_log.h>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

static TestOpt test_opt;

static
gboolean
test_unref_ipc(
    gpointer ipc)
{
    gbinder_ipc_unref(ipc);
    return G_SOURCE_REMOVE;
}

static
void
test_quit_when_destroyed(
    gpointer loop,
    GObject* obj)
{
    test_quit_later((GMainLoop*)loop);
}

static
GBinderLocalRequest*
test_local_request_new(
    GBinderIpc* ipc)
{
    return gbinder_local_request_new(gbinder_driver_io(ipc->driver),
        gbinder_driver_protocol(ipc->driver), NULL);
}

static
GBinderLocalReply*
test_local_reply_new(
    GBinderIpc* ipc)
{
    return gbinder_local_reply_new(gbinder_driver_io(ipc->driver),
        gbinder_driver_protocol(ipc->driver));
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    int status = INT_MAX;

    g_assert(!gbinder_ipc_ref(NULL));
    gbinder_ipc_unref(NULL);
    g_assert(!gbinder_ipc_sync_main.sync_reply(NULL, 0, 0, NULL, NULL));
    g_assert(!gbinder_ipc_sync_main.sync_reply(NULL, 0, 0, NULL, &status));
    g_assert_cmpint(status, == ,-EINVAL);
    g_assert(!gbinder_ipc_sync_worker.sync_reply(NULL, 0, 0, NULL, NULL));
    g_assert(!gbinder_ipc_sync_worker.sync_reply(NULL, 0, 0, NULL, &status));
    g_assert_cmpint(status, == ,-EINVAL);
    g_assert_cmpint(gbinder_ipc_sync_main.sync_oneway(NULL, 0, 0, NULL), == ,
        -EINVAL);
    g_assert_cmpint(gbinder_ipc_sync_worker.sync_oneway(NULL, 0, 0, NULL), == ,
        -EINVAL);
    g_assert(!gbinder_ipc_transact(NULL, 0, 0, 0, NULL, NULL, NULL, NULL));
    g_assert(!gbinder_ipc_transact_custom(NULL, NULL, NULL, NULL, NULL));
    g_assert(!gbinder_ipc_object_registry(NULL));
    gbinder_ipc_looper_check(NULL);
    gbinder_ipc_cancel(NULL, 0);

    g_assert(!gbinder_object_registry_ref(NULL));
    gbinder_object_registry_unref(NULL);
    g_assert(!gbinder_object_registry_get_local(NULL, NULL));
    g_assert(!gbinder_object_registry_get_remote(NULL, 0, FALSE));
    g_assert(!gbinder_ipc_find_local_object(NULL, NULL, NULL));
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
gboolean
test_basic_find_none(
    GBinderLocalObject* obj,
    void* user_data)
{
    return FALSE;
}

static
gboolean
test_basic_find(
    GBinderLocalObject* obj,
    void* user_data)
{
    return obj == user_data;
}

static
void
test_basic(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderIpc* ipc2 = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER, NULL);
    GBinderLocalObject* obj;

    g_assert(ipc);
    g_assert(ipc2);
    g_assert(ipc != ipc2);
    gbinder_ipc_cancel(ipc2, 0); /* not a valid transaction */
    gbinder_ipc_unref(ipc2);

    g_assert(!gbinder_ipc_find_local_object(NULL, test_basic_find_none, NULL));
    g_assert(!gbinder_ipc_find_local_object(ipc, test_basic_find_none, NULL));
    obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    g_assert(obj);
    g_assert(!gbinder_ipc_find_local_object(ipc, test_basic_find_none, NULL));
    g_assert(gbinder_ipc_find_local_object(ipc, test_basic_find, obj) == obj);
    gbinder_local_object_unref(obj); /* Above call added a reference */
    gbinder_local_object_unref(obj);

    /* Second gbinder_ipc_new returns the same (default) object */
    g_assert(gbinder_ipc_new(NULL, NULL) == ipc);
    g_assert(gbinder_ipc_new("", NULL) == ipc);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_unref(ipc);

    /* Invalid path */
    g_assert(!gbinder_ipc_new("invalid path", NULL));

    test_binder_exit_wait(&test_opt, NULL);
}

/*==========================================================================*
 * protocol
 *==========================================================================*/

static
void
test_protocol(
    void)
{
    /* GBinderIpc objects are identified by device + protocol combination */
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, "aidl");
    GBinderIpc* ipc2 = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, "hidl");

    g_assert(ipc);
    g_assert(ipc2);
    g_assert(ipc != ipc2);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_unref(ipc2);

    test_binder_exit_wait(&test_opt, NULL);
}

/*==========================================================================*
 * async_oneway
 *==========================================================================*/

static
void
test_async_oneway_done(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    g_assert(!status);
    g_assert(!reply);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_async_oneway(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id;

    test_binder_br_transaction_complete(fd, TX_THREAD);
    id = gbinder_ipc_transact(ipc, 0, 1, GBINDER_TX_FLAG_ONEWAY,
        req, test_async_oneway_done, NULL, loop);
    g_assert(id);
    test_run(&test_opt, loop);

    gbinder_local_request_unref(req);
    gbinder_ipc_unref(ipc);
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
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);

    test_binder_br_transaction_complete(fd, THIS_THREAD);
    g_assert_cmpint(gbinder_ipc_sync_main.sync_oneway(ipc, 0, 1, req), == ,0);
    gbinder_local_request_unref(req);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
}

/*==========================================================================*
 * sync_reply_ok
 *==========================================================================*/

static
void
test_sync_reply_ok_status(
    int* status)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    GBinderLocalReply* reply = test_local_reply_new(ipc);
    GBinderRemoteReply* tx_reply;
    GBinderOutputData* data;
    const int fd = gbinder_driver_fd(ipc->driver);
    const guint32 handle = 0;
    const guint32 code = 1;
    const char* result_in = "foo";
    char* result_out;

    g_assert(gbinder_local_reply_append_string16(reply, result_in));
    data = gbinder_local_reply_data(reply);
    g_assert(data);

    test_binder_br_noop(fd, THIS_THREAD);
    test_binder_br_transaction_complete(fd, THIS_THREAD);
    test_binder_br_noop(fd, THIS_THREAD);
    test_binder_br_reply(fd, THIS_THREAD, handle, code, data->bytes);

    tx_reply = gbinder_ipc_sync_main.sync_reply(ipc, handle, code, req, status);
    g_assert(tx_reply);

    result_out = gbinder_remote_reply_read_string16(tx_reply);
    g_assert(!g_strcmp0(result_out, result_in));
    g_free(result_out);

    gbinder_remote_reply_unref(tx_reply);
    gbinder_local_request_unref(req);
    gbinder_local_reply_unref(reply);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
}

static
void
test_sync_reply_ok(
    void)
{
    int status = -1;

    test_sync_reply_ok_status(NULL);
    test_sync_reply_ok_status(&status);
    g_assert(status == GBINDER_STATUS_OK);
}

/*==========================================================================*
 * sync_reply_error
 *==========================================================================*/

static
void
test_sync_reply_error(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);
    const guint32 handle = 0;
    const guint32 code = 1;
    const gint expected_status = (-EINVAL);
    const gint unexpected_status = GBINDER_STATUS_FAILED;
    int status = INT_MAX;

    test_binder_ignore_dead_object(fd);
    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_transaction_complete(fd, TX_THREAD);
    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_reply_status(fd, TX_THREAD, expected_status);

    g_assert(!gbinder_ipc_sync_main.sync_reply(ipc,handle,code,req,&status));
    g_assert_cmpint(status, == ,expected_status);

    /* Should return GBINDER_STATUS_FAILED */
    test_binder_ignore_dead_object(fd);
    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_transaction_complete(fd, TX_THREAD);
    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_reply_status(fd, TX_THREAD, unexpected_status);

    g_assert(!gbinder_ipc_sync_main.sync_reply(ipc,handle,code,req,&status));
    g_assert_cmpint(status, == , GBINDER_STATUS_FAILED);

    gbinder_local_request_unref(req);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
}

/*==========================================================================*
 * transact_ok
 *==========================================================================*/

#define TEST_REQ_PARAM_STR "foo"

static
void
test_transact_ok_destroy(
    void* user_data)
{
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_transact_ok_done(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    char* result;

    GVERBOSE_("");
    result = gbinder_remote_reply_read_string16(reply);
    g_assert(!g_strcmp0(result, TEST_REQ_PARAM_STR));
    g_free(result);
    g_assert(status == GBINDER_STATUS_OK);
}

static
void
test_transact_ok(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    GBinderLocalReply* reply = test_local_reply_new(ipc);
    GBinderOutputData* data;
    const guint32 handle = 0;
    const guint32 code = 1;
    const int fd = gbinder_driver_fd(ipc->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id;

    g_assert(gbinder_local_reply_append_string16(reply, TEST_REQ_PARAM_STR));
    data = gbinder_local_reply_data(reply);
    g_assert(data);

    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_transaction_complete(fd, TX_THREAD);
    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_reply(fd, TX_THREAD, handle, code, data->bytes);

    id = gbinder_ipc_transact(ipc, handle, code, 0, req,
        test_transact_ok_done, test_transact_ok_destroy, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    /* Transaction id is not valid anymore: */
    gbinder_ipc_cancel(ipc, id);
    gbinder_local_request_unref(req);
    gbinder_local_reply_unref(reply);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_dead
 *==========================================================================*/

static
void
test_transact_dead_done(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    GVERBOSE_("%d", status);
    g_assert(!reply);
    g_assert(status == GBINDER_STATUS_DEAD_OBJECT);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_transact_dead(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id;

    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_dead_reply(fd, TX_THREAD);

    id = gbinder_ipc_transact(ipc, 1, 2, 0, req, test_transact_dead_done,
        NULL, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    /* Transaction id is not valid anymore: */
    gbinder_ipc_cancel(ipc, id);
    gbinder_local_request_unref(req);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_failed
 *==========================================================================*/

static
void
test_transact_failed_done(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    GVERBOSE_("%d", status);
    g_assert(!reply);
    g_assert(status == GBINDER_STATUS_FAILED);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_transact_failed(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id;

    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_failed_reply(fd, TX_THREAD);

    id = gbinder_ipc_transact(ipc, 1, 2, 0, req, test_transact_failed_done,
        NULL, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    /* Transaction id is not valid anymore: */
    gbinder_ipc_cancel(ipc, id);
    gbinder_local_request_unref(req);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_status
 *==========================================================================*/

#define EXPECTED_STATUS (0x42424242)

static
void
test_transact_status_done(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    GVERBOSE_("%d", status);
    g_assert(!reply);
    g_assert(status == EXPECTED_STATUS);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_transact_status(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id;

    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_reply_status(fd, TX_THREAD, EXPECTED_STATUS);

    id = gbinder_ipc_transact(ipc, 1, 2, 0, req, test_transact_status_done,
        NULL, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    /* Transaction id is not valid anymore: */
    gbinder_ipc_cancel(ipc, id);
    gbinder_local_request_unref(req);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_custom
 *==========================================================================*/

static
void
test_transact_custom_done(
    const GBinderIpcTx* tx)
{
    GVERBOSE_("");
    test_quit_later((GMainLoop*)tx->user_data);
}

static
void
test_transact_custom(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id = gbinder_ipc_transact_custom(ipc, NULL,
        test_transact_custom_done, NULL, loop);

    g_assert(id);
    test_run(&test_opt, loop);

    gbinder_ipc_exit();
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_custom2
 *==========================================================================*/

static
void
test_transact_custom_destroy(
    void* user_data)
{
    GVERBOSE_("");
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_transact_custom2(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id = gbinder_ipc_transact_custom(ipc, NULL, NULL,
        test_transact_custom_destroy, loop);

    g_assert(id);
    test_run(&test_opt, loop);

    gbinder_ipc_exit();
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_custom3
 *==========================================================================*/

static
void
test_transact_custom3_exec(
    const GBinderIpcTx* tx)
{
    GVERBOSE_("");
    gbinder_ipc_unref(tx->ipc);
    test_quit_later((GMainLoop*)tx->user_data);
}

static
void
test_transact_custom3(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    /* Reusing test_transact_cancel_done and test_transact_cancel_destroy */
    gulong id = gbinder_ipc_transact_custom(ipc, test_transact_custom3_exec,
        NULL, NULL, loop);

    g_assert(id);
    test_run(&test_opt, loop);

    /* Reference to GBinderIpc is released by test_transact_custom3_exec */
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_cancel
 *==========================================================================*/

static
void
test_transact_cancel_destroy(
    void* user_data)
{
    GVERBOSE_("");
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_transact_cancel_exec(
    const GBinderIpcTx* tx)
{
    GVERBOSE_("");
}

static
void
test_transact_cancel_done(
    const GBinderIpcTx* tx)
{
    GVERBOSE_("");
    g_assert(tx->cancelled);
}

static
void
test_transact_cancel(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id = gbinder_ipc_transact_custom(ipc, test_transact_cancel_exec,
        test_transact_cancel_done, test_transact_cancel_destroy, loop);

    g_assert(id);
    gbinder_ipc_cancel(ipc, id);
    test_run(&test_opt, loop);

    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_cancel2
 *==========================================================================*/

static
gboolean
test_transact_cancel2_cancel(
    gpointer data)
{
    const GBinderIpcTx* tx = data;

    GVERBOSE_("");
    gbinder_ipc_cancel(tx->ipc, tx->id);
    return G_SOURCE_REMOVE;
}

static
void
test_transact_cancel2_exec(
    const GBinderIpcTx* tx)
{
    GVERBOSE_("");
    g_assert(!tx->cancelled);
    g_main_context_invoke(NULL, test_transact_cancel2_cancel, (void*)tx);
}

static
void
test_transact_cancel2(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    /* Reusing test_transact_cancel_done and test_transact_cancel_destroy */
    gulong id = gbinder_ipc_transact_custom(ipc, test_transact_cancel2_exec,
        test_transact_cancel_done, test_transact_cancel_destroy, loop);

    g_assert(id);
    test_run(&test_opt, loop);

    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * transact_2way
 *==========================================================================*/

static
GBinderLocalReply*
test_transact_2way_incoming_proc(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    int* incoming_call = user_data;

    GVERBOSE_("\"%s\" %u", gbinder_remote_request_interface(req), code);
    g_assert_cmpuint(flags, == ,0);
    g_assert_cmpint(gbinder_remote_request_sender_pid(req), == ,getpid());
    g_assert_cmpint(gbinder_remote_request_sender_euid(req), == ,geteuid());
    g_assert_cmpstr(gbinder_remote_request_interface(req), == ,"test");
    g_assert_cmpstr(gbinder_remote_request_read_string8(req), == ,"message");
    g_assert_cmpuint(code, == ,2);
    g_assert_cmpint(*incoming_call, == ,0);
    (*incoming_call)++;

    *status = GBINDER_STATUS_OK;
    return gbinder_local_object_new_reply(obj);
}

static
void
test_transact_2way_run(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* dev = gbinder_driver_dev(ipc->driver);
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    const char* const ifaces[] = { "test", NULL };
    const guint32 handle = 0;
    const guint32 code = 1;
    int incoming_call = 0;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, ifaces, test_transact_2way_incoming_proc, &incoming_call);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    GBinderLocalRequest* incoming_req = test_local_request_new(ipc);
    GBinderLocalReply* reply = test_local_reply_new(ipc);
    GBinderWriter writer;

    /* Prepare reply */
    g_assert(gbinder_local_reply_append_string16(reply, TEST_REQ_PARAM_STR));

    /* Prepare incoming request */
    gbinder_local_request_init_writer(req, &writer);
    prot->write_rpc_header(&writer, "test");
    gbinder_writer_append_string8(&writer, "message");

    test_binder_ignore_dead_object(fd);
    test_binder_br_transaction(fd, TX_THREAD, obj, 2,
        gbinder_local_request_data(req)->bytes);
    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_transaction_complete(fd, TX_THREAD);
    test_binder_br_noop(fd, TX_THREAD);
    test_binder_br_reply(fd, TX_THREAD, handle, code,
        gbinder_local_reply_data(reply)->bytes);

    /* NB. Reusing test_transact_ok_done and test_transact_ok_destroy */
    g_assert(gbinder_ipc_transact(ipc, handle, code, 0, req,
        test_transact_ok_done, test_transact_ok_destroy, loop));

    test_run(&test_opt, loop);

    /* Now we need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_local_object_unref(obj);
    gbinder_local_request_unref(req);
    gbinder_local_request_unref(incoming_req);
    gbinder_local_reply_unref(reply);
    g_idle_add(test_unref_ipc, ipc);
    test_run(&test_opt, loop);

    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_transact_2way(
    void)
{
    test_run_in_context(&test_opt, test_transact_2way_run);
}

/*==========================================================================*
 * transact_unhandled
 *==========================================================================*/

static
void
test_transact_unhandled_done(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    g_assert(!reply);
    g_assert_cmpint(status, == ,GBINDER_STATUS_DEAD_OBJECT);
    test_quit_later((GMainLoop*)user_data);
}


static
void
test_transact_unhandled_run(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderDriver* driver = ipc->driver;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalRequest* req = gbinder_driver_local_request_new_ping(driver);

    g_assert(gbinder_ipc_transact(ipc, 1 /* Non-existent object */,
        gbinder_driver_protocol(driver)->ping_tx, 0, req,
        test_transact_unhandled_done, NULL, loop));
    gbinder_local_request_unref(req);
    test_run(&test_opt, loop);

    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_transact_unhandled(
    void)
{
    test_run_in_context(&test_opt, test_transact_unhandled_run);
}

/*==========================================================================*
 * transact_incoming
 *==========================================================================*/

static
GBinderLocalReply*
test_transact_incoming_proc(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    GVERBOSE_("\"%s\" %u", gbinder_remote_request_interface(req), code);
    g_assert(!flags);
    g_assert(gbinder_remote_request_sender_pid(req) == getpid());
    g_assert(gbinder_remote_request_sender_euid(req) == geteuid());
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), "test"));
    g_assert(!g_strcmp0(gbinder_remote_request_read_string8(req), "message"));
    g_assert(code == 1);
    test_quit_later((GMainLoop*)user_data);

    *status = GBINDER_STATUS_OK;
    return gbinder_local_object_new_reply(obj);
}

static
void
test_transact_incoming_run(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* dev = gbinder_driver_dev(ipc->driver);
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    const char* const ifaces[] = { "test", NULL };
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, ifaces, test_transact_incoming_proc, loop);
    GBinderLocalRequest* ping = test_local_request_new(ipc);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    GBinderWriter writer;

    gbinder_local_request_init_writer(ping, &writer);
    prot->write_ping(&writer);

    gbinder_local_request_init_writer(req, &writer);
    prot->write_rpc_header(&writer, "test");
    gbinder_writer_append_string8(&writer, "message");

    test_binder_br_transaction(fd, LOOPER_THREAD, obj, prot->ping_tx,
        gbinder_local_request_data(ping)->bytes);
    test_binder_br_transaction_complete(fd, LOOPER_THREAD); /* For reply */
    test_binder_br_transaction(fd, LOOPER_THREAD, obj, 1,
        gbinder_local_request_data(req)->bytes);
    test_binder_br_transaction_complete(fd, LOOPER_THREAD); /* For reply */
    test_run(&test_opt, loop);

    /* Now we need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_local_object_unref(obj);
    gbinder_local_request_unref(ping);
    gbinder_local_request_unref(req);
    g_idle_add(test_unref_ipc, ipc);
    test_run(&test_opt, loop);

    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_transact_incoming(
    void)
{
    test_run_in_context(&test_opt, test_transact_incoming_run);
}

/*==========================================================================*
 * transact_status_reply
 *==========================================================================*/

static
GBinderLocalReply*
test_transact_status_reply_proc(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    GVERBOSE_("\"%s\" %u", gbinder_remote_request_interface(req), code);
    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), "test"));
    g_assert(!g_strcmp0(gbinder_remote_request_read_string8(req), "message"));
    g_assert(code == 1);
    test_quit_later((GMainLoop*)user_data);

    *status = EXPECTED_STATUS;
    return NULL;
}

static
void
test_transact_status_reply_run(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* dev = gbinder_driver_dev(ipc->driver);
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    const char* const ifaces[] = { "test", NULL };
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, ifaces, test_transact_status_reply_proc, loop);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    prot->write_rpc_header(&writer, "test");
    gbinder_writer_append_string8(&writer, "message");
    data = gbinder_local_request_data(req);

    test_binder_br_transaction(fd, LOOPER_THREAD, obj, 1, data->bytes);
    test_binder_br_transaction_complete(fd, LOOPER_THREAD); /* For reply */
    test_run(&test_opt, loop);

    /* Now we need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_local_object_unref(obj);
    gbinder_local_request_unref(req);
    g_idle_add(test_unref_ipc, ipc);
    test_run(&test_opt, loop);

    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_transact_status_reply(
    void)
{
    test_run_in_context(&test_opt, test_transact_status_reply_run);
}

/*==========================================================================*
 * transact_async
 *==========================================================================*/

typedef struct test_transact_async_req {
    GBinderLocalObject* obj;
    GBinderRemoteRequest* req;
    GMainLoop* loop;
} TestTransactAsyncReq;

static
void
test_transact_async_done(
    gpointer data)
{
    TestTransactAsyncReq* test = data;

    gbinder_local_object_unref(test->obj);
    gbinder_remote_request_unref(test->req);
    test_quit_later(test->loop);
    g_free(test);
}

static
gboolean
test_transact_async_reply(
    gpointer data)
{
    TestTransactAsyncReq* test = data;
    GBinderLocalReply* reply = gbinder_local_object_new_reply(test->obj);

    gbinder_remote_request_complete(test->req, reply, 0);
    gbinder_local_reply_unref(reply);
    return G_SOURCE_REMOVE;
}

static
GBinderLocalReply*
test_transact_async_proc(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* loop)
{
    TestTransactAsyncReq* test = g_new(TestTransactAsyncReq, 1);

    GVERBOSE_("\"%s\" %u", gbinder_remote_request_interface(req), code);
    g_assert(!flags);
    g_assert(gbinder_remote_request_sender_pid(req) == getpid());
    g_assert(gbinder_remote_request_sender_euid(req) == geteuid());
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), "test"));
    g_assert(!g_strcmp0(gbinder_remote_request_read_string8(req), "message"));
    g_assert(code == 1);

    test->obj = gbinder_local_object_ref(obj);
    test->req = gbinder_remote_request_ref(req);
    test->loop = (GMainLoop*)loop;

    gbinder_remote_request_block(req);
    gbinder_remote_request_block(req); /* wrong state; has no effect */

    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, test_transact_async_reply, test,
        test_transact_async_done);
    return NULL;
}

static
void
test_transact_async_run(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* dev = gbinder_driver_dev(ipc->driver);
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    const char* const ifaces[] = { "test", NULL };
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, ifaces, test_transact_async_proc, loop);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    prot->write_rpc_header(&writer, "test");
    gbinder_writer_append_string8(&writer, "message");
    data = gbinder_local_request_data(req);

    test_binder_br_transaction(fd, LOOPER_THREAD, obj, 1, data->bytes);
    test_binder_br_transaction_complete(fd, LOOPER_THREAD); /* For reply */
    test_run(&test_opt, loop);

    /* Now we need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_local_object_unref(obj);
    gbinder_local_request_unref(req);
    g_idle_add(test_unref_ipc, ipc);
    test_run(&test_opt, loop);

    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_transact_async(
    void)
{
    test_run_in_context(&test_opt, test_transact_async_run);
}

/*==========================================================================*
 * transact_async_sync
 *==========================================================================*/

static
GBinderLocalReply*
test_transact_async_sync_proc(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* loop)
{
    GBinderLocalReply* reply = gbinder_local_object_new_reply(obj);

    GVERBOSE_("\"%s\" %u", gbinder_remote_request_interface(req), code);
    g_assert(!flags);
    g_assert(gbinder_remote_request_sender_pid(req) == getpid());
    g_assert(gbinder_remote_request_sender_euid(req) == geteuid());
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), "test"));
    g_assert(!g_strcmp0(gbinder_remote_request_read_string8(req), "message"));
    g_assert(code == 1);

    /* Block and immediately complete the call */
    gbinder_remote_request_block(req);
    gbinder_remote_request_complete(req, reply, 0);
    gbinder_remote_request_complete(req, reply, 0); /* This one is ignored */
    gbinder_local_reply_unref(reply);

    test_quit_later((GMainLoop*)loop);
    return NULL;
}

static
void
test_transact_async_sync_run(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* dev = gbinder_driver_dev(ipc->driver);
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    const char* const ifaces[] = { "test", NULL };
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, ifaces, test_transact_async_sync_proc, loop);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    GBinderOutputData* data;
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    prot->write_rpc_header(&writer, "test");
    gbinder_writer_append_string8(&writer, "message");
    data = gbinder_local_request_data(req);

    test_binder_br_transaction(fd, LOOPER_THREAD, obj, 1, data->bytes);
    test_binder_br_transaction_complete(fd, LOOPER_THREAD); /* For reply */
    test_run(&test_opt, loop);

    /* Now we need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_local_object_unref(obj);
    gbinder_local_request_unref(req);
    g_idle_add(test_unref_ipc, ipc);
    test_run(&test_opt, loop);

    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_transact_async_sync(
    void)
{
    test_run_in_context(&test_opt, test_transact_async_sync_run);
}

/*==========================================================================*
 * drop_remote_refs
 *==========================================================================*/

static
void
test_drop_remote_refs_cb(
    GBinderLocalObject* obj,
    void* user_data)
{
    GVERBOSE_("%d", obj->strong_refs);
    g_assert(obj->strong_refs == 1);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_drop_remote_refs_run(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, NULL, NULL, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd = gbinder_driver_fd(ipc->driver);
    gulong id = gbinder_local_object_add_strong_refs_changed_handler(obj,
        test_drop_remote_refs_cb, loop);

    test_binder_br_acquire(fd, ANY_THREAD, obj);
    test_run(&test_opt, loop);

    g_assert(obj->strong_refs == 1);
    gbinder_local_object_remove_handler(obj, id);
    gbinder_local_object_unref(obj);

    /* gbinder_ipc_exit will drop the remote reference */
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_drop_remote_refs(
    void)
{
    test_run_in_context(&test_opt, test_drop_remote_refs_run);
}

/*==========================================================================*
 * cancel_on_exit
 *==========================================================================*/

static
void
test_cancel_on_exit_not_reached(
    GBinderIpc* ipc,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    g_assert_not_reached();
}

static
void
test_cancel_on_exit(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = test_local_request_new(ipc);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd = gbinder_driver_fd(ipc->driver);

    /* This transaction will be cancelled by gbinder_ipc_exit */
    test_binder_br_transaction_complete(fd, TX_THREAD);
    gbinder_ipc_transact(ipc, 0, 1, GBINDER_TX_FLAG_ONEWAY,
        req, test_cancel_on_exit_not_reached, NULL, NULL);

    gbinder_local_request_unref(req);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/ipc/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("protocol"), test_protocol);
    g_test_add_func(TEST_("async_oneway"), test_async_oneway);
    g_test_add_func(TEST_("sync_oneway"), test_sync_oneway);
    g_test_add_func(TEST_("sync_reply_ok"), test_sync_reply_ok);
    g_test_add_func(TEST_("sync_reply_error"), test_sync_reply_error);
    g_test_add_func(TEST_("transact_ok"), test_transact_ok);
    g_test_add_func(TEST_("transact_dead"), test_transact_dead);
    g_test_add_func(TEST_("transact_failed"), test_transact_failed);
    g_test_add_func(TEST_("transact_status"), test_transact_status);
    g_test_add_func(TEST_("transact_custom"), test_transact_custom);
    g_test_add_func(TEST_("transact_custom2"), test_transact_custom2);
    g_test_add_func(TEST_("transact_custom3"), test_transact_custom3);
    g_test_add_func(TEST_("transact_cancel"), test_transact_cancel);
    g_test_add_func(TEST_("transact_cancel2"), test_transact_cancel2);
    g_test_add_func(TEST_("transact_2way"), test_transact_2way);
    g_test_add_func(TEST_("transact_incoming"), test_transact_incoming);
    g_test_add_func(TEST_("transact_unhandled"), test_transact_unhandled);
    g_test_add_func(TEST_("transact_status_reply"), test_transact_status_reply);
    g_test_add_func(TEST_("transact_async"), test_transact_async);
    g_test_add_func(TEST_("transact_async_sync"), test_transact_async_sync);
    g_test_add_func(TEST_("drop_remote_refs"), test_drop_remote_refs);
    g_test_add_func(TEST_("cancel_on_exit"), test_cancel_on_exit);
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
