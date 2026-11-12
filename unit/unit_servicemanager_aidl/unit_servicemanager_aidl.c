/*
 * Copyright (C) 2020-2022 Jolla Ltd.
 * Copyright (C) 2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2026 Jolla Mobile Ltd
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
#include "gbinder_reader.h"
#include "gbinder_writer.h"
#include "gbinder_servicemanager_p.h"
#include "gbinder_local_object_p.h"
#include "gbinder_local_reply.h"
#include "gbinder_local_request.h"
#include "gbinder_remote_request.h"
#include "gbinder_remote_object_p.h"

#include <gutil_strv.h>
#include <gutil_log.h>

static TestOpt test_opt;
static const char TMP_DIR_TEMPLATE[] =
    "gbinder-test-servicemanager_aidl-XXXXXX";

GType
gbinder_servicemanager_hidl_get_type(
    void)
{
    /* Dummy function to avoid pulling in gbinder_servicemanager_hidl */
    g_assert_not_reached();
    return 0;
}

GType
gbinder_servicemanager_aidl2_get_type(
    void)
{
    /* Dummy function to avoid pulling in gbinder_servicemanager_aidl2 */
    g_assert_not_reached();
    return 0;
}

GType
gbinder_servicemanager_aidl3_get_type(
    void)
{
    /* Dummy function to avoid pulling in gbinder_servicemanager_aidl3 */
    g_assert_not_reached();
    return 0;
}

GType
gbinder_servicemanager_aidl5_get_type(
    void)
{
    /* Dummy function to avoid pulling in gbinder_servicemanager_aidl5 */
    g_assert_not_reached();
    return 0;
}

GType
gbinder_servicemanager_aidl6_get_type(
    void)
{
    /* Dummy function to avoid pulling in gbinder_servicemanager_aidl6 */
    g_assert_not_reached();
    return 0;
}

/*==========================================================================*
 * Test service manager
 *==========================================================================*/

#define SVCMGR_HANDLE (0)
static const char SVCMGR_IFACE[] = "android.os.IServiceManager";
static const char CALLBACK_IFACE[] =  "android.os.IServiceCallback";

enum servicemanager_aidl_tx {
    GET_SERVICE_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    CHECK_SERVICE_TRANSACTION,
    ADD_SERVICE_TRANSACTION,
    LIST_SERVICES_TRANSACTION,
    REGISTER_FOR_NOTIFICATIONS_TRANSACTION,
    UNREGISTER_FOR_NOTIFICATIONS_TRANSACTION
};

enum servicemanager_aidl_callbacl_tx {
    ON_REGISTRATION_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION
};

const char* const servicemanager_aidl_ifaces[] = { SVCMGR_IFACE, NULL };

typedef GBinderLocalObjectClass ServiceManagerAidlClass;
typedef struct service_manager_aidl {
    GBinderLocalObject parent;
    gboolean notify;
    GHashTable* objects;
    GPtrArray* watches;
    GMutex mutex;
} ServiceManagerAidl;

typedef struct service_manager_aidl_watch {
    char* name;
    GBinderClient* watcher;
} ServiceManagerAidlWatch;

#define SERVICE_MANAGER_AIDL_TYPE (service_manager_aidl_get_type())
#define SERVICE_MANAGER_AIDL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        SERVICE_MANAGER_AIDL_TYPE, ServiceManagerAidl))
G_DEFINE_TYPE(ServiceManagerAidl, service_manager_aidl, \
        GBINDER_TYPE_LOCAL_OBJECT)

static
void
test_servicemanager_aidl_watch_destroy(
    gpointer data)
{
    ServiceManagerAidlWatch* watch = data;

    gbinder_client_unref(watch->watcher);
    g_free(watch->name);
    g_free(watch);
}

static
void
test_servicemanager_aidl_watch_notify(
    const ServiceManagerAidlWatch* watch,
    GBinderRemoteObject* obj)
{
    GBinderLocalRequest* notify = gbinder_client_new_request(watch->watcher);
    GBinderWriter writer;

    /*
     * IServiceCallback.aidl:
     * void onRegistration(@utf8InCpp String name, IBinder binder);
     */
    gbinder_local_request_init_writer(notify, &writer);
    gbinder_writer_append_string16(&writer, watch->name);
    gbinder_writer_append_remote_object(&writer, obj);
    gbinder_client_transact(watch->watcher, ON_REGISTRATION_TRANSACTION,
        GBINDER_TX_FLAG_ONEWAY, notify, NULL, NULL, NULL);
    gbinder_local_request_unref(notify);
}

