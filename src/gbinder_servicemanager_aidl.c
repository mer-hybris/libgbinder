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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gbinder_servicemanager_aidl.h"
#include "gbinder_servicepoll.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_client_p.h"
#include "gbinder_log.h"

#include <gbinder_local_request.h>
#include <gbinder_remote_reply.h>

typedef struct gbinder_servicemanager_aidl_watch {
    GBinderServicePoll* poll;
    char* name;
    gulong handler_id;
    GBinderEventLoopTimeout* notify;
} GBinderServiceManagerAidlWatch;

struct gbinder_servicemanager_aidl_priv {
    GBinderServicePoll* poll;
    GHashTable* watch_table;
};

G_DEFINE_TYPE(GBinderServiceManagerAidl,
    gbinder_servicemanager_aidl,
    GBINDER_TYPE_SERVICEMANAGER)

#define PARENT_CLASS gbinder_servicemanager_aidl_parent_class
#define GBINDER_SERVICEMANAGER_AIDL(obj) \
    G_TYPE_CHECK_INSTANCE_CAST((obj), GBINDER_TYPE_SERVICEMANAGER_AIDL, \
    GBinderServiceManagerAidl)
#define GBINDER_SERVICEMANAGER_AIDL_GET_CLASS(obj) \
    G_TYPE_INSTANCE_GET_CLASS((obj), GBINDER_TYPE_SERVICEMANAGER_AIDL, \
    GBinderServiceManagerAidlClass)

enum gbinder_servicemanager_aidl_calls {
    GET_SERVICE_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    CHECK_SERVICE_TRANSACTION,
    ADD_SERVICE_TRANSACTION,
    LIST_SERVICES_TRANSACTION
};

#define SERVICEMANAGER_AIDL_IFACE  "android.os.IServiceManager"

static
void
gbinder_servicemanager_aidl_watch_proc(
    GBinderServicePoll* poll,
    const char* name_added,
    void* user_data)
{
    GBinderServiceManagerAidlWatch* watch = user_data;

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
gbinder_servicemanager_aidl_watch_notify(
    gpointer user_data)
{
    GBinderServiceManagerAidlWatch* watch = user_data;
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
gbinder_servicemanager_aidl_watch_free(
    gpointer user_data)
{
    GBinderServiceManagerAidlWatch* watch = user_data;

    gbinder_timeout_remove(watch->notify);
    gbinder_servicepoll_remove_handler(watch->poll, watch->handler_id);
    gbinder_servicepoll_unref(watch->poll);
    g_free(watch->name);
    g_slice_free(GBinderServiceManagerAidlWatch, watch);
}

static
GBinderServiceManagerAidlWatch*
gbinder_servicemanager_aidl_watch_new(
    GBinderServiceManagerAidl* self,
    const char* name)
{
    GBinderServiceManagerAidlPriv* priv = self->priv;
    GBinderServiceManagerAidlWatch* watch =
        g_slice_new0(GBinderServiceManagerAidlWatch);

    watch->name = g_strdup(name);
    watch->poll = gbinder_servicepoll_new(&self->manager, &priv->poll);
    watch->handler_id = gbinder_servicepoll_add_handler(priv->poll,
        gbinder_servicemanager_aidl_watch_proc, watch);
    return watch;
}

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
    GBinderServiceManagerAidlClass* klass =
        GBINDER_SERVICEMANAGER_AIDL_GET_CLASS(manager);
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

    gbinder_local_request_append_string16(req, name);
    reply = gbinder_client_transact_sync_reply2(self->client,
        CHECK_SERVICE_TRANSACTION, req, status, api);

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
    GBinderLocalRequest* req = GBINDER_SERVICEMANAGER_AIDL_GET_CLASS
        (manager)->add_service_req(client, name, obj);
    GBinderRemoteReply* reply = gbinder_client_transact_sync_reply2(client,
        ADD_SERVICE_TRANSACTION, req, &status, api);

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
    GBinderServiceManagerAidl* self = GBINDER_SERVICEMANAGER_AIDL(manager);
    GBinderServiceManagerAidlPriv* priv = self->priv;
    GBinderServiceManagerAidlWatch* watch =
        gbinder_servicemanager_aidl_watch_new(self, name);

    g_hash_table_replace(priv->watch_table, watch->name, watch);
    if (gbinder_servicepoll_is_known_name(watch->poll, name)) {
        watch->notify = gbinder_idle_add
            (gbinder_servicemanager_aidl_watch_notify, watch);
    }
    return TRUE;
}

static
void
gbinder_servicemanager_aidl_unwatch(
    GBinderServiceManager* manager,
    const char* name)
{
    GBinderServiceManagerAidl* self = GBINDER_SERVICEMANAGER_AIDL(manager);
    GBinderServiceManagerAidlPriv* priv = self->priv;

    g_hash_table_remove(priv->watch_table, name);
}

static
void
gbinder_servicemanager_aidl_init(
    GBinderServiceManagerAidl* self)
{
    GBinderServiceManagerAidlPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        GBINDER_TYPE_SERVICEMANAGER_AIDL, GBinderServiceManagerAidlPriv);

    self->priv = priv;
    priv->watch_table = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, gbinder_servicemanager_aidl_watch_free);
}

static
void
gbinder_servicemanager_aidl_finalize(
    GObject* object)
{
    GBinderServiceManagerAidl* self = GBINDER_SERVICEMANAGER_AIDL(object);
    GBinderServiceManagerAidlPriv* priv = self->priv;

    g_hash_table_destroy(priv->watch_table);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
gbinder_servicemanager_aidl_class_init(
    GBinderServiceManagerAidlClass* klass)
{
    GBinderServiceManagerClass* manager = GBINDER_SERVICEMANAGER_CLASS(klass);
    GObjectClass* object = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GBinderServiceManagerAidlPriv));
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
