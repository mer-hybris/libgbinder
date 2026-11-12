/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
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

#include "gbinder_servicemanager_aidl.h"
#include "gbinder_servicepoll.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_client_p.h"
#include "gbinder_log.h"

#include <gbinder_local_object.h>
#include <gbinder_local_request.h>
#include <gbinder_remote_reply.h>
#include <gbinder_remote_request.h>

typedef struct gbinder_servicemanager_aidl_poll {
    GBinderServicePoll* poll;
    char* name;
    gulong handler_id;
    GBinderEventLoopTimeout* notify;
} GBinderServiceManagerAidlPoll;

typedef struct gbinder_servicemanager_aidl_callback_registration_call {
    int ref_count;  /* tx and registration_call_table hold the refs */
    gboolean is_register;
    GBinderServiceManagerAidl* obj;
    gulong tx;
    char* name;
} GBinderServiceManagerAidlCallbackRegistrationCall;

struct gbinder_servicemanager_aidl_priv {
    GBinderServicePoll* poll;
    GBinderLocalObject* callback;
    GHashTable* registration_call_table;
    GHashTable* poll_table;
};

G_DEFINE_TYPE(GBinderServiceManagerAidl,
    gbinder_servicemanager_aidl,
    GBINDER_TYPE_SERVICEMANAGER)

#define PARENT_CLASS gbinder_servicemanager_aidl_parent_class
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, \
        GBINDER_TYPE_SERVICEMANAGER_AIDL, GBinderServiceManagerAidl)
#define GET_THIS_CLASS(obj) GBINDER_SERVICEMANAGER_AIDL_GET_CLASS(obj)

#define SERVICEMANAGER_AIDL_IFACE  "android.os.IServiceManager"
#define SERVICEMANAGER_AIDL_CALLBACK_IFACE  "android.os.IServiceCallback"

enum gbinder_servicemanager_aidl_calls {
    GET_SERVICE_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    CHECK_SERVICE_TRANSACTION,
    ADD_SERVICE_TRANSACTION,
    LIST_SERVICES_TRANSACTION,
    REGISTER_FOR_NOTIFICATIONS_TRANSACTION,
    UNREGISTER_FOR_NOTIFICATIONS_TRANSACTION
};

/*==========================================================================*
 * Poll (if registerForNotifications is not supported)
 *==========================================================================*/

static
void
gbinder_servicemanager_aidl_poll_proc(
    GBinderServicePoll* poll,
    const char* name_added,
    void* user_data)
{
    GBinderServiceManagerAidlPoll* watch = user_data;

    if (!g_strcmp0(name_added, watch->name)) {
        GBinderServiceManager* manager =
            gbinder_servicepoll_manager(watch->poll);

        if (watch->notify) {
            gbinder_timeout_remove(watch->notify);
            watch->notify = NULL;
        }
        gbinder_servicemanager_service_registered(manager, name_added);
    }
}

static
gboolean
gbinder_servicemanager_aidl_poll_notify(
    gpointer user_data)
{
    GBinderServiceManagerAidlPoll* watch = user_data;
    GBinderServiceManager* manager = gbinder_servicepoll_manager(watch->poll);
    char* name = g_strdup(watch->name);

    GASSERT(watch->notify);
    watch->notify = NULL;
    gbinder_servicemanager_service_registered(manager, name);
    g_free(name);
    return G_SOURCE_REMOVE;
}

static
void
gbinder_servicemanager_aidl_poll_free(
    gpointer user_data)
{
    GBinderServiceManagerAidlPoll* watch = user_data;

    gbinder_timeout_remove(watch->notify);
    gbinder_servicepoll_remove_handler(watch->poll, watch->handler_id);
    gbinder_servicepoll_unref(watch->poll);
    g_free(watch->name);
    g_slice_free(GBinderServiceManagerAidlPoll, watch);
}