static
GBinderLocalReply*
servicemanager_aidl_handler(
    ServiceManagerAidl* self,
    GBinderRemoteRequest* req,
    guint code,
    int* status)
{
    GBinderLocalObject* obj = &self->parent;
    GBinderLocalReply* reply = NULL;
    GBinderReader reader;
    GBinderRemoteObject* remote_obj;
    guint32 num;
    char* str;

    GDEBUG("%s %u", gbinder_remote_request_interface(req), code);
    gbinder_remote_request_init_reader(req, &reader);
    *status = -1;

    /* Lock */
    g_mutex_lock(&self->mutex);
    switch (code) {
    case GET_SERVICE_TRANSACTION:
    case CHECK_SERVICE_TRANSACTION:
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
        str = gbinder_reader_read_string16(&reader);
        remote_obj = gbinder_reader_read_object(&reader);
        if (str && remote_obj && gbinder_reader_read_uint32(&reader, &num)) {
            const GPtrArray* watches = self->watches;
            guint i;

            GDEBUG("Adding '%s'", str);
            g_hash_table_replace(self->objects, str, remote_obj);
            reply = gbinder_local_object_new_reply(obj);
            *status = GBINDER_STATUS_OK;

            for (i = 0; i < watches->len; i++) {
                const ServiceManagerAidlWatch* watch = watches->pdata[i];

                if (!strcmp(str, watch->name)) {
                    test_servicemanager_aidl_watch_notify(watch, remote_obj);
                }
            }
            remote_obj = NULL;
            str = NULL;
        }
        g_free(str);
        gbinder_remote_object_unref(remote_obj);
        break;
    case LIST_SERVICES_TRANSACTION:
        if (gbinder_reader_read_uint32(&reader, &num)) {
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
    case REGISTER_FOR_NOTIFICATIONS_TRANSACTION:
        /*
         * void registerForNotifications(@utf8InCpp String name,
         *     IServiceCallback callback);
         */
        if (self->notify) {
            char* name = gbinder_reader_read_string16(&reader);
            GBinderRemoteObject* obj = gbinder_reader_read_object(&reader);

            if (name && obj) {
                ServiceManagerAidlWatch* watch =
                    g_new(ServiceManagerAidlWatch, 1);

                GDEBUG("Registering watcher %u for '%s'",
                    (guint) obj->handle, name);
                watch->watcher = gbinder_client_new(obj, CALLBACK_IFACE);
                watch->name = name;
                name = NULL; /* We have stolen it */
                g_ptr_array_add(self->watches, watch);
                *status = GBINDER_STATUS_OK;
            }
            gbinder_remote_object_unref(obj);
            g_free(name);
        } else {
            GDEBUG("registerForNotifications not supported");
        }
        break;
    case UNREGISTER_FOR_NOTIFICATIONS_TRANSACTION:
        /*
         * void unregisterForNotifications(@utf8InCpp String name,
         *     IServiceCallback callback);
         */
        if (self->notify) {
            char* name = gbinder_reader_read_string16(&reader);
            GBinderRemoteObject* obj = gbinder_reader_read_object(&reader);

            if (name && obj) {
                GPtrArray* watches = self->watches;
                guint i;

                for (i = 0; i < watches->len; i++) {
                    ServiceManagerAidlWatch* watch = watches->pdata[i];

                    if (watch->watcher->remote->handle == obj->handle &&
                        !strcmp(name, watch->name)) {
                        GDEBUG("Unregistering watcher %u for '%s'",
                            (guint) obj->handle, name);
                        g_ptr_array_remove_index(watches, i);
                        *status = GBINDER_STATUS_OK;
                        break;
                    }
                }
            }
            gbinder_remote_object_unref(obj);
            g_free(name);
        } else {
            GDEBUG("registerForNotifications not supported");
        }
        break;
    default:
        GDEBUG("Unhandled command %u", code);
        break;
    }
    g_mutex_unlock(&self->mutex);
    /* Unlock */

    return reply;
}

static
GBinderLocalReply*
servicemanager_aidl_handle_looper_transaction(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
{
    return !g_strcmp0(gbinder_remote_request_interface(req), SVCMGR_IFACE) ?
        servicemanager_aidl_handler(SERVICE_MANAGER_AIDL(obj),
            req, code, status) :
        GBINDER_LOCAL_OBJECT_CLASS(service_manager_aidl_parent_class)->
            handle_looper_transaction(obj, req, code, flags, status);
}

static
GBINDER_LOCAL_TRANSACTION_SUPPORT
servicemanager_aidl_can_handle_transaction(
    GBinderLocalObject* self,
    const char* iface,
    guint code)
{
    /* Handle servicemanager transactions on the looper thread */
    return !g_strcmp0(iface, SVCMGR_IFACE) ? GBINDER_LOCAL_TRANSACTION_LOOPER :
        GBINDER_LOCAL_OBJECT_CLASS(service_manager_aidl_parent_class)->
            can_handle_transaction(self, iface, code);
}

static
void
service_manager_aidl_finalize(
    GObject* object)
{
    ServiceManagerAidl* self = SERVICE_MANAGER_AIDL(object);

    g_mutex_clear(&self->mutex);
    g_hash_table_destroy(self->objects);
    g_ptr_array_free(self->watches, TRUE);
    G_OBJECT_CLASS(service_manager_aidl_parent_class)->finalize(object);
}

static
void
service_manager_aidl_init(
    ServiceManagerAidl* self)
{
    g_mutex_init(&self->mutex);
    self->objects = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) gbinder_remote_object_unref);
    self->watches = g_ptr_array_new_with_free_func((GDestroyNotify)
        test_servicemanager_aidl_watch_destroy);
}

