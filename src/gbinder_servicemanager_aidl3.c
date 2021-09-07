/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2021 Gary Wang <gary.wang@canonical.com>
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

#include "gbinder_servicemanager_aidl.h"
#include "gbinder_client_p.h"
#include "gbinder_reader_p.h"
#include "gbinder_log.h"

#include <gbinder_local_request.h>
#include <gbinder_remote_reply.h>

enum gbinder_stability_level {
    UNDECLARED = 0,
    VENDOR = 0b000011,
    SYSTEM = 0b001100,
    VINTF = 0b111111,
};

/* Variant of AIDL servicemanager appeared in Android 11 (API level 30) */

typedef GBinderServiceManagerAidl GBinderServiceManagerAidl3;
typedef GBinderServiceManagerAidlClass GBinderServiceManagerAidl3Class;

G_DEFINE_TYPE(GBinderServiceManagerAidl3,
    gbinder_servicemanager_aidl3,
    GBINDER_TYPE_SERVICEMANAGER_AIDL)

#define PARENT_CLASS gbinder_servicemanager_aidl3_parent_class

static
GBinderLocalRequest*
gbinder_servicemanager_aidl3_list_services_req(
    GBinderClient* client,
    gint32 index)
{
    (void) index;
    GBinderLocalRequest* req = gbinder_client_new_request(client);
    // Starting from Android 11, no `index` field is required but
    // only with `dump priority` field to request to list services.
    // As a result, a vector of strings which stands for service
    // list is given in the binder response.
    gbinder_local_request_append_int32(req, DUMP_FLAG_PRIORITY_ALL);
    return req;
}

static
GBinderLocalRequest*
gbinder_servicemanager_aidl3_add_service_req(
    GBinderClient* client,
    const char* name,
    GBinderLocalObject* obj)
{
    GBinderLocalRequest* req = gbinder_client_new_request(client);

    gbinder_local_request_append_string16(req, name);
    gbinder_local_request_append_local_object(req, obj);
    // Starting from Android 11, to add a service, Android framework requires
    // an additional field `stability` when reading a strong binder.
    gbinder_local_request_append_int32(req, SYSTEM);
    gbinder_local_request_append_int32(req, 0);
    gbinder_local_request_append_int32(req, DUMP_FLAG_PRIORITY_DEFAULT);
    return req;
}

static
void
gbinder_servicemanager_aidl3_init(
    GBinderServiceManagerAidl* self)
{
}

static
char**
gbinder_servicemanager_aidl3_list(
    GBinderServiceManager* manager,
    const GBinderIpcSyncApi* api)
{
    GPtrArray* list = g_ptr_array_new();
    GBinderClient* client = manager->client;
    GBinderServiceManagerAidlClass* klass =
        GBINDER_SERVICEMANAGER_AIDL_GET_CLASS(manager);

    GBinderLocalRequest* req = klass->list_services_req(client, 0);
    GBinderRemoteReply* reply = NULL;

    int status;
    if ((reply = gbinder_client_transact_sync_reply2(client,
        LIST_SERVICES_TRANSACTION, req, &status, api)) != NULL) {

        GBinderReader reader;
        gbinder_remote_reply_init_reader(reply, &reader);
        /* Read status */
        GVERIFY(gbinder_reader_read_int32(&reader, &status));
        /* Read size */
        int srv_size = 0;
        GVERIFY(gbinder_reader_read_int32(&reader, &srv_size));
        /* Iterate each service name */
        for (int i = 0; i < srv_size; i++) {
            char* service = gbinder_reader_read_string16(&reader);
            if (service) {
                g_ptr_array_add(list, service);
            }
        }
        gbinder_remote_reply_unref(reply);
    }

    gbinder_local_request_unref(req);
    g_ptr_array_add(list, NULL);
    return (char**)g_ptr_array_free(list, FALSE);
}

static
void
gbinder_servicemanager_aidl3_class_init(
    GBinderServiceManagerAidl3Class* cls)
{
    GBinderServiceManagerClass* manager = GBINDER_SERVICEMANAGER_CLASS(cls);
    cls->list_services_req = gbinder_servicemanager_aidl3_list_services_req;
    cls->add_service_req = gbinder_servicemanager_aidl3_add_service_req;
    manager->list = gbinder_servicemanager_aidl3_list;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

