/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
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
#include "gbinder_local_object_p.h"
#include "gbinder_local_reply.h"
#include "gbinder_remote_request.h"
#include "gbinder_remote_object.h"

#include <gutil_strv.h>
#include <gutil_log.h>

static TestOpt test_opt;
static const char TMP_DIR_TEMPLATE[] =
    "gbinder-test-servicemanager_aidl2-XXXXXX";

GType
gbinder_servicemanager_hidl_get_type()
{
    /* Avoid pulling in gbinder_servicemanager_hidl object */
    return 0;
}

/*==========================================================================*
 * Test service manager
 *==========================================================================*/

#define SVCMGR_HANDLE (0)
static const char SVCMGR_IFACE[] = "android.os.IServiceManager";
enum servicemanager_aidl_tx {
    GET_SERVICE_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    CHECK_SERVICE_TRANSACTION,
    ADD_SERVICE_TRANSACTION,
    LIST_SERVICES_TRANSACTION
};

const char* const servicemanager_aidl_ifaces[] = { SVCMGR_IFACE, NULL };

typedef GBinderLocalObjectClass ServiceManagerAidl2Class;
typedef struct service_manager_aidl2 {
    GBinderLocalObject parent;
    GHashTable* objects;
    gboolean handle_on_looper_thread;
} ServiceManagerAidl2;

#define SERVICE_MANAGER_AIDL2_TYPE (service_manager_aidl2_get_type())
#define SERVICE_MANAGER_AIDL2(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        SERVICE_MANAGER_AIDL2_TYPE, ServiceManagerAidl2))
G_DEFINE_TYPE(ServiceManagerAidl2, service_manager_aidl2, \
        GBINDER_TYPE_LOCAL_OBJECT)

static
GBinderLocalReply*
servicemanager_aidl2_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    ServiceManagerAidl2* self = user_data;
    GBinderLocalReply* reply = NULL;
    GBinderReader reader;
    GBinderRemoteObject* remote_obj;
    guint32 num, allow_isolated, dumpsys_priority;
    char* str;

    g_assert(!flags);
    GDEBUG("%s %u", gbinder_remote_request_interface(req), code);
    g_assert_cmpstr(gbinder_remote_request_interface(req), == ,SVCMGR_IFACE);
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
        if (str && remote_obj &&
            gbinder_reader_read_uint32(&reader, &allow_isolated) &&
            gbinder_reader_read_uint32(&reader, &dumpsys_priority)) {
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
        gbinder_remote_request_init_reader(req, &reader);
        if (gbinder_reader_read_uint32(&reader, &num) &&
            gbinder_reader_read_uint32(&reader, &dumpsys_priority)) {
            if (num < g_hash_table_size(self->objects)) {
                GList* keys = g_hash_table_get_keys(self->objects);
                GList* l = g_list_nth(keys, num);

                /* Ignore dumpsys_priority */
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
ServiceManagerAidl2*
servicemanager_aidl2_new(
    const char* dev,
    gboolean handle_on_looper_thread)
{
    ServiceManagerAidl2* self = g_object_new(SERVICE_MANAGER_AIDL2_TYPE, NULL);
    GBinderLocalObject* obj = GBINDER_LOCAL_OBJECT(self);
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    const int fd = gbinder_driver_fd(ipc->driver);

    self->handle_on_looper_thread = handle_on_looper_thread;
    gbinder_local_object_init_base(obj, ipc, servicemanager_aidl_ifaces,
        servicemanager_aidl2_handler, self);
    test_binder_set_looper_enabled(fd, TEST_LOOPER_ENABLE);
    test_binder_register_object(fd, obj, SVCMGR_HANDLE);
    gbinder_ipc_register_local_object(ipc, obj);
    gbinder_ipc_unref(ipc);
    return self;
}

static
GBINDER_LOCAL_TRANSACTION_SUPPORT
service_manager_aidl2_can_handle_transaction(
    GBinderLocalObject* object,
    const char* iface,
    guint code)
{
    ServiceManagerAidl2* self = SERVICE_MANAGER_AIDL2(object);

    if (self->handle_on_looper_thread && !g_strcmp0(SVCMGR_IFACE, iface)) {
        return GBINDER_LOCAL_TRANSACTION_LOOPER;
    } else {
        return GBINDER_LOCAL_OBJECT_CLASS(service_manager_aidl2_parent_class)->
            can_handle_transaction(object, iface, code);
    }
}

static
GBinderLocalReply*
service_manager_aidl2_handle_looper_transaction(
    GBinderLocalObject* object,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
{
    if (!g_strcmp0(gbinder_remote_request_interface(req), SVCMGR_IFACE)) {
        return GBINDER_LOCAL_OBJECT_CLASS(service_manager_aidl2_parent_class)->
            handle_transaction(object, req, code, flags, status);
    } else {
        return GBINDER_LOCAL_OBJECT_CLASS(service_manager_aidl2_parent_class)->
            handle_looper_transaction(object, req, code, flags, status);
    }
}

static
void
service_manager_aidl2_finalize(
    GObject* object)
{
    ServiceManagerAidl2* self = SERVICE_MANAGER_AIDL2(object);

    g_hash_table_destroy(self->objects);
    G_OBJECT_CLASS(service_manager_aidl2_parent_class)->finalize(object);
}

static
void
service_manager_aidl2_init(
    ServiceManagerAidl2* self)
{
    self->objects = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) gbinder_remote_object_unref);
}

static
void
service_manager_aidl2_class_init(
    ServiceManagerAidl2Class* klass)
{
    GObjectClass* object = G_OBJECT_CLASS(klass);
    GBinderLocalObjectClass* local_object = GBINDER_LOCAL_OBJECT_CLASS(klass);

    object->finalize = service_manager_aidl2_finalize;
    local_object->can_handle_transaction =
        service_manager_aidl2_can_handle_transaction;
    local_object->handle_looper_transaction =
        service_manager_aidl2_handle_looper_transaction;
}

/*==========================================================================*
 * Test context
 *==========================================================================*/

typedef struct test_context {
    const char* default_config_dir;
    const char* default_config_file;
    char* config_dir;
    char* config_subdir;
    char* config_file;
    GBinderLocalObject* object;
    ServiceManagerAidl2* service;
    GBinderServiceManager* client;
    int fd;
} TestContext;

static
void
test_context_init(
    TestContext* test)
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    const char* other_dev = GBINDER_DEFAULT_BINDER "-private";
    /*
     * Also set defaults so that both /dev/binder and /dev/binder-private
     * use the same protocol.
     */
    const char* config =
        "[Protocol]\n"
        "Default = aidl2\n"
        "/dev/binder = aidl2\n"
        "[ServiceManager]\n"
        "Default = aidl2\n"
        "/dev/binder = aidl2\n";
    GBinderIpc* ipc;

    memset(test, 0, sizeof(*test));
    test->default_config_dir = gbinder_config_dir;
    test->default_config_file = gbinder_config_file;
    test->config_dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    test->config_subdir = g_build_filename(test->config_dir, "d", NULL);
    test->config_file = g_build_filename(test->config_dir, "test.conf", NULL);
    g_assert(g_file_set_contents(test->config_file, config, -1, NULL));
    GDEBUG("Config file %s", test->config_file);
    gbinder_config_dir = test->config_subdir; /* Doesn't exist */
    gbinder_config_file = test->config_file;

    ipc = gbinder_ipc_new(dev);
    test->fd = gbinder_driver_fd(ipc->driver);
    test->object = gbinder_local_object_new(ipc, NULL, NULL, NULL);

    /* Set up binder simulator */
    test_binder_register_object(test->fd, test->object, AUTO_HANDLE);
    test_binder_set_passthrough(test->fd, TRUE);

    test->service = servicemanager_aidl2_new(other_dev, TRUE);
    test->client = gbinder_servicemanager_new(dev);
    gbinder_ipc_unref(ipc);
}

static
void
test_context_deinit(
    TestContext* test)
{
    test_binder_unregister_objects(test->fd);
    gbinder_local_object_unref(test->object);
    gbinder_local_object_drop(GBINDER_LOCAL_OBJECT(test->service));
    gbinder_servicemanager_unref(test->client);
    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, NULL);
    remove(test->config_file);
    remove(test->config_dir);
    g_free(test->config_file);
    g_free(test->config_subdir);
    g_free(test->config_dir);
    gbinder_config_dir = test->default_config_dir;
    gbinder_config_file = test->default_config_file;
    gbinder_config_exit();
}