static
void
service_manager_aidl_class_init(
    ServiceManagerAidlClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = service_manager_aidl_finalize;
    klass->can_handle_transaction =
        servicemanager_aidl_can_handle_transaction;
    klass->handle_looper_transaction =
        servicemanager_aidl_handle_looper_transaction;
}

static
ServiceManagerAidl*
servicemanager_aidl_new2(
    const char* dev,
    gboolean notify)
{
    ServiceManagerAidl* self = g_object_new(SERVICE_MANAGER_AIDL_TYPE, NULL);
    GBinderLocalObject* obj = GBINDER_LOCAL_OBJECT(self);
    GBinderIpc* ipc = gbinder_ipc_new(dev, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);

    self->notify = notify;
    gbinder_local_object_init_base(obj, ipc, servicemanager_aidl_ifaces,
        NULL, NULL);
    test_binder_register_object(fd, obj, SVCMGR_HANDLE);
    gbinder_ipc_register_local_object(ipc, obj);
    gbinder_ipc_unref(ipc);
    return self;
}

static
ServiceManagerAidl*
servicemanager_aidl_new(
    const char* dev)
{
    return servicemanager_aidl_new2(dev, FALSE);
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
test_get_run(
    void)
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev, NULL);
    ServiceManagerAidl* smsvc = servicemanager_aidl_new(dev);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* name = "name";
    GBinderServiceManager* sm;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
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

    /* Query the object (this time it must be there) and wait for completion */
    GDEBUG("Querying '%s' again", name);
    g_assert(gbinder_servicemanager_get_service(sm, name, test_get_cb, loop));
    test_run(&test_opt, loop);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(obj);
    gbinder_local_object_unref(GBINDER_LOCAL_OBJECT(smsvc));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_get(
    void)
{
    test_run_in_context(&test_opt, test_get_run);
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
test_list_run(
    void)
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev, NULL);
    ServiceManagerAidl* smsvc = servicemanager_aidl_new(dev);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    const char* name = "name";
    GBinderServiceManager* sm;
    TestList test;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
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
    gbinder_local_object_unref(GBINDER_LOCAL_OBJECT(smsvc));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    test_binder_exit_wait(&test_opt, test.loop);

    g_strfreev(test.list);
    g_main_loop_unref(test.loop);
}

static
void
test_list(
    void)
{
    test_run_in_context(&test_opt, test_list_run);
}

/*==========================================================================*
 * notify
 *==========================================================================*/

typedef struct test_notify {
    const char* name;
    gulong watch_id;
    GBinderLocalObject* obj;
    GMainLoop* loop;
} TestNotify;

static
gboolean
test_notify_done_cb(
    GBinderServiceManager* sm,
    char** services,
    void* user_data)
{
    TestNotify* test = user_data;

    g_assert_true(gutil_strv_contains(services, test->name));
    gbinder_local_object_unref(test->obj);
    test->obj = NULL;
    g_main_loop_quit(test->loop);
    return FALSE;
}

static
void
test_notify_cb(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data)
{
    TestNotify* test = user_data;

    GDEBUG("'%s' is registered", name);
    g_assert_cmpuint(test->watch_id, != ,0);
    gbinder_servicemanager_remove_handler(sm, test->watch_id);
    test->watch_id = 0;
    g_assert(gbinder_servicemanager_list(sm, test_notify_done_cb, test));
}

