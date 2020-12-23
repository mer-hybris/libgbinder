/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_driver.h"
#include "gbinder_config.h"
#include "gbinder_ipc.h"
#include "gbinder_reader.h"
#include "gbinder_servicemanager_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_local_object.h"
#include "gbinder_local_reply.h"
#include "gbinder_remote_request.h"
#include "gbinder_remote_object.h"

#include <gutil_strv.h>
#include <gutil_macros.h>
#include <gutil_log.h>

#include <errno.h>

static TestOpt test_opt;

GType
gbinder_servicemanager_hidl_get_type()
{
    /* Avoid pulling in gbinder_servicemanager_hidl object */
    return 0;
}

GType
gbinder_servicemanager_aidl2_get_type()
{
    /* Avoid pulling in gbinder_servicemanager_aidl2 object */
    return 0;
}

/*==========================================================================*
 * Test service manager
 *==========================================================================*/

#define SVCMGR_HANDLE (0)
static const char SVCMGR[] = "android.os.IServiceManager";
enum servicemanager_aidl_tx {
    GET_SERVICE_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    CHECK_SERVICE_TRANSACTION,
    ADD_SERVICE_TRANSACTION,
    LIST_SERVICES_TRANSACTION
};

const char* const servicemanager_aidl_ifaces[] = { SVCMGR, NULL };

typedef struct test_service_manager {
    GBinderLocalObject* obj;
    GHashTable* objects;
} TestServiceManager;

static
GBinderLocalReply*
servicemanager_aidl_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestServiceManager* self = user_data;
    GBinderLocalReply* reply = NULL;
    GBinderReader reader;
    GBinderRemoteObject* remote_obj;
    guint32 num;
    char* str;

    g_assert(!flags);
    g_assert_cmpstr(gbinder_remote_request_interface(req), == ,SVCMGR);
    *status = -1;
    switch (code) {
    case GET_SERVICE_TRANSACTION:
    case CHECK_SERVICE_TRANSACTION:
        gbinder_remote_request_init_reader(req, &reader);
        str = gbinder_reader_read_string16(&reader);
        if (str) {
            reply = gbinder_local_object_new_reply(obj);
            remote_obj = g_hash_table_lookup(self->objects, str);
            if (remote_obj) {
                GDEBUG("Found name '%s' => %p", str, remote_obj);
                gbinder_local_reply_append_remote_object(reply, remote_obj);
            } else {
                GDEBUG("Name '%s' not found", str);
                gbinder_local_reply_append_int32(reply, GBINDER_STATUS_OK);
            }
            g_free(str);
        }
        break;
    case ADD_SERVICE_TRANSACTION:
        gbinder_remote_request_init_reader(req, &reader);
        str = gbinder_reader_read_string16(&reader);
        remote_obj = gbinder_reader_read_object(&reader);
        if (str && remote_obj && gbinder_reader_read_uint32(&reader, &num)) {
            GDEBUG("Adding '%s'", str);
            g_hash_table_replace(self->objects, str, remote_obj);
            remote_obj = NULL;
            str = NULL;
            reply = gbinder_local_object_new_reply(obj);
            *status = GBINDER_STATUS_OK;
        }
        g_free(str);
        gbinder_remote_object_unref(remote_obj);
        break;
    case LIST_SERVICES_TRANSACTION:
        if (gbinder_remote_request_read_uint32(req, &num)) {
            if (num < g_hash_table_size(self->objects)) {
                GList* keys = g_hash_table_get_keys(self->objects);
                GList* l = g_list_nth(keys, num);

                reply = gbinder_local_object_new_reply(obj);
                gbinder_local_reply_append_string16(reply, l->data);
                g_list_free(keys);
                *status = GBINDER_STATUS_OK;
            } else {
                GDEBUG("Index %u out of bounds", num);
            }
        }
        break;
    default:
        GDEBUG("Unhandled command %u", code);
        break;
    }
    return reply;
}

static
TestServiceManager*
servicemanager_aidl_new(
    const char* dev)
{
    TestServiceManager* self = g_new0(TestServiceManager, 1);
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    const int fd = gbinder_driver_fd(ipc->driver);

    self->objects = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) gbinder_remote_object_unref);
    self->obj = gbinder_local_object_new(ipc, servicemanager_aidl_ifaces,
        servicemanager_aidl_handler, self);
    test_binder_set_looper_enabled(fd, TRUE);
    test_binder_register_object(fd, self->obj, SVCMGR_HANDLE);
    gbinder_ipc_unref(ipc);
    return self;
}

static
void
servicemanager_aidl_free(
    TestServiceManager* self)
{
    gbinder_local_object_drop(self->obj);
    g_hash_table_destroy(self->objects);
    g_free(self);
}

/*==========================================================================*
 * get
 *==========================================================================*/

static
void
test_add_cb(
    GBinderServiceManager* sm,
    int status,
    void* user_data)
{
    g_assert(status == GBINDER_STATUS_OK);
    if (user_data) {
        g_main_loop_quit(user_data);
    }
}

