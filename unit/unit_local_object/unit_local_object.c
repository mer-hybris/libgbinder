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

#include "test_binder.h"

#include "gbinder_buffer_p.h"
#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_local_object_p.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_object_registry.h"
#include "gbinder_output_data.h"
#include "gbinder_reader_p.h"
#include "gbinder_remote_request_p.h"
#include "gbinder_rpc_protocol.h"

#include <gutil_intarray.h>
#include <gutil_strv.h>
#include <gutil_log.h>

#include <errno.h>

static TestOpt test_opt;

/* android.hidl.base@1.0::IBase */
#define TEST_BASE_INTERFACE_BYTES \
    'a', 'n', 'd', 'r', 'o', 'i', 'd', '.', \
    'h', 'i', 'd', 'l', '.', 'b', 'a', 's', \
    'e', '@', '1', '.', '0', ':', ':', 'I', \
    'B', 'a', 's', 'e'
#define TEST_BASE_INTERFACE_HEADER_BYTES \
    TEST_BASE_INTERFACE_BYTES, 0x00, 0x00, 0x00, 0x00
static const char base_interface[] = { TEST_BASE_INTERFACE_BYTES, 0x00 };

static
void
test_reader_data_init_for_reply(
    GBinderReaderData* data,
    GBinderLocalObject* obj,
    GBinderLocalReply* reply)
{
    GBinderIpc* ipc = obj->ipc;
    GBinderOutputData* out = gbinder_local_reply_data(reply);
    GUtilIntArray* offsets = gbinder_output_data_offsets(out);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderBuffer* buf = gbinder_buffer_new(ipc->driver,
        g_memdup(out->bytes->data, out->bytes->len),
        out->bytes->len, NULL);

    memset(data, 0, sizeof(*data));
    data->buffer = buf;
    data->reg = gbinder_object_registry_ref(reg);
    g_assert(!gbinder_object_registry_get_local(reg, NULL));
    g_assert(gbinder_object_registry_get_local(reg, obj) == obj);
    gbinder_local_object_unref(obj); /* ref added by the above call */
    if (offsets && offsets->count > 0) {
        guint i;

        data->objects = g_new(void*, offsets->count + 1);
        for (i = 0; i < offsets->count; i++) {
            data->objects[i] = (guint8*)buf->data + offsets->data[i];
        }
        data->objects[i] = NULL;
    }
}