static
void
gbinder_servicemanager_aidl_poll_start(
    GBinderServiceManagerAidl* self,
    const char* name)
{
    GBinderServiceManagerAidlPriv* priv = self->priv;
    GBinderServiceManagerAidlPoll* watch =
        g_slice_new0(GBinderServiceManagerAidlPoll);

    watch->name = g_strdup(name);
    watch->poll = gbinder_servicepoll_new(&self->manager, &priv->poll);
    watch->handler_id = gbinder_servicepoll_add_handler(priv->poll,
        gbinder_servicemanager_aidl_poll_proc, watch);

    if (!priv->poll_table) {
        priv->poll_table = g_hash_table_new_full(g_str_hash, g_str_equal,
            NULL, gbinder_servicemanager_aidl_poll_free);
    }

    g_hash_table_replace(priv->poll_table, watch->name, watch);
    if (gbinder_servicepoll_is_known_name(watch->poll, name)) {
        watch->notify =
            gbinder_idle_add(gbinder_servicemanager_aidl_poll_notify, watch);
    }
}

/*==========================================================================*
 * IServiceCallback
 *==========================================================================*/

enum gbinder_servicemanager_aidl_callback_transactions {
    ON_REGISTRATION_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION
};

static
GBinderLocalReply*
gbinder_servicemanager_aidl_callback(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    GBinderServiceManagerAidl* self = THIS(user_data);
    const char* iface = gbinder_remote_request_interface(req);

    if (!g_strcmp0(iface, SERVICEMANAGER_AIDL_CALLBACK_IFACE) &&
        code == ON_REGISTRATION_TRANSACTION) {
        GBinderReader reader;
        char* name;

        /*
         * IServiceCallback.aidl:
         * void onRegistration(@utf8InCpp String name, IBinder binder);
         */
        gbinder_remote_request_init_reader(req, &reader);
        name = gbinder_reader_read_string16(&reader);
        GDEBUG("%s %u onRegistration %s", iface, code, name);
        gbinder_servicemanager_service_registered(&self->manager, name);
        g_free(name);
        *status = GBINDER_STATUS_OK;
    } else {
        GDEBUG("%s %u", iface, code);
        *status = GBINDER_STATUS_FAILED;
    }
    return NULL;
}

static
void
gbinder_servicemanager_aidl_callback_registration_call_done(
    void* user_data)
{
    GBinderServiceManagerAidlCallbackRegistrationCall* call = user_data;
    GBinderServiceManagerAidlPriv* priv = call->obj->priv;

    call->tx = 0;
    /* Only remove our own entry, a newer call may have replaced it */
    if (priv->registration_call_table && g_hash_table_lookup
        (priv->registration_call_table, call->name) == call) {
        g_hash_table_remove(priv->registration_call_table, call->name);
    }
}

static
void
gbinder_servicemanager_aidl_callback_register_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    GBinderServiceManagerAidlCallbackRegistrationCall* call = user_data;
    GBinderServiceManagerAidl* self = call->obj;
    GBinderServiceManagerAidlPriv* priv = self->priv;

    gbinder_servicemanager_aidl_callback_registration_call_done(call);

    /*
     * If registration fails, drop the callback object and revert
     * to polling.
     */
    if (status != GBINDER_STATUS_OK) {
        GHashTable* table = priv->registration_call_table;
        GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
        guint i;

        GWARN("registerForNotifications(%s) tx error %d", call->name, status);
        gbinder_local_object_drop(priv->callback);
        priv->callback = NULL;
        priv->registration_call_table = NULL;
        g_ptr_array_add(names, g_strdup(call->name));
        if (table) {
            GHashTableIter it;
            gpointer value;

            /* Other pending registrations are going to fail too */
            g_hash_table_iter_init(&it, table);
            while (g_hash_table_iter_next(&it, NULL, &value)) {
                GBinderServiceManagerAidlCallbackRegistrationCall* other =
                    value;

                if (other->is_register && g_strcmp0(other->name, call->name)) {
                    g_ptr_array_add(names, g_strdup(other->name));
                }
            }
            g_hash_table_destroy(table);
        }

        /* Revert to polling */
        for (i = 0; i < names->len; i++) {
            gbinder_servicemanager_aidl_poll_start(self, names->pdata[i]);
        }
        g_ptr_array_free(names, TRUE);
    }
}

