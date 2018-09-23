/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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
#include "gbinder_log.h"

#include <gbinder_client.h>
#include <gbinder_local_request.h>
#include <gbinder_remote_reply.h>

#include <gutil_macros.h>

#include <errno.h>
#include <pthread.h>

typedef GBinderServiceManager GBinderDefaultServiceManager;
typedef GBinderServiceManagerClass GBinderDefaultServiceManagerClass;

G_DEFINE_TYPE(GBinderDefaultServiceManager,
    gbinder_defaultservicemanager,
    GBINDER_TYPE_SERVICEMANAGER)

enum gbinder_defaultservicemanager_calls {
    GET_SERVICE_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    CHECK_SERVICE_TRANSACTION,
    ADD_SERVICE_TRANSACTION,
    LIST_SERVICES_TRANSACTION
};

/* As a special case, ServiceManager's handle is zero */
#define DEFAULTSERVICEMANAGER_HANDLE (0)
#define DEFAULTSERVICEMANAGER_IFACE  "android.os.IServiceManager"

GBinderServiceManager*
gbinder_defaultservicemanager_new(
    const char* dev)
{
    return gbinder_servicemanager_new_with_type
        (gbinder_defaultservicemanager_get_type(), dev);
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
void
gbinder_defaultservicemanager_init(
    GBinderDefaultServiceManager* self)
{
}

static
void
gbinder_defaultservicemanager_class_init(
    GBinderDefaultServiceManagerClass* klass)
{
    klass->handle = DEFAULTSERVICEMANAGER_HANDLE;
    klass->iface = DEFAULTSERVICEMANAGER_IFACE;
    klass->default_device = GBINDER_DEFAULT_BINDER;
    klass->rpc_protocol = &gbinder_rpc_protocol_binder;

    klass->list = gbinder_defaultservicemanager_list;
    klass->get_service = gbinder_defaultservicemanager_get_service;
    klass->add_service = gbinder_defaultservicemanager_add_service;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