static
void
test_reader_data_cleanup(
    GBinderReaderData* data)
{
    gbinder_object_registry_unref(data->reg);
    gbinder_buffer_free(data->buffer);
    g_free(data->objects);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    int status = 0;

    g_assert(!gbinder_local_object_new(NULL, NULL, NULL, NULL));
    g_assert(!gbinder_local_object_ref(NULL));
    gbinder_local_object_unref(NULL);
    gbinder_local_object_drop(NULL);
    g_assert(!gbinder_local_object_new_reply(NULL));
    g_assert(!gbinder_local_object_add_weak_refs_changed_handler(NULL,
        NULL, NULL));
    g_assert(!gbinder_local_object_add_strong_refs_changed_handler(NULL,
        NULL, NULL));
    gbinder_local_object_remove_handler(NULL, 0);
    g_assert(gbinder_local_object_can_handle_transaction(NULL, NULL, 0) ==
        GBINDER_LOCAL_TRANSACTION_NOT_SUPPORTED);
    g_assert(!gbinder_local_object_handle_transaction(NULL, NULL, 0, 0, NULL));
    g_assert(!gbinder_local_object_handle_transaction(NULL, NULL, 0, 0,
        &status));
    g_assert(!gbinder_local_object_handle_looper_transaction(NULL, NULL, 0, 0,
        NULL));
    g_assert(!gbinder_local_object_handle_looper_transaction(NULL, NULL, 0, 0,
        &status));
    g_assert(status == (-EBADMSG));
    g_assert(!gbinder_ipc_transact_custom(NULL, NULL, NULL, NULL, NULL));
    gbinder_local_object_handle_increfs(NULL);
    gbinder_local_object_handle_decrefs(NULL);
    gbinder_local_object_handle_acquire(NULL);
    gbinder_local_object_handle_release(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    const char* const ifaces_foo[] = { "foo", NULL };
    const char* const ifaces_bar[] = { "bar", NULL };
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderLocalObject* foo;
    GBinderLocalObject* bar;

    /* ipc is not a local object */
    g_assert(!gbinder_object_registry_get_local(reg, ipc));

    /* Create a new local objects */
    foo = gbinder_local_object_new(ipc, ifaces_foo, NULL, NULL);
    bar = gbinder_local_object_new(ipc, ifaces_bar, NULL, NULL);

    /* But ipc is still not a local object! */
    g_assert(!gbinder_object_registry_get_local(reg, ipc));

    gbinder_ipc_unref(ipc);

    g_assert(foo);
    g_assert(!gbinder_local_object_add_weak_refs_changed_handler(foo,
        NULL, NULL));
    g_assert(!gbinder_local_object_add_strong_refs_changed_handler(foo,
        NULL, NULL));
    gbinder_local_object_remove_handler(foo, 0);
    g_assert(gbinder_local_object_can_handle_transaction(foo,
        base_interface, -1) == GBINDER_LOCAL_TRANSACTION_NOT_SUPPORTED);
    gbinder_local_object_handle_increfs(foo);
    gbinder_local_object_handle_decrefs(foo);
    gbinder_local_object_handle_acquire(foo);
    gbinder_local_object_handle_release(foo);
    gbinder_local_object_unref(foo);

    g_assert(bar);
    g_assert(gbinder_local_object_ref(bar) == bar);
    gbinder_local_object_drop(bar);
    gbinder_local_object_unref(bar);
}

/*==========================================================================*
 * ping
 *==========================================================================*/

static
void
test_ping(
    void)
{
    int status = INT_MAX;
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteRequest* req = gbinder_remote_request_new(reg, prot, 0, 0);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    GBinderLocalReply* reply;
    GBinderOutputData* out_data;
    static const guint8 result[] = { 0x00, 0x00, 0x00, 0x00 };

    g_assert(gbinder_local_object_can_handle_transaction(obj, NULL,
        GBINDER_PING_TRANSACTION) == GBINDER_LOCAL_TRANSACTION_LOOPER);

    /* If can_handle_transaction() returns TRANSACTION_LOOPER then it must be
     * handled by handle_looper_transaction() */
    g_assert(!gbinder_local_object_handle_transaction(obj, req,
        GBINDER_PING_TRANSACTION, 0, &status));
    g_assert(status == (-EBADMSG));
    reply = gbinder_local_object_handle_looper_transaction(obj, req,
        GBINDER_PING_TRANSACTION, 0, &status);
    g_assert(reply);
    g_assert(status == GBINDER_STATUS_OK);

    out_data = gbinder_local_reply_data(reply);
    g_assert(out_data);
    g_assert(out_data->bytes);
    g_assert(out_data->bytes->len == sizeof(result));
    g_assert(!memcmp(out_data->bytes->data, result, sizeof(result)));

    gbinder_ipc_unref(ipc);
    gbinder_local_object_unref(obj);
    gbinder_local_reply_unref(reply);
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * interface
 *==========================================================================*/

static
void
test_interface(
    void)
{
    int status = INT_MAX;
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    const char* const ifaces[] = { "x", NULL };
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteRequest* req = gbinder_remote_request_new(reg, prot, 0, 0);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, ifaces, NULL, NULL);
    GBinderLocalReply* reply;
    GBinderOutputData* out_data;
    static const guint8 result[] = {
        TEST_INT32_BYTES(1),
        TEST_INT16_BYTES('x'), 0x00, 0x00
    };

    g_assert(gbinder_local_object_can_handle_transaction(obj, NULL,
        GBINDER_INTERFACE_TRANSACTION) == GBINDER_LOCAL_TRANSACTION_LOOPER);

    /* If can_handle_transaction() returns TRANSACTION_LOOPER then it must be
     * handled by handle_looper_transaction() */
    g_assert(!gbinder_local_object_handle_transaction(obj, req,
        GBINDER_INTERFACE_TRANSACTION, 0, &status));
    g_assert(status == (-EBADMSG));
    reply = gbinder_local_object_handle_looper_transaction(obj, req,
        GBINDER_INTERFACE_TRANSACTION, 0, &status);
    g_assert(reply);
    g_assert(status == GBINDER_STATUS_OK);

    out_data = gbinder_local_reply_data(reply);
    g_assert(out_data);
    g_assert(out_data->bytes);
    g_assert(out_data->bytes->len == sizeof(result));
    g_assert(!memcmp(out_data->bytes->data, result, sizeof(result)));

    gbinder_ipc_unref(ipc);
    gbinder_local_object_unref(obj);
    gbinder_local_reply_unref(reply);
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * hidl_ping
 *==========================================================================*/

static
void
test_hidl_ping(
    void)
{
    static const guint8 req_data [] = { TEST_BASE_INTERFACE_HEADER_BYTES };
    int status = INT_MAX;
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteRequest* req = gbinder_remote_request_new(reg, prot, 0, 0);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    GBinderLocalReply* reply;
    GBinderOutputData* out_data;
    static const guint8 result[] = { 0x00, 0x00, 0x00, 0x00 };

    gbinder_remote_request_set_data(req, HIDL_PING_TRANSACTION,
        gbinder_buffer_new(ipc->driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), base_interface));
    g_assert(gbinder_local_object_can_handle_transaction(obj, base_interface,
        HIDL_PING_TRANSACTION) == GBINDER_LOCAL_TRANSACTION_LOOPER);

    /* If can_handle_transaction() returns TRANSACTION_LOOPER then it must be
     * handled by handle_looper_transaction() */
    g_assert(!gbinder_local_object_handle_transaction(obj, req,
        HIDL_PING_TRANSACTION, 0, &status));
    g_assert(status == (-EBADMSG));
    reply = gbinder_local_object_handle_looper_transaction(obj, req,
        HIDL_PING_TRANSACTION, 0, &status);
    g_assert(reply);
    g_assert(status == GBINDER_STATUS_OK);

    out_data = gbinder_local_reply_data(reply);
    g_assert(out_data);
    g_assert(out_data->bytes);
    g_assert(out_data->bytes->len == sizeof(result));
    g_assert(!memcmp(out_data->bytes->data, result, sizeof(result)));

    gbinder_ipc_unref(ipc);
    gbinder_local_object_unref(obj);
    gbinder_local_reply_unref(reply);
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * get_descriptor
 *==========================================================================*/

static
void
test_get_descriptor(
    void)
{
    static const guint8 req_data [] = { TEST_BASE_INTERFACE_HEADER_BYTES };
    int status = INT_MAX;
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteRequest* req = gbinder_remote_request_new(reg, prot, 0, 0);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    GBinderLocalReply* reply;

    gbinder_remote_request_set_data(req, HIDL_PING_TRANSACTION,
        gbinder_buffer_new(ipc->driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), base_interface));
    g_assert(gbinder_local_object_can_handle_transaction(obj, base_interface,
        HIDL_GET_DESCRIPTOR_TRANSACTION) == GBINDER_LOCAL_TRANSACTION_LOOPER);

    /* If can_handle_transaction() returns TRANSACTION_LOOPER then it must be
     * handled by handle_looper_transaction() */
    g_assert(!gbinder_local_object_handle_transaction(obj, req,
        HIDL_GET_DESCRIPTOR_TRANSACTION, 0, &status));
    g_assert(status == (-EBADMSG));
    reply = gbinder_local_object_handle_looper_transaction(obj, req,
        HIDL_GET_DESCRIPTOR_TRANSACTION, 0, &status);
    g_assert(reply);
    g_assert(status == GBINDER_STATUS_OK);

    /* Unsupported transaction */
    g_assert(!gbinder_local_object_handle_looper_transaction
        (obj, req, -1, 0, NULL));
    g_assert(!gbinder_local_object_handle_looper_transaction
        (obj, req, -1, 0, &status));
    g_assert(status == (-EBADMSG));
    g_assert(!gbinder_local_object_handle_transaction(obj, req, -1, 0, NULL));
    g_assert(!gbinder_local_object_handle_transaction(obj, req, -1, 0,
        &status));
    g_assert(status == (-EBADMSG));

    gbinder_ipc_unref(ipc);
    gbinder_local_object_unref(obj);
    gbinder_local_reply_unref(reply);
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * descriptor_chain
 *==========================================================================*/

static
void
test_descriptor_chain(
    void)
{
    static const guint8 req_data [] = {
        TEST_BASE_INTERFACE_HEADER_BYTES
    };
    int status = INT_MAX;
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    const char* const ifaces[] = { "android.hidl.base@1.0::IBase", NULL };
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteRequest* req = gbinder_remote_request_new(reg, prot, 0, 0);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, ifaces, NULL, NULL);
    GBinderLocalReply* reply;
    GBinderOutputData* reply_data;

    gbinder_remote_request_set_data(req, HIDL_DESCRIPTOR_CHAIN_TRANSACTION,
        gbinder_buffer_new(ipc->driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), base_interface));
    g_assert(gbinder_local_object_can_handle_transaction(obj, base_interface,
        HIDL_DESCRIPTOR_CHAIN_TRANSACTION) == GBINDER_LOCAL_TRANSACTION_LOOPER);

    /* If can_handle_transaction() returns TRANSACTION_LOOPER then it must be
     * handled by handle_looper_transaction() */
    g_assert(!gbinder_local_object_handle_transaction(obj, req,
        HIDL_DESCRIPTOR_CHAIN_TRANSACTION, 0, &status));
    g_assert(status == (-EBADMSG));
    reply = gbinder_local_object_handle_looper_transaction(obj, req,
        HIDL_DESCRIPTOR_CHAIN_TRANSACTION, 0, &status);
    g_assert(reply);
    g_assert(status == GBINDER_STATUS_OK);

    /* Should get 3 buffers - vector, string and its contents */
    reply_data = gbinder_local_reply_data(reply);
    g_assert(gbinder_output_data_offsets(reply_data)->count == 3);
    g_assert(gbinder_output_data_buffers_size(reply_data) == 64);

    gbinder_ipc_unref(ipc);
    gbinder_local_object_unref(obj);
    gbinder_local_reply_unref(reply);
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * custom_call
 *==========================================================================*/

#define CUSTOM_TRANSACTION (GBINDER_FIRST_CALL_TRANSACTION + 1)
#define CUSTOM_INTERFACE_BYTES 'f', 'o', 'o'
#define CUSTOM_INTERFACE_HEADER_BYTES  CUSTOM_INTERFACE_BYTES, 0x00
static const char custom_iface[] = { CUSTOM_INTERFACE_BYTES, 0x00 };

static
GBinderLocalReply*
test_custom_iface_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    int* count = user_data;

    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), custom_iface));
    g_assert(code == CUSTOM_TRANSACTION);
    *status = GBINDER_STATUS_OK;
    (*count)++;
    return gbinder_local_object_new_reply(obj);
}

