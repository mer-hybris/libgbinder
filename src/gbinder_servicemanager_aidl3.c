/*
 * Copyright (C) 2021-2022 Jolla Ltd.
 * Copyright (C) 2021-2022 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2021 Gary Wang <gary.wang@canonical.com>
 * Copyright (C) 2021 Madhushan Nishantha <jlmadushan@gmail.com>
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
#include "gbinder_client_p.h"

#include <gbinder_local_request.h>
#include <gbinder_remote_reply.h>

/* Variant of AIDL servicemanager appeared in Android 11 (API level 30) */

typedef GBinderServiceManagerAidl GBinderServiceManagerAidl3;
typedef GBinderServiceManagerAidlClass GBinderServiceManagerAidl3Class;

G_DEFINE_TYPE(GBinderServiceManagerAidl3,
    gbinder_servicemanager_aidl3,
    GBINDER_TYPE_SERVICEMANAGER_AIDL2)

#define GET_THIS_CLASS(obj) GBINDER_SERVICEMANAGER_AIDL_GET_CLASS(obj)

static
GBinderRemoteObject*
gbinder_servicemanager_aidl3_get_service(
    GBinderServiceManager* manager,
    const char* name,
    int* status,
    const GBinderIpcSyncApi* api)
{
    GBinderClient* client = manager->client;
    GBinderLocalRequest* req = gbinder_client_new_request(client);
    GBinderRemoteObject* obj;
    GBinderRemoteReply* reply;
    GBinderReader reader;
    GBinderServiceManagerAidlClass* klass = GET_THIS_CLASS(manager);

    gbinder_local_request_append_string16(req, name);
    reply = gbinder_client_transact_sync_reply2(client,
        klass->check_service_transaction, req, status, api);

    gbinder_remote_reply_init_reader(reply, &reader);
    gbinder_reader_read_int32(&reader, NULL /* status? */);
    obj = gbinder_reader_read_object(&reader);

    gbinder_remote_reply_unref(reply);
    gbinder_local_request_unref(req);
    return obj;
}

static
char**
gbinder_servicemanager_aidl3_list(
    GBinderServiceManager* manager,
    const GBinderIpcSyncApi* api)
{
    GPtrArray* list = g_ptr_array_new();
    GBinderClient* client = manager->client;
    GBinderRemoteReply* reply;
    GBinderLocalRequest* req = gbinder_client_new_request(client);
    GBinderServiceManagerAidlClass* klass = GET_THIS_CLASS(manager);

    /*
     * Starting from Android 11, no `index` field is required but
     * only with `dump priority` field to request to list services.
     * As a result, a vector of strings which stands for service
     * list is given in the binder response.
     */
    gbinder_local_request_append_int32(req, DUMP_FLAG_PRIORITY_ALL);
    reply = gbinder_client_transact_sync_reply2(client,
        klass->list_services_transaction, req, NULL, api);

    if (reply) {
        GBinderReader reader;
        gint32 count;

        gbinder_remote_reply_init_reader(reply, &reader);
        gbinder_reader_read_int32(&reader, NULL /* status */);
        if (gbinder_reader_read_int32(&reader, &count)) {
            int i;

            /*
             * Iterate each service name. Don't trust the count, stop
             * when the data runs out. A NULL in the middle would also
             * truncate the strv and leak the strings after it.
             */
            for (i = 0; i < count; i++) {
                char* name = gbinder_reader_read_string16(&reader);

                if (!name) {
                    break;
                }
                g_ptr_array_add(list, name);
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
gbinder_servicemanager_aidl3_init(
    GBinderServiceManagerAidl3* self)
{
}

static
void
gbinder_servicemanager_aidl3_class_init(
    GBinderServiceManagerAidl3Class* klass)
{
    GBinderServiceManagerClass* manager = GBINDER_SERVICEMANAGER_CLASS(klass);

    gbinder_servicemanager_class_common_init(manager);
    manager->list = gbinder_servicemanager_aidl3_list;
    manager->get_service = gbinder_servicemanager_aidl3_get_service;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
