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

#include "test_servicemanager_hidl.h"

#include "gbinder_local_object_p.h"
#include "gbinder_local_reply.h"
#include "gbinder_local_request.h"
#include "gbinder_remote_request.h"
#include "gbinder_remote_reply.h"
#include "gbinder_remote_object.h"
#include "gbinder_reader.h"
#include "gbinder_writer.h"
#include "gbinder_client.h"
#include "gbinder_ipc.h"

#include <gutil_log.h>
#include <gutil_strv.h>

/*==========================================================================*
 * Test service manager
 *==========================================================================*/

#define BASE_IFACE "android.hidl.base@1.0::IBase"
#define MANAGER_IFACE "android.hidl.manager@1.0::IServiceManager"
#define NOTIFICATION_IFACE "android.hidl.manager@1.0::IServiceNotification"

const char* const servicemanager_hidl_ifaces[] = { MANAGER_IFACE, NULL };

enum servicemanager_hidl_tx {
    GET_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    ADD_TRANSACTION,
    GET_TRANSPORT_TRANSACTION,
    LIST_TRANSACTION,
    LIST_BY_INTERFACE_TRANSACTION,
    REGISTER_FOR_NOTIFICATIONS_TRANSACTION,
    DEBUG_DUMP_TRANSACTION,
    REGISTER_PASSTHROUGH_CLIENT_TRANSACTION
};

enum servicemanager_hidl_notify_tx {
    ON_REGISTRATION_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION
};

typedef GBinderLocalObjectClass TestServiceManagerHidlClass;
struct test_servicemanager_hidl {
    GBinderLocalObject parent;
    GHashTable* objects;
    GPtrArray* watchers;
    GMutex mutex;
};

typedef struct test_servicemanager_hidl_add {
    TestServiceManagerHidl* manager;
    GBinderRemoteObject* object;
    char* instance;
} TestServiceManagerHidlAdd;

struct test_servicemanager_hidl_watcher {
    GBinderClient* client;
    char* iface;
    char* name;
} TestServiceManagerHidlWatcher;

#define THIS_TYPE test_servicemanager_hidl_get_type()
#define PARENT_CLASS test_servicemanager_hidl_parent_class
#define THIS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), THIS_TYPE, \
    TestServiceManagerHidl))
G_DEFINE_TYPE(TestServiceManagerHidl, test_servicemanager_hidl, \
    GBINDER_TYPE_LOCAL_OBJECT)

static
void
test_servicemanager_hidl_notify(
    TestServiceManagerHidl* self,
    GBinderClient* watcher,
    const char* iface,
    const char* instance,
    gboolean preexisting)
{
    GBinderLocalRequest* notify = gbinder_client_new_request(watcher);
    GBinderWriter writer;
    char* iface2 = g_strdup(iface);
    char* instance2 = g_strdup(instance);

    gbinder_local_request_init_writer(notify, &writer);
    gbinder_writer_append_hidl_string(&writer, iface2);
    gbinder_writer_append_hidl_string(&writer, instance2);
    gbinder_writer_append_bool(&writer, preexisting);

    gbinder_writer_add_cleanup(&writer, g_free, iface2);
    gbinder_writer_add_cleanup(&writer, g_free, instance2);

    gbinder_client_transact(watcher, ON_REGISTRATION_TRANSACTION,
        GBINDER_TX_FLAG_ONEWAY, notify, NULL, NULL, NULL);
    gbinder_local_request_unref(notify);
}

static
void
test_servicemanager_hidl_notify_all(
    TestServiceManagerHidl* self,
    const char* iface,
    const char* instance,
    gboolean preexisting)
{
    GPtrArray* watchers = self->watchers;
    guint i;

    /* For unit test purposes, just always notify all watchers */
    for (i = 0; i < watchers->len; i++) {
        test_servicemanager_hidl_notify(self, watchers->pdata[i],
            iface, instance, preexisting);
    }
}