static
void
test_get_none_cb(
    GBinderServiceManager* sm,
    GBinderRemoteObject* obj,
    int status,
    void* user_data)
{
    g_assert(!obj);
    g_assert(status == GBINDER_STATUS_OK);
    g_main_loop_quit(user_data);
}

static
void
test_get_cb(
    GBinderServiceManager* sm,
    GBinderRemoteObject* obj,
    int status,
    void* user_data)
{
    g_assert(obj);
    g_assert(status == GBINDER_STATUS_OK);
    g_main_loop_quit(user_data);
}

static
void
test_get()
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    const char* other_dev = GBINDER_DEFAULT_BINDER "-private";
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    TestServiceManager* smsvc = servicemanager_aidl_new(other_dev);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* name = "name";
    GBinderServiceManager* sm;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
    test_binder_set_passthrough(fd, TRUE);
    sm = gbinder_servicemanager_new(dev);

    /* Query the object (it's not there yet) and wait for completion */
    GDEBUG("Querying '%s'", name);
    g_assert(gbinder_servicemanager_get_service(sm, name, test_get_none_cb,
        loop));
    test_run(&test_opt, loop);

    /* Register object and wait for completion */
    GDEBUG("Registering object '%s' => %p", name, obj);
    g_assert(gbinder_servicemanager_add_service(sm, name, obj,
        test_add_cb, loop));
    test_run(&test_opt, loop);

    g_assert_cmpuint(g_hash_table_size(smsvc->objects), == ,1);
    g_assert(g_hash_table_contains(smsvc->objects, name));

    /* Query the object (it should be there not) and wait for completion */
    GDEBUG("Querying '%s' again", name);
    g_assert(gbinder_servicemanager_get_service(sm, name, test_get_cb, loop));
    test_run(&test_opt, loop);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(obj);
    servicemanager_aidl_free(smsvc);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    test_binder_exit_wait();
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * list
 *==========================================================================*/

typedef struct test_list {
    char** list;
    GMainLoop* loop;
} TestList;

static
gboolean
test_list_cb(
    GBinderServiceManager* sm,
    char** services,
    void* user_data)
{
    TestList* test = user_data;

    GDEBUG("Got %u name(s)", gutil_strv_length(services));
    g_strfreev(test->list);
    test->list = services;
    g_main_loop_quit(test->loop);
    return TRUE;
}

static
void
test_list()
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    const char* other_dev = GBINDER_DEFAULT_BINDER "-private";
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    TestServiceManager* smsvc = servicemanager_aidl_new(other_dev);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* name = "name";
    GBinderServiceManager* sm;
    TestList test;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
    test_binder_set_passthrough(fd, TRUE);
    sm = gbinder_servicemanager_new(dev);

    /* Request the list and wait for completion */
    g_assert(gbinder_servicemanager_list(sm, test_list_cb, &test));
    test_run(&test_opt, test.loop);

    /* There's nothing there yet */
    g_assert(test.list);
    g_assert(!test.list[0]);

    /* Register object and wait for completion */
    g_assert(gbinder_servicemanager_add_service(sm, name, obj,
        test_add_cb, test.loop));
    test_run(&test_opt, test.loop);

    /* Request the list again */
    g_assert(gbinder_servicemanager_list(sm, test_list_cb, &test));
    test_run(&test_opt, test.loop);

    /* Now the name must be there */
    g_assert_cmpuint(gutil_strv_length(test.list), == ,1);
    g_assert_cmpstr(test.list[0], == ,name);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(obj);
    servicemanager_aidl_free(smsvc);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    test_binder_exit_wait();

    g_strfreev(test.list);
    g_main_loop_unref(test.loop);
}

/*==========================================================================*
 * notify
 *==========================================================================*/

static
void
test_notify_cb(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data)
{
    g_assert(name);
    GDEBUG("'%s' is registered", name);
    g_main_loop_quit(user_data);
}

static
void
test_notify()
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    const char* other_dev = GBINDER_DEFAULT_BINDER "-private";
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    TestServiceManager* smsvc = servicemanager_aidl_new(other_dev);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* name = "name";
    GBinderServiceManager* sm;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id;

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
    test_binder_set_passthrough(fd, TRUE);
    sm = gbinder_servicemanager_new(dev);
    gbinder_ipc_set_max_threads(ipc, 1);

    /* Start watching */
    id = gbinder_servicemanager_add_registration_handler(sm, name,
        test_notify_cb, loop);
    g_assert(id);

    /* Register the object and wait for completion */
    GDEBUG("Registering object '%s' => %p", name, obj);
    g_assert(gbinder_servicemanager_add_service(sm, name, obj,
        test_add_cb, NULL));

    /* test_notify_cb will stop the loop */
    test_run(&test_opt, loop);
    gbinder_servicemanager_remove_handler(sm, id);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(obj);
    servicemanager_aidl_free(smsvc);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    gbinder_ipc_exit();
    test_binder_exit_wait();
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/servicemanager_aidl/" t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("get"), test_get);
    g_test_add_func(TEST_("list"), test_list);
    g_test_add_func(TEST_("notify"), test_notify);
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