static
void
test_custom_iface(
    void)
{
    static const guint8 req_data [] = { CUSTOM_INTERFACE_HEADER_BYTES };
    const char* const ifaces[] = { custom_iface, NULL };
    int count = 0, status = INT_MAX;
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteRequest* req = gbinder_remote_request_new(reg, prot, 0, 0);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, ifaces,
        test_custom_iface_handler, &count);
    GBinderLocalReply* reply;
    GBinderReaderData reader_data;
    GBinderReader reader;
    char** strv;
    char* str;

    gbinder_remote_request_set_data(req, HIDL_PING_TRANSACTION,
        gbinder_buffer_new(ipc->driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));
    g_assert(gbinder_local_object_can_handle_transaction(obj, base_interface,
        HIDL_DESCRIPTOR_CHAIN_TRANSACTION) == GBINDER_LOCAL_TRANSACTION_LOOPER);
    g_assert(gbinder_local_object_can_handle_transaction(obj, custom_iface,
        HIDL_DESCRIPTOR_CHAIN_TRANSACTION) ==
        GBINDER_LOCAL_TRANSACTION_SUPPORTED);
    g_assert(gbinder_local_object_can_handle_transaction(obj, custom_iface,
        CUSTOM_TRANSACTION) == GBINDER_LOCAL_TRANSACTION_SUPPORTED);

    /* This returns the custom interface */
    reply = gbinder_local_object_handle_looper_transaction(obj, req,
        HIDL_GET_DESCRIPTOR_TRANSACTION, 0, &status);
    g_assert(reply);
    g_assert(status == GBINDER_STATUS_OK);

    /* Parse the reply and check the interface */
    test_reader_data_init_for_reply(&reader_data, obj, reply);
    gbinder_reader_init(&reader, &reader_data, 0, reader_data.buffer->size);
    g_assert(gbinder_reader_read_int32(&reader, &status));
    g_assert(status == GBINDER_STATUS_OK);
    str = gbinder_reader_read_hidl_string(&reader);
    g_assert(!g_strcmp0(str, custom_iface));
    g_free(str);
    test_reader_data_cleanup(&reader_data);
    gbinder_local_reply_unref(reply);

    /* And this returns two interfaces */
    reply = gbinder_local_object_handle_looper_transaction(obj, req,
        HIDL_DESCRIPTOR_CHAIN_TRANSACTION, 0, &status);
    g_assert(reply);
    g_assert(status == GBINDER_STATUS_OK);

    /* Parse the reply and check the interface */
    test_reader_data_init_for_reply(&reader_data, obj, reply);
    gbinder_reader_init(&reader, &reader_data, 0, reader_data.buffer->size);
    g_assert(gbinder_reader_read_int32(&reader, &status));
    g_assert(status == GBINDER_STATUS_OK);
    strv = gbinder_reader_read_hidl_string_vec(&reader);
    g_assert(gutil_strv_length(strv) == 2);
    g_assert(!g_strcmp0(strv[0], custom_iface));
    g_assert(!g_strcmp0(strv[1], base_interface));
    g_strfreev(strv);
    test_reader_data_cleanup(&reader_data);
    gbinder_local_reply_unref(reply);

    /* Execute the custom transaction */
    reply = gbinder_local_object_handle_transaction(obj, req,
        CUSTOM_TRANSACTION, 0, &status);
    g_assert(reply);
    g_assert(status == GBINDER_STATUS_OK);
    g_assert(count == 1);

    gbinder_ipc_unref(ipc);
    gbinder_local_object_unref(obj);
    gbinder_local_reply_unref(reply);
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * reply_status
 *==========================================================================*/