/*==========================================================================*
 * get
 *==========================================================================*/

static
void
test_get_run()
{
    TestContext test;
    const char* name = "name";
    int status = -1;

    test_context_init(&test);

    /* Query the object (it's not there yet) */
    GDEBUG("Querying '%s'", name);
    g_assert(!gbinder_servicemanager_get_service_sync(test.client,
        name, &status));
    g_assert_cmpint(status, == ,GBINDER_STATUS_OK);

    /* Register object */
    GDEBUG("Registering object '%s' => %p", name, test.object);
    g_assert_cmpint(gbinder_servicemanager_add_service_sync(test.client,
        name, test.object), == ,GBINDER_STATUS_OK);

    g_assert_cmpuint(g_hash_table_size(test.service->objects), == ,1);
    g_assert(g_hash_table_contains(test.service->objects, name));

    /* Query the object (this time it must be there) */
    GDEBUG("Querying '%s' again", name);
    g_assert(gbinder_servicemanager_get_service_sync(test.client, name,
        &status));
    g_assert_cmpint(status, == ,GBINDER_STATUS_OK);

    test_context_deinit(&test);
}

static
void
test_get()
{
    test_run_in_context(&test_opt, test_get_run);
}

/*==========================================================================*
 * list
 *==========================================================================*/

static
void
test_list_run()
{
    TestContext test;
    const char* name = "name";
    char** list;

    test_context_init(&test);

    /* Request the list */
    list = gbinder_servicemanager_list_sync(test.client);

    /* There's nothing there yet */
    g_assert(list);
    g_assert(!list[0]);
    g_strfreev(list);

    /* Register object */
    GDEBUG("Registering object '%s' => %p", name, test.object);
    g_assert_cmpint(gbinder_servicemanager_add_service_sync(test.client,
        name, test.object), == ,GBINDER_STATUS_OK);

    /* Request the list again */
    list = gbinder_servicemanager_list_sync(test.client);

    /* Now the name must be there */
    g_assert_cmpuint(gutil_strv_length(list), == ,1);
    g_assert_cmpstr(list[0], == ,name);
    g_strfreev(list);

    test_context_deinit(&test);
}

static
void
test_list()
{
    test_run_in_context(&test_opt, test_list_run);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/servicemanager_aidl2/" t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("get"), test_get);
    g_test_add_func(TEST_("list"), test_list);
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