static
void
gbinder_servicemanager_aidl_callback_unregister_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* call)
{
    gbinder_servicemanager_aidl_callback_registration_call_done(call);
}

static
void
gbinder_servicemanager_aidl_callback_registration_call_unref(
    GBinderServiceManagerAidlCallbackRegistrationCall* call)
{
    call->ref_count--;
    if (!call->ref_count) {
        g_object_unref(call->obj);
        g_free(call->name);
        g_free(call);
    }
}

static
void
gbinder_servicemanager_aidl_callback_registration_call_destroy(
    void* user_data)
{
    GBinderServiceManagerAidlCallbackRegistrationCall* call = user_data;

    /* Transaction completion callback */
    gbinder_servicemanager_aidl_callback_registration_call_done(call);
    gbinder_servicemanager_aidl_callback_registration_call_unref(call);
}

static
void
gbinder_servicemanager_aidl_callback_registration_call_drop(
    void* user_data)
{
    GBinderServiceManagerAidlCallbackRegistrationCall* call = user_data;

    /* Handles removal from registration_call_table */
    if (call->tx) {
        /* The call is being removed from registration_call_table before
         * completion. Cancel it. */
        gbinder_client_cancel(call->obj->manager.client, call->tx);
    }
    gbinder_servicemanager_aidl_callback_registration_call_unref(call);
}

static
gboolean
gbinder_servicemanager_aidl_callback_registration_call_submit(
    GBinderServiceManagerAidl* self,
    guint code,
    const char* name,
    GBinderClientReplyFunc fn)
{
    GBinderServiceManager* manager = &self->manager;
    GBinderServiceManagerAidlPriv* priv = self->priv;
    GBinderLocalRequest* req = gbinder_client_new_request(manager->client);
    GBinderServiceManagerAidlCallbackRegistrationCall* call =
        g_new0(GBinderServiceManagerAidlCallbackRegistrationCall, 1);

    call->ref_count = 1; /* Reference for the transaction */
    call->is_register =
        (fn == gbinder_servicemanager_aidl_callback_register_reply);
    g_object_ref(call->obj = self);
    call->name = g_strdup(name);

    /*
     * Both calls have the same arguments:
     *
     * void registerForNotifications(@utf8InCpp String name,
     *     IServiceCallback callback);
     *
     * void unregisterForNotifications(@utf8InCpp String name,
     *     IServiceCallback callback);
     */
    gbinder_local_request_append_string16(req, name);
    gbinder_local_request_append_local_object(req, priv->callback);
    call->tx = gbinder_client_transact(manager->client, code, 0, req, fn,
        gbinder_servicemanager_aidl_callback_registration_call_destroy, call);
    gbinder_local_request_unref(req);

    if (call->tx) {
        if (!priv->registration_call_table) {
            priv->registration_call_table = g_hash_table_new_full(g_str_hash,
                g_str_equal, NULL,
                gbinder_servicemanager_aidl_callback_registration_call_drop);
        }
        call->ref_count++; /* Reference for registration_call_table */
        g_hash_table_replace(priv->registration_call_table, call->name, call);
        return TRUE;
    } else {
        /* Transaction wasn't submitted, drop the ref */
        gbinder_servicemanager_aidl_callback_registration_call_unref(call);
        return FALSE;
    }
}