#define EXPECTED_STATUS (424242)

static
GBinderLocalReply*
test_reply_status_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    int* count = user_data;

    g_assert(!flags);
    g_assert(!g_strcmp0(gbinder_remote_request_interface(req), custom_iface));
    g_assert(code == CUSTOM_TRANSACTION);
    *status = EXPECTED_STATUS;
    (*count)++;
    return NULL;
}

static
void
test_reply_status(
    void)
{
    static const guint8 req_data [] = { CUSTOM_INTERFACE_HEADER_BYTES };
    const char* const ifaces[] = { custom_iface, NULL };
    int count = 0, status = 0;
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    const GBinderRpcProtocol* prot = gbinder_rpc_protocol_for_device(dev);
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteRequest* req = gbinder_remote_request_new(reg, prot, 0, 0);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, ifaces,
        test_reply_status_handler, &count);

    gbinder_remote_request_set_data(req, HIDL_PING_TRANSACTION,
        gbinder_buffer_new(ipc->driver, g_memdup(req_data, sizeof(req_data)),
        sizeof(req_data), NULL));

    /* Execute the custom transaction */
    g_assert(!gbinder_local_object_handle_transaction(obj, req,
        CUSTOM_TRANSACTION, 0, &status));
    g_assert(status == EXPECTED_STATUS);
    g_assert(count == 1);

    gbinder_ipc_unref(ipc);
    gbinder_local_object_unref(obj);
    gbinder_remote_request_unref(req);
}

