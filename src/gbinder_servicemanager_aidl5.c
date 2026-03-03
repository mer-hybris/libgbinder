/*
 * Copyright (C) 2021-2022 Jolla Ltd.
 * Copyright (C) 2021-2022 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2021 Gary Wang <gary.wang@canonical.com>
 * Copyright (C) 2021 Madhushan Nishantha <jlmadushan@gmail.com>
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

#include "gbinder_servicemanager_aidl_p.h"
#include "gbinder_client_p.h"
#include "gbinder_reader_p.h"

#include <gbinder_local_request.h>
#include <gbinder_remote_reply.h>

/* Variant of AIDL servicemanager appeared in Android 15 (API level 35) */

enum gbinder_servicemanager_aidl5_calls {
    GET_SERVICE_TRANSACTION_AIDL5 = GBINDER_FIRST_CALL_TRANSACTION + 0,
    GET_SERVICE2_TRANSACTION_AIDL5,
    CHECK_SERVICE_TRANSACTION_AIDL5,
    ADD_SERVICE_TRANSACTION_AIDL5,
    LIST_SERVICES_TRANSACTION_AIDL5
};

typedef GBinderServiceManagerAidl GBinderServiceManagerAidl5;
typedef GBinderServiceManagerAidlClass GBinderServiceManagerAidl5Class;

G_DEFINE_TYPE(GBinderServiceManagerAidl5,
    gbinder_servicemanager_aidl5,
    GBINDER_TYPE_SERVICEMANAGER_AIDL)

#define PARENT_CLASS gbinder_servicemanager_aidl5_parent_class

#define BINDER_WIRE_FORMAT_VERSION (1)

GBinderRemoteObject*
gbinder_servicemanager_aidl5_get_service(
    GBinderServiceManager* self,
    const char* name,
    int* status,
    const GBinderIpcSyncApi* api)
{
    GBinderClient* client = self->client;
    GBinderLocalRequest* req = gbinder_client_new_request(client);
    GBinderRemoteObject* obj = NULL;
    GBinderRemoteReply* reply;
    GBinderReader reader;
    gint32 ex = 0;
    gint32 tag = -1;

    gbinder_local_request_append_string16(req, name);
    reply = gbinder_client_transact_sync_reply2(client,
        CHECK_SERVICE_TRANSACTION_AIDL5, req, status, api);

    gbinder_remote_reply_init_reader(reply, &reader);
    if (gbinder_reader_read_int32(&reader, &ex) &&
        gbinder_reader_read_int32(&reader, &tag)) {
        if (tag == 0) {
            /* serviceWithMetadata */
            if (gbinder_reader_read_int32(&reader, NULL)) {
                obj = gbinder_reader_read_object(&reader);
                gbinder_reader_read_int32(&reader, NULL);
            }
        } else if (tag == 1) {
            /* accessor */
            obj = gbinder_reader_read_object(&reader);
        }
    }

    gbinder_remote_reply_unref(reply);
    gbinder_local_request_unref(req);
    return obj;
}

char**
gbinder_servicemanager_aidl5_list(
    GBinderServiceManager* manager,
    const GBinderIpcSyncApi* api)
{
    GPtrArray* list = g_ptr_array_new();
    GBinderClient* client = manager->client;
    GBinderRemoteReply* reply;
    GBinderLocalRequest* req = gbinder_client_new_request(client);

    /*
     * Starting from Android 11, no `index` field is required but
     * only with `dump priority` field to request to list services.
     * As a result, a vector of strings which stands for service
     * list is given in the binder response.
     */
    gbinder_local_request_append_int32(req, DUMP_FLAG_PRIORITY_ALL);
    reply = gbinder_client_transact_sync_reply2(client,
        LIST_SERVICES_TRANSACTION_AIDL5, req, NULL, api);

    if (reply) {
        GBinderReader reader;
        gint32 count;

        gbinder_remote_reply_init_reader(reply, &reader);
        gbinder_reader_read_int32(&reader, NULL /* status */);
        if (gbinder_reader_read_int32(&reader, &count)) {
            int i;

            /* Iterate each service name */
            for (i = 0; i < count; i++) {
                g_ptr_array_add(list, gbinder_reader_read_string16(&reader));
            }
        }
        gbinder_remote_reply_unref(reply);
    }

    gbinder_local_request_unref(req);
    g_ptr_array_add(list, NULL);
    return (char**)g_ptr_array_free(list, FALSE);
}

static
GBinderLocalRequest*
gbinder_servicemanager_aidl5_add_service_req(
    GBinderClient* client,
    const char* name,
    GBinderLocalObject* obj)
{
    GBinderLocalRequest* req = gbinder_client_new_request(client);

    gbinder_local_request_append_string16(req, name);
    gbinder_local_request_append_local_object(req, obj);
    gbinder_local_request_append_int32(req, 0);
    gbinder_local_request_append_int32(req, DUMP_FLAG_PRIORITY_DEFAULT);
    return req;
}

static
int
gbinder_servicemanager_aidl5_add_service(
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
        ADD_SERVICE_TRANSACTION_AIDL5, req, &status, api);

    gbinder_remote_reply_unref(reply);
    gbinder_local_request_unref(req);
    return status;
}

static
void
gbinder_servicemanager_aidl5_init(
    GBinderServiceManagerAidl5* self)
{
}

static
void
gbinder_servicemanager_aidl5_class_init(
    GBinderServiceManagerAidl5Class* klass)
{
    GBinderServiceManagerClass* manager = GBINDER_SERVICEMANAGER_CLASS(klass);

    klass->add_service_req = gbinder_servicemanager_aidl5_add_service_req;
    manager->list = gbinder_servicemanager_aidl5_list;
    manager->get_service = gbinder_servicemanager_aidl5_get_service;
    manager->add_service = gbinder_servicemanager_aidl5_add_service;

}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