static
void
test_notify_run(
    gconstpointer param)
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    const gboolean aidl_notify = GPOINTER_TO_INT(param);
    GBinderIpc* ipc = gbinder_ipc_new(dev, NULL);
    ServiceManagerAidl* svc = servicemanager_aidl_new2(dev, aidl_notify);
    const int fd = gbinder_driver_fd(ipc->driver);
    TestNotify test;
    GBinderServiceManager* sm;

    memset(&test, 0, sizeof(test));
    test.name = "test";
    test.loop = g_main_loop_new(NULL, FALSE);
    test.obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);

    /* Set up binder simulator */
    test_binder_register_object(fd, test.obj, AUTO_HANDLE);
    sm = gbinder_servicemanager_new(dev);
    gbinder_ipc_set_max_threads(ipc, 1);

    /* Start watching */
    test.watch_id = gbinder_servicemanager_add_registration_handler(sm,
        test.name, test_notify_cb, &test);
    g_assert_cmpuint(test.watch_id, != ,0);

    /* Register the object and wait for completion */
    GDEBUG("Registering object '%s' => %p", test.name, test.obj);
    g_assert(gbinder_servicemanager_add_service(sm, test.name, test.obj,
        test_add_cb, NULL));

    /* test_notify_cb will stop the loop */
    test_run(&test_opt, test.loop);
    g_assert_cmpuint(test.watch_id, == ,0);
    g_assert_null(test.obj);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(GBINDER_LOCAL_OBJECT(svc));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    test_binder_exit_wait(&test_opt, test.loop);
    g_main_loop_unref(test.loop);
}

static
void
test_notify(
    gconstpointer param)
{
    test_run_in_context_param(&test_opt, test_notify_run, param);
}

/*==========================================================================*
 * notify2
 *==========================================================================*/

static
void
test_notify2_cb(
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
test_notify2_run(
    void)
{
    /* This test relies on the polling notification mechanism */
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev, NULL);
    ServiceManagerAidl* smsvc = servicemanager_aidl_new(dev);
    GBinderLocalObject* obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    GBinderServiceManager* sm;
    const char* name1 = "name1";
    const char* name2 = "name2";
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id1, id2;

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
    sm = gbinder_servicemanager_new(dev);
    gbinder_ipc_set_max_threads(ipc, 1);

    /* Register the object synchronously (twice)*/
    GDEBUG("Registering object '%s' => %p", name1, obj);
    g_assert_cmpint(gbinder_servicemanager_add_service_sync(sm,name1,obj),==,0);
    g_assert(gbinder_servicemanager_get_service_sync(sm, name1, NULL));
    GDEBUG("Registering object '%s' => %p", name2, obj);
    g_assert_cmpint(gbinder_servicemanager_add_service_sync(sm,name2,obj),==,0);
    g_assert(gbinder_servicemanager_get_service_sync(sm, name2, NULL));

    /* Watch for the first name to create internal name watcher */
    id1 = gbinder_servicemanager_add_registration_handler(sm, name1,
        test_notify2_cb, loop);
    g_assert(id1);
    test_run(&test_opt, loop);

    /* Now watch for the second name */
    id2 = gbinder_servicemanager_add_registration_handler(sm, name2,
        test_notify2_cb, loop);
    g_assert(id2);
    test_run(&test_opt, loop);

    gbinder_servicemanager_remove_handler(sm, id1);
    gbinder_servicemanager_remove_handler(sm, id2);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(obj);
    gbinder_local_object_unref(GBINDER_LOCAL_OBJECT(smsvc));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_notify2(
    void)
{
    test_run_in_context(&test_opt, test_notify2_run);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/servicemanager_aidl/" t

int main(int argc, char* argv[])
{
    TestConfig config;
    int result;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("get"), test_get);
    g_test_add_func(TEST_("list"), test_list);
    g_test_add_data_func(TEST_("notify/1"), GINT_TO_POINTER(1), test_notify);
    g_test_add_data_func(TEST_("notify/2"), GINT_TO_POINTER(0), test_notify);
    g_test_add_func(TEST_("notify/3"), test_notify2);
    test_init(&test_opt, argc, argv);
    test_config_init(&config, TMP_DIR_TEMPLATE);
    result = g_test_run();
    test_config_cleanup(&config);
    return result;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