/*==========================================================================*
 * increfs
 *==========================================================================*/

static
void
test_increfs_cb(
    GBinderLocalObject* obj,
    void* user_data)
{
    GVERBOSE_("%d", obj->weak_refs);
    g_assert(obj->weak_refs == 1);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_increfs(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, NULL, NULL, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd = gbinder_driver_fd(ipc->driver);
    gulong id = gbinder_local_object_add_weak_refs_changed_handler(obj,
        test_increfs_cb, loop);

    /* ipc is not an object, will be ignored */
    test_binder_br_increfs(fd, ipc);
    test_binder_br_increfs(fd, obj);
    test_binder_set_looper_enabled(fd, TRUE);
    test_run(&test_opt, loop);

    g_assert(obj->weak_refs == 1);
    gbinder_local_object_remove_handler(obj, id);
    gbinder_local_object_unref(obj);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * decrefs
 *==========================================================================*/

static
void
test_decrefs_cb(
    GBinderLocalObject* obj,
    void* user_data)
{
    GVERBOSE_("%d", obj->weak_refs);
    if (!obj->weak_refs) {
        test_quit_later((GMainLoop*)user_data);
    }
}

static
void
test_decrefs(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, NULL, NULL, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd = gbinder_driver_fd(ipc->driver);
    gulong id = gbinder_local_object_add_weak_refs_changed_handler(obj,
        test_decrefs_cb, loop);

    /* ipc is not an object, will be ignored */
    test_binder_br_decrefs(fd, ipc);
    test_binder_br_increfs(fd, obj);
    test_binder_br_decrefs(fd, obj);
    test_binder_set_looper_enabled(fd, TRUE);
    test_run(&test_opt, loop);

    g_assert(obj->weak_refs == 0);
    gbinder_local_object_remove_handler(obj, id);
    gbinder_local_object_unref(obj);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * acquire
 *==========================================================================*/

static
void
test_acquire_cb(
    GBinderLocalObject* obj,
    void* user_data)
{
    GVERBOSE_("%d", obj->strong_refs);
    g_assert(obj->strong_refs == 1);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_acquire(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderLocalObject* obj = gbinder_local_object_new
        (ipc, NULL, NULL, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd = gbinder_driver_fd(ipc->driver);
    gulong id = gbinder_local_object_add_strong_refs_changed_handler(obj,
        test_acquire_cb, loop);

    /* ipc is not an object, will be ignored */
    test_binder_br_acquire(fd, ipc);
    test_binder_br_acquire(fd, obj);
    test_binder_set_looper_enabled(fd, TRUE);
    test_run(&test_opt, loop);

    g_assert(obj->strong_refs == 1);
    gbinder_local_object_remove_handler(obj, id);
    gbinder_local_object_unref(obj);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * release
 *==========================================================================*/

static
void
test_release_cb(
    GBinderLocalObject* obj,
    void* user_data)
{
    GVERBOSE_("%d", obj->strong_refs);
    if (!obj->strong_refs) {
        test_quit_later((GMainLoop*)user_data);
    }
}

static
void
test_release(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    int fd = gbinder_driver_fd(ipc->driver);
    gulong id = gbinder_local_object_add_strong_refs_changed_handler(obj,
        test_release_cb, loop);

    /* ipc is not an object, will be ignored */
    test_binder_br_release(fd, ipc);
    test_binder_br_acquire(fd, obj);
    test_binder_br_release(fd, obj);
    test_binder_set_looper_enabled(fd, TRUE);
    test_run(&test_opt, loop);

    g_assert(obj->strong_refs == 0);
    gbinder_local_object_remove_handler(obj, id);
    gbinder_local_object_unref(obj);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/local_object/"

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "null", test_null);
    g_test_add_func(TEST_PREFIX "basic", test_basic);
    g_test_add_func(TEST_PREFIX "ping", test_ping);
    g_test_add_func(TEST_PREFIX "interface", test_interface);
    g_test_add_func(TEST_PREFIX "hidl_ping", test_hidl_ping);
    g_test_add_func(TEST_PREFIX "get_descriptor", test_get_descriptor);
    g_test_add_func(TEST_PREFIX "descriptor_chain", test_descriptor_chain);
    g_test_add_func(TEST_PREFIX "custom_iface", test_custom_iface);
    g_test_add_func(TEST_PREFIX "reply_status", test_reply_status);
    g_test_add_func(TEST_PREFIX "increfs", test_increfs);
    g_test_add_func(TEST_PREFIX "decrefs", test_decrefs);
    g_test_add_func(TEST_PREFIX "acquire", test_acquire);
    g_test_add_func(TEST_PREFIX "release", test_release);
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