static
void
test_servicemanager_hidl_add_complete2(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    TestServiceManagerHidlAdd* add = user_data;
    TestServiceManagerHidl* self = add->manager;
    const char* instance = add->instance;

    g_mutex_lock(&self->mutex);
    /* Remove the temporary entry */
    GDEBUG("Dropping '%s'", instance);
    g_hash_table_remove(self->objects, instance);
    if (reply) {
        GBinderReader reader;
        gint32 status;

        gbinder_remote_reply_init_reader(reply, &reader);
        if (gbinder_reader_read_int32(&reader, &status) && status == 0) {
            char** ifaces = gbinder_reader_read_hidl_string_vec(&reader);

            if (ifaces) {
                char** ptr = ifaces;

                while (*ptr) {
                    const char* iface = *ptr++;
                    char* fqinstance = g_strconcat(iface, "/", instance, NULL);

                    /* Add permanent entries */
                    GDEBUG("Adding '%s'", fqinstance);
                    g_hash_table_replace(self->objects, fqinstance,
                        gbinder_remote_object_ref(add->object));
                    test_servicemanager_hidl_notify_all(self, iface,
                        instance, FALSE);
                }
                g_strfreev(ifaces);
            }
        }
    }
    g_mutex_unlock(&self->mutex);
}

static
void
test_servicemanager_hidl_add_done(
    gpointer data)
{
    TestServiceManagerHidlAdd* add = data;

    gbinder_remote_object_unref(add->object);
    g_object_unref(add->manager);
    g_free(add->instance);
    g_free(add);
}

static
gboolean
test_servicemanager_hidl_add_complete(
    gpointer data)
{
    TestServiceManagerHidlAdd* add = data;
    GBinderClient* client = gbinder_client_new(add->object, BASE_IFACE);

    gbinder_client_transact(client, HIDL_DESCRIPTOR_CHAIN_TRANSACTION, 0, NULL,
        test_servicemanager_hidl_add_complete2,
        test_servicemanager_hidl_add_done, add);
    gbinder_client_unref(client);
    return G_SOURCE_REMOVE;
}

/*==========================================================================*
 * Call handlers
 *==========================================================================*/

static
GBinderLocalReply*
test_servicemanager_hidl_get(
    TestServiceManagerHidl* self,
    GBinderRemoteRequest* req)
{
    GBinderRemoteObject* remote_obj;
    GBinderLocalReply* reply =
        gbinder_local_object_new_reply(GBINDER_LOCAL_OBJECT(self));
    GBinderReader reader;
    GBinderWriter writer;
    const char* ifname;
    const char* instance;
    char* fqinstance;

    gbinder_remote_request_init_reader(req, &reader);
    g_assert((ifname = gbinder_reader_read_hidl_string_c(&reader)));
    g_assert((instance = gbinder_reader_read_hidl_string_c(&reader)));
    fqinstance = g_strconcat(ifname, "/", instance, NULL);

    remote_obj = g_hash_table_lookup(self->objects, fqinstance);
    if (!remote_obj) {
        remote_obj = g_hash_table_lookup(self->objects, instance);
    }
    gbinder_local_reply_init_writer(reply, &writer);
    gbinder_local_reply_append_int32(reply, GBINDER_STATUS_OK);
    if (remote_obj) {
        GDEBUG("Found name '%s' => %p", fqinstance, remote_obj);
    } else {
        GDEBUG("Name '%s' not found", fqinstance);
    }
    gbinder_local_reply_append_remote_object(reply, remote_obj);
    g_free(fqinstance);
    return reply;
}

