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

#include "gbinder_servicemanager_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_servicepoll.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_log.h"

#include <gbinder_client.h>
#include <gbinder_local_request.h>
#include <gbinder_remote_reply.h>

#include <gutil_macros.h>

#include <errno.h>
#include <pthread.h>

typedef struct gbinder_defaultservicemanager_watch {
    GBinderServicePoll* poll;
    char* name;
    gulong handler_id;
    GBinderEventLoopTimeout* notify;
} GBinderDefaultServiceManagerWatch;

typedef GBinderServiceManagerClass GBinderDefaultServiceManagerClass;
typedef struct gbinder_defaultservicemanager {
    GBinderServiceManager manager;
    GBinderServicePoll* poll;
    GHashTable* watch_table;
} GBinderDefaultServiceManager;

G_DEFINE_TYPE(GBinderDefaultServiceManager,
    gbinder_defaultservicemanager,
    GBINDER_TYPE_SERVICEMANAGER)

#define PARENT_CLASS gbinder_defaultservicemanager_parent_class
#define GBINDER_TYPE_DEFAULTSERVICEMANAGER \
    gbinder_defaultservicemanager_get_type()
#define GBINDER_DEFAULTSERVICEMANAGER(obj) \
    G_TYPE_CHECK_INSTANCE_CAST((obj), GBINDER_TYPE_DEFAULTSERVICEMANAGER, \
    GBinderDefaultServiceManager)

enum gbinder_defaultservicemanager_calls {
    GET_SERVICE_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    CHECK_SERVICE_TRANSACTION,
    ADD_SERVICE_TRANSACTION,
    LIST_SERVICES_TRANSACTION
};

#define DEFAULTSERVICEMANAGER_IFACE  "android.os.IServiceManager"

GBinderServiceManager*
gbinder_defaultservicemanager_new(
    const char* dev)
{
    return gbinder_servicemanager_new_with_type
        (GBINDER_TYPE_DEFAULTSERVICEMANAGER, dev);
}