static
gboolean
gbinder_servicemanager_aidl_callback_register(
    GBinderServiceManagerAidl* self,
    const char* name)
{
    GBinderServiceManager* manager = &self->manager;
    GBinderServiceManagerAidlPriv* priv = self->priv;
    GBinderServiceManagerAidlClass* klass = GET_THIS_CLASS(self);

    if (!priv->callback) {
        priv->callback = gbinder_servicemanager_new_local_object(manager,
            SERVICEMANAGER_AIDL_CALLBACK_IFACE,
            gbinder_servicemanager_aidl_callback, self);
    }
    return gbinder_servicemanager_aidl_callback_registration_call_submit(self,
        klass->register_for_notifications_transaction, name,
        gbinder_servicemanager_aidl_callback_register_reply);
}

static
void
gbinder_servicemanager_aidl_callback_unregister(
    GBinderServiceManagerAidl* self,
    const char* name)
{
    GBinderServiceManagerAidlPriv* priv = self->priv;

    if (priv->callback) {
        GBinderServiceManagerAidlClass* klass = GET_THIS_CLASS(self);

        gbinder_servicemanager_aidl_callback_registration_call_submit(self,
            klass->unregister_for_notifications_transaction, name,
            gbinder_servicemanager_aidl_callback_unregister_reply);
    }
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
GBinderLocalRequest*
gbinder_servicemanager_aidl_list_services_req(
    GBinderClient* client,
    gint32 index)
{
    GBinderLocalRequest* req = gbinder_client_new_request(client);

    gbinder_local_request_append_int32(req, index);
    return req;
}

static
GBinderLocalRequest*
gbinder_servicemanager_aidl_add_service_req(
    GBinderClient* client,
    const char* name,
    GBinderLocalObject* obj)
{
    GBinderLocalRequest* req = gbinder_client_new_request(client);

    gbinder_local_request_append_string16(req, name);
    gbinder_local_request_append_local_object(req, obj);
    gbinder_local_request_append_int32(req, 0);
    return req;
}

static
char**
gbinder_servicemanager_aidl_list(
    GBinderServiceManager* manager,
    const GBinderIpcSyncApi* api)
{
    GPtrArray* list = g_ptr_array_new();
    GBinderClient* client = manager->client;
    GBinderServiceManagerAidlClass* klass = GET_THIS_CLASS(manager);
    GBinderLocalRequest* req = klass->list_services_req(client, 0);
    GBinderRemoteReply* reply;

    while ((reply = gbinder_client_transact_sync_reply2(client,
        LIST_SERVICES_TRANSACTION, req, NULL, api)) != NULL) {
        char* service = gbinder_remote_reply_read_string16(reply);

        gbinder_remote_reply_unref(reply);
        if (service) {
            g_ptr_array_add(list, service);
            gbinder_local_request_unref(req);
            req = klass->list_services_req(client, list->len);
        } else {
            break;
        }
    }

    gbinder_local_request_unref(req);
    g_ptr_array_add(list, NULL);
    return (char**)g_ptr_array_free(list, FALSE);
}

static
GBinderRemoteObject*
gbinder_servicemanager_aidl_get_service(
    GBinderServiceManager* self,
    const char* name,
    int* status,
    const GBinderIpcSyncApi* api)
{
    GBinderRemoteObject* obj;
    GBinderRemoteReply* reply;
    GBinderLocalRequest* req = gbinder_client_new_request(self->client);
    GBinderServiceManagerAidlClass* klass = GET_THIS_CLASS(self);

    gbinder_local_request_append_string16(req, name);
    reply = gbinder_client_transact_sync_reply2(self->client,
        klass->check_service_transaction, req, status, api);

    obj = gbinder_remote_reply_read_object(reply);
    gbinder_remote_reply_unref(reply);
    gbinder_local_request_unref(req);
    return obj;
}

static
int
gbinder_servicemanager_aidl_add_service(
    GBinderServiceManager* manager,
    const char* name,
    GBinderLocalObject* obj,
    const GBinderIpcSyncApi* api)
{
    int status;
    GBinderClient* client = manager->client;
    GBinderServiceManagerAidlClass* klass = GET_THIS_CLASS(manager);
    GBinderLocalRequest* req = klass->add_service_req(client, name, obj);
    GBinderRemoteReply* reply = gbinder_client_transact_sync_reply2(client,
        klass->add_service_transaction, req, &status, api);

    gbinder_remote_reply_unref(reply);
    gbinder_local_request_unref(req);
    return status;
}

static
GBINDER_SERVICEMANAGER_NAME_CHECK
gbinder_servicemanager_aidl_check_name(
    GBinderServiceManager* self,
    const char* name)
{
    return GBINDER_SERVICEMANAGER_NAME_OK;
}

static
gboolean
gbinder_servicemanager_aidl_watch(
    GBinderServiceManager* manager,
    const char* name)
{
    GBinderServiceManagerAidl* self = THIS(manager);
    GBinderServiceManagerAidlPriv* priv = self->priv;

    if (priv->poll_table ||
        !gbinder_servicemanager_aidl_callback_register(self, name)) {
        /* registerForNotifications isn't working */
        gbinder_servicemanager_aidl_poll_start(self, name);
    }
    return TRUE;
}

static
void
gbinder_servicemanager_aidl_unwatch(
    GBinderServiceManager* manager,
    const char* name)
{
    GBinderServiceManagerAidl* self = THIS(manager);
    GBinderServiceManagerAidlPriv* priv = self->priv;

    if (priv->registration_call_table) {
        g_hash_table_remove(priv->registration_call_table, name);
        gbinder_servicemanager_aidl_callback_unregister(self, name);
    }
    if (priv->poll_table) {
        g_hash_table_remove(priv->poll_table, name);
    }
}

static
void
gbinder_servicemanager_aidl_init(
    GBinderServiceManagerAidl* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        GBINDER_TYPE_SERVICEMANAGER_AIDL,
        GBinderServiceManagerAidlPriv);
}