static
GBinderLocalReply*
test_servicemanager_hidl_add(
    TestServiceManagerHidl* self,
    GBinderRemoteRequest* req)
{
    GBinderRemoteObject* remote_obj;
    GBinderLocalReply* reply =
        gbinder_local_object_new_reply(GBINDER_LOCAL_OBJECT(self));
    GBinderReader reader;
    const char* instance;
    gboolean success;

    gbinder_remote_request_init_reader(req, &reader);
    instance = gbinder_reader_read_hidl_string_c(&reader);
    remote_obj = gbinder_reader_read_object(&reader);

    if (instance && remote_obj) {
        const char* sep = strrchr(instance, '/');

        GDEBUG("Adding '%s'", instance);
        g_hash_table_replace(self->objects, g_strdup(instance), remote_obj);
        if (sep) {
            /* Alread know the interface */
            char* iface = g_strndup(instance, sep - instance);

            test_servicemanager_hidl_notify_all(self, iface, sep + 1, FALSE);
            g_free(iface);
        } else {
            /* Query interface chain on the main thread */
            TestServiceManagerHidlAdd* add = g_new(TestServiceManagerHidlAdd,1);

            g_object_ref(add->manager = self);
            add->object = gbinder_remote_object_ref(remote_obj);
            add->instance = g_strdup(instance);
            g_idle_add(test_servicemanager_hidl_add_complete, add);
        }
        success = TRUE;
    } else {
        gbinder_remote_object_unref(remote_obj);
        success = FALSE;
    }

    gbinder_local_reply_append_bool(reply, success);
    return reply;
}

static
GBinderLocalReply*
test_servicemanager_hidl_list(
    TestServiceManagerHidl* self,
    GBinderRemoteRequest* req)
{
    GHashTableIter it;
    GBinderReader reader;
    GBinderWriter writer;
    GBinderLocalReply* reply =
        gbinder_local_object_new_reply(GBINDER_LOCAL_OBJECT(self));
    gpointer key;
    char** list = NULL;

    gbinder_remote_request_init_reader(req, &reader);
    g_assert(gbinder_reader_at_end(&reader));

    g_hash_table_iter_init(&it, self->objects);
    while (g_hash_table_iter_next(&it, &key, NULL)) {
        list = gutil_strv_add(list, key);
    }

    gbinder_local_reply_init_writer(reply, &writer);
    gbinder_writer_append_int32(&writer, 0);
    gbinder_writer_append_hidl_string_vec(&writer, (const char**) list, -1);
    gbinder_writer_add_cleanup(&writer, (GDestroyNotify) g_strfreev, list);
    return reply;
}

static
GBinderLocalReply*
test_servicemanager_hidl_register_for_notifications(
    TestServiceManagerHidl* self,
    GBinderRemoteRequest* req)
{
    GBinderRemoteObject* watcher;
    GBinderLocalReply* reply =
        gbinder_local_object_new_reply(GBINDER_LOCAL_OBJECT(self));
    GBinderReader reader;
    const char* iface;
    const char* instance;
    gboolean success;

    gbinder_remote_request_init_reader(req, &reader);
    iface = gbinder_reader_read_hidl_string_c(&reader);
    instance = gbinder_reader_read_hidl_string_c(&reader);
    watcher = gbinder_reader_read_object(&reader);
 
    if (watcher) {
        GBinderClient* wc = gbinder_client_new(watcher, NOTIFICATION_IFACE);
        GHashTableIter it;
        gpointer key;

        GDEBUG("Registering watcher %s/%s", iface, instance);
        g_ptr_array_add(self->watchers, wc);
        gbinder_remote_object_unref(watcher); /* Client keeps the reference */

        /* Send notifications for pre-existing services */
        g_hash_table_iter_init(&it, self->objects);
        while (g_hash_table_iter_next(&it, &key, NULL)) {
            const char* name = key;
            const char* sep = strrchr(name, '/');

            if (sep) {
                char* intf = g_strndup(name, sep - name);

                test_servicemanager_hidl_notify(self, wc, intf, sep + 1, TRUE);
                g_free(intf);
            }
        }
        success = TRUE;
    } else {
        success = FALSE;
    }

    gbinder_local_reply_append_int32(reply, 0);
    gbinder_local_reply_append_bool(reply, success);
    return reply;
}

static
GBinderLocalReply*
test_servicemanager_hidl_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    TestServiceManagerHidl* self = THIS(user_data);
    GBinderLocalReply* reply = NULL;

    g_assert(!flags);
    g_assert_cmpstr(gbinder_remote_request_interface(req), == ,MANAGER_IFACE);
    *status = -1;
    g_mutex_lock(&self->mutex);
    switch (code) {
    case GET_TRANSACTION:
        reply = test_servicemanager_hidl_get(self, req);
        break;
    case ADD_TRANSACTION:
        reply = test_servicemanager_hidl_add(self, req);
        break;
    case LIST_TRANSACTION:
        reply = test_servicemanager_hidl_list(self, req);
        break;
    case REGISTER_FOR_NOTIFICATIONS_TRANSACTION:
        reply = test_servicemanager_hidl_register_for_notifications(self, req);
        break;
    default:
        GDEBUG("Unhandled command %u", code);
        break;
    }
    g_mutex_unlock(&self->mutex);
    return reply;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