static
void
gbinder_defaultservicemanager_watch_proc(
    GBinderServicePoll* poll,
    const char* name_added,
    void* user_data)
{
    GBinderDefaultServiceManagerWatch* watch = user_data;

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
gbinder_defaultservicemanager_watch_notify(
    gpointer user_data)
{
    GBinderDefaultServiceManagerWatch* watch = user_data;
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
gbinder_defaultservicemanager_watch_free(
    gpointer user_data)
{
    GBinderDefaultServiceManagerWatch* watch = user_data;

    gbinder_timeout_remove(watch->notify);
    gbinder_servicepoll_remove_handler(watch->poll, watch->handler_id);
    gbinder_servicepoll_unref(watch->poll);
    g_free(watch->name);
    g_slice_free(GBinderDefaultServiceManagerWatch, watch);
}

static
GBinderDefaultServiceManagerWatch*
gbinder_defaultservicemanager_watch_new(
    GBinderDefaultServiceManager* manager,
    const char* name)
{
    GBinderDefaultServiceManagerWatch* watch =
        g_slice_new0(GBinderDefaultServiceManagerWatch);

    watch->name = g_strdup(name);
    watch->poll = gbinder_servicepoll_new(&manager->manager, &manager->poll);
    watch->handler_id = gbinder_servicepoll_add_handler(watch->poll,
        gbinder_defaultservicemanager_watch_proc, watch);
    return watch;
}

static
GBinderLocalRequest*
gbinder_servicemanager_list_services_req(
    GBinderServiceManager* self,
    gint32 index)
{
    return gbinder_local_request_append_int32
        (gbinder_client_new_request(self->client), index);
}

static
char**
gbinder_defaultservicemanager_list(
    GBinderServiceManager* self)
{
    GPtrArray* list = g_ptr_array_new();
    GBinderLocalRequest* req = gbinder_servicemanager_list_services_req(self,0);
    GBinderRemoteReply* reply;

    while ((reply = gbinder_client_transact_sync_reply(self->client,
        LIST_SERVICES_TRANSACTION, req, NULL)) != NULL) {
        char* service = gbinder_remote_reply_read_string16(reply);

        gbinder_remote_reply_unref(reply);
        if (service) {
            g_ptr_array_add(list, service);
            gbinder_local_request_unref(req);
            req = gbinder_servicemanager_list_services_req(self, list->len);
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
gbinder_defaultservicemanager_get_service(
    GBinderServiceManager* self,
    const char* name,
    int* status)
{
    GBinderRemoteObject* obj;
    GBinderRemoteReply* reply;
    GBinderLocalRequest* req = gbinder_client_new_request(self->client);

    gbinder_local_request_append_string16(req, name);
    reply = gbinder_client_transact_sync_reply(self->client,
        CHECK_SERVICE_TRANSACTION, req, status);

    obj = gbinder_remote_reply_read_object(reply);
    gbinder_remote_reply_unref(reply);
    gbinder_local_request_unref(req);
    return obj;
}

static
int
gbinder_defaultservicemanager_add_service(
    GBinderServiceManager* self,
    const char* name,
    GBinderLocalObject* obj)
{
    int status;
    GBinderRemoteReply* reply;
    GBinderLocalRequest* req = gbinder_client_new_request(self->client);

    gbinder_local_request_append_string16(req, name);
    gbinder_local_request_append_local_object(req, obj);
    gbinder_local_request_append_int32(req, 0);

    reply = gbinder_client_transact_sync_reply(self->client,
        ADD_SERVICE_TRANSACTION, req, &status);

    gbinder_remote_reply_unref(reply);
    gbinder_local_request_unref(req);
    return status;
}

static
GBINDER_SERVICEMANAGER_NAME_CHECK
gbinder_defaultservicemanager_check_name(
    GBinderServiceManager* self,
    const char* name)
{
    return GBINDER_SERVICEMANAGER_NAME_OK;
}

static
gboolean
gbinder_defaultservicemanager_watch(
    GBinderServiceManager* manager,
    const char* name)
{
    GBinderDefaultServiceManager* self = GBINDER_DEFAULTSERVICEMANAGER(manager);
    GBinderDefaultServiceManagerWatch* watch =
        gbinder_defaultservicemanager_watch_new(self, name);

    g_hash_table_replace(self->watch_table, watch->name, watch);
    if (gbinder_servicepoll_is_known_name(watch->poll, name)) {
        watch->notify = gbinder_idle_add
            (gbinder_defaultservicemanager_watch_notify, watch);
    }
    return TRUE;
}

static
void
gbinder_defaultservicemanager_unwatch(
    GBinderServiceManager* manager,
    const char* name)
{
    g_hash_table_remove(GBINDER_DEFAULTSERVICEMANAGER(manager)->watch_table,
        name);
}

static
void
gbinder_defaultservicemanager_init(
    GBinderDefaultServiceManager* self)
{
    self->watch_table = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, gbinder_defaultservicemanager_watch_free);
}

static
void
gbinder_defaultservicemanager_finalize(
    GObject* object)
{
    GBinderDefaultServiceManager* self = GBINDER_DEFAULTSERVICEMANAGER(object);

    g_hash_table_destroy(self->watch_table);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
gbinder_defaultservicemanager_class_init(
    GBinderDefaultServiceManagerClass* klass)
{
    klass->iface = DEFAULTSERVICEMANAGER_IFACE;
    klass->default_device = GBINDER_DEFAULT_BINDER;
    klass->rpc_protocol = &gbinder_rpc_protocol_binder;

    klass->list = gbinder_defaultservicemanager_list;
    klass->get_service = gbinder_defaultservicemanager_get_service;
    klass->add_service = gbinder_defaultservicemanager_add_service;
    klass->check_name = gbinder_defaultservicemanager_check_name;
    /* normalize_name is not needed */
    klass->watch = gbinder_defaultservicemanager_watch;
    klass->unwatch = gbinder_defaultservicemanager_unwatch;
    G_OBJECT_CLASS(klass)->finalize = gbinder_defaultservicemanager_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