static
void
gbinder_servicemanager_aidl_finalize(
    GObject* object)
{
    GBinderServiceManagerAidl* self = THIS(object);
    GBinderServiceManagerAidlPriv* priv = self->priv;

    gbinder_local_object_drop(priv->callback);
    if (priv->registration_call_table) {
        g_hash_table_destroy(priv->registration_call_table);
    }
    if (priv->poll_table) {
        g_hash_table_destroy(priv->poll_table);
    }
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
gbinder_servicemanager_aidl_class_init(
    GBinderServiceManagerAidlClass* klass)
{
    GBinderServiceManagerClass* manager = GBINDER_SERVICEMANAGER_CLASS(klass);
    GObjectClass* object = G_OBJECT_CLASS(klass);

    gbinder_servicemanager_class_common_init(manager);
    g_type_class_add_private(klass, sizeof(GBinderServiceManagerAidlPriv));

    klass->check_service_transaction = CHECK_SERVICE_TRANSACTION;
    klass->add_service_transaction = ADD_SERVICE_TRANSACTION;
    klass->list_services_transaction = LIST_SERVICES_TRANSACTION;
    klass->register_for_notifications_transaction =
        REGISTER_FOR_NOTIFICATIONS_TRANSACTION;
    klass->unregister_for_notifications_transaction =
        UNREGISTER_FOR_NOTIFICATIONS_TRANSACTION;

    klass->list_services_req = gbinder_servicemanager_aidl_list_services_req;
    klass->add_service_req = gbinder_servicemanager_aidl_add_service_req;

    manager->iface = SERVICEMANAGER_AIDL_IFACE;
    manager->default_device = GBINDER_DEFAULT_BINDER;

    manager->list = gbinder_servicemanager_aidl_list;
    manager->get_service = gbinder_servicemanager_aidl_get_service;
    manager->add_service = gbinder_servicemanager_aidl_add_service;
    manager->check_name = gbinder_servicemanager_aidl_check_name;
    /* normalize_name is not needed */
    manager->watch = gbinder_servicemanager_aidl_watch;
    manager->unwatch = gbinder_servicemanager_aidl_unwatch;

    object->finalize = gbinder_servicemanager_aidl_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