TestServiceManagerHidl*
test_servicemanager_hidl_new(
    GBinderIpc* ipc)
{
    TestServiceManagerHidl* self = g_object_new(THIS_TYPE, NULL);
    GBinderLocalObject* obj = GBINDER_LOCAL_OBJECT(self);

    gbinder_local_object_init_base(obj, ipc, servicemanager_hidl_ifaces,
        test_servicemanager_hidl_handler, self);
    gbinder_ipc_register_local_object(ipc, obj);
    return self;
}

void
test_servicemanager_hidl_free(
    TestServiceManagerHidl* self)
{
    gbinder_local_object_drop(GBINDER_LOCAL_OBJECT(self));
}

GBinderIpc*
test_servicemanager_hidl_ipc(
    TestServiceManagerHidl* self)
{
    return self ? self->parent.ipc : 0;
}

guint
test_servicemanager_hidl_object_count(
    TestServiceManagerHidl* self)
{
    guint count = 0;

    if (self) {
        g_mutex_lock(&self->mutex);
        count = g_hash_table_size(self->objects);
        g_mutex_unlock(&self->mutex);
    }
    return count;
}

GBinderRemoteObject*
test_servicemanager_hidl_lookup(
    TestServiceManagerHidl* self,
    const char* name)
{
    GBinderRemoteObject* object = NULL;

    if (self) {
        g_mutex_lock(&self->mutex);
        object = g_hash_table_lookup(self->objects, name);
        g_mutex_unlock(&self->mutex);
    }
    return object;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
GBINDER_LOCAL_TRANSACTION_SUPPORT
test_servicemanager_hidl_can_handle_transaction(
    GBinderLocalObject* object,
    const char* iface,
    guint code)
{
    /*
     * Handle all transactions on the looper thread to avoid deadlocks.
     * Unlike the real situation, binder simulation has one main thread
     * shared by both sides of the binder socket connection.
     */
    return GBINDER_LOCAL_TRANSACTION_LOOPER;
}

static
GBinderLocalReply*
test_servicemanager_hidl_handle_looper_transaction(
    GBinderLocalObject* object,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status)
{
    if (!g_strcmp0(gbinder_remote_request_interface(req), MANAGER_IFACE)) {
        return GBINDER_LOCAL_OBJECT_CLASS(PARENT_CLASS)->
            handle_transaction(object, req, code, flags, status);
    } else {
        return GBINDER_LOCAL_OBJECT_CLASS(PARENT_CLASS)->
            handle_looper_transaction(object, req, code, flags, status);
    }
}

static
void
test_servicemanager_hidl_finalize(
    GObject* object)
{
    TestServiceManagerHidl* self = THIS(object);

    g_mutex_clear(&self->mutex);
    g_hash_table_destroy(self->objects);
    g_ptr_array_free(self->watchers, TRUE);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
test_servicemanager_hidl_init(
    TestServiceManagerHidl* self)
{
    g_mutex_init(&self->mutex);
    self->objects = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) gbinder_remote_object_unref);
    self->watchers = g_ptr_array_new_with_free_func((GDestroyNotify)
        gbinder_client_unref);
}

static
void
test_servicemanager_hidl_class_init(
    TestServiceManagerHidlClass* klass)
{
    GObjectClass* object = G_OBJECT_CLASS(klass);
    GBinderLocalObjectClass* local_object = GBINDER_LOCAL_OBJECT_CLASS(klass);

    object->finalize = test_servicemanager_hidl_finalize;
    local_object->can_handle_transaction =
        test_servicemanager_hidl_can_handle_transaction;
    local_object->handle_looper_transaction =
        test_servicemanager_hidl_handle_looper_transaction;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
