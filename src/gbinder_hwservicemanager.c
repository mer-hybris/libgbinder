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
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
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
#include "gbinder_log.h"

#include <gbinder_client.h>
#include <gbinder_local_request.h>
#include <gbinder_remote_reply.h>
#include <gbinder_reader.h>

#include <errno.h>
#include <pthread.h>

typedef GBinderServiceManager GBinderHwServiceManager;
typedef GBinderServiceManagerClass GBinderHwServiceManagerClass;

G_DEFINE_TYPE(GBinderHwServiceManager,
    gbinder_hwservicemanager,
    GBINDER_TYPE_SERVICEMANAGER)

enum gbinder_hwservicemanager_calls {
    GET_TRANSACTION = GBINDER_FIRST_CALL_TRANSACTION,
    ADD_TRANSACTION,
    GET_TRANSPORT_TRANSACTION,
    LIST_TRANSACTION,
    LIST_BY_INTERFACE_TRANSACTION,
    REGISTER_FOR_NOTIFICATIONS_TRANSACTION,
    DEBUG_DUMP_TRANSACTION,
    REGISTER_PASSTHROUGH_CLIENT_TRANSACTION
};

/* As a special case, ServiceManager's handle is zero */
#define HWSERVICEMANAGER_HANDLE (0)
#define HWSERVICEMANAGER_IFACE  "android.hidl.manager@1.0::IServiceManager"

GBinderServiceManager*
gbinder_hwservicemanager_new(
    const char* dev)
{
    return gbinder_servicemanager_new_with_type
        (gbinder_hwservicemanager_get_type(), dev);
}

static
char**
gbinder_hwservicemanager_list(
    GBinderHwServiceManager* self)
{
    GBinderLocalRequest* req = gbinder_client_new_request(self->client);
    GBinderRemoteReply* reply = gbinder_client_transact_sync_reply
        (self->client, LIST_TRANSACTION, req, NULL);

    gbinder_local_request_unref(req);
    if (reply) {
        GBinderReader reader;
        char** result = NULL;
        int status = -1;

        gbinder_remote_reply_init_reader(reply, &reader);

        /* Read status */
        GVERIFY(gbinder_reader_read_int32(&reader, &status));
        GASSERT(status == GBINDER_STATUS_OK);

        /* Followed by hidl_vec<string> */
        result = gbinder_reader_read_hidl_string_vec(&reader);
        gbinder_remote_reply_unref(reply);
        return result;
    }
    return NULL;
}

static
GBinderRemoteObject*
gbinder_hwservicemanager_get_service(
    GBinderServiceManager* self,
    const char* fqinstance,
    int* status)
{
    /* e.g. "android.hardware.radio@1.1::IRadio/slot1" */
    const char* sep = strchr(fqinstance, '/');
    GBinderRemoteObject* obj = NULL;
    if (sep) {
        GBinderRemoteReply* reply;
        GBinderLocalRequest* req = gbinder_client_new_request(self->client);
        char* fqname = g_strndup(fqinstance, sep - fqinstance);
        const char* name = sep + 1;

        gbinder_local_request_append_hidl_string(req, fqname);
        gbinder_local_request_append_hidl_string(req, name);

        reply = gbinder_client_transact_sync_reply(self->client,
            GET_TRANSACTION, req, status);

        if (reply) {
            GBinderReader reader;
            int status = -1;

            gbinder_remote_reply_init_reader(reply, &reader);

            /* Read status */
            GVERIFY(gbinder_reader_read_int32(&reader, &status));
            GASSERT(status == GBINDER_STATUS_OK);

            /* Read the object */
            obj = gbinder_reader_read_object(&reader);
            gbinder_remote_reply_unref(reply);
        }

        gbinder_local_request_unref(req);
        g_free(fqname);
    } else {
        GERR("Invalid instance \"%s\"", fqinstance);
        if (status) *status = (-EINVAL);
    }
    return obj;
}

static
int
gbinder_hwservicemanager_add_service(
    GBinderServiceManager* self,
    const char* name,
    GBinderLocalObject* obj)
{
    int status;
    GBinderRemoteReply* reply;
    GBinderLocalRequest* req = gbinder_client_new_request(self->client);

    /* add(string name, interface service) generates (bool success); */
    gbinder_local_request_append_hidl_string(req, name);
    gbinder_local_request_append_local_object(req, obj);

    reply = gbinder_client_transact_sync_reply(self->client,
        ADD_TRANSACTION, req, &status);

    gbinder_remote_reply_unref(reply);
    gbinder_local_request_unref(req);
    return status;
}

static
void
gbinder_hwservicemanager_init(
    GBinderHwServiceManager* self)
{
}

static
void
gbinder_hwservicemanager_class_init(
    GBinderHwServiceManagerClass* klass)
{
    klass->handle = HWSERVICEMANAGER_HANDLE;
    klass->iface = HWSERVICEMANAGER_IFACE;
    klass->default_device = GBINDER_DEFAULT_HWBINDER;

    klass->list = gbinder_hwservicemanager_list;
    klass->get_service = gbinder_hwservicemanager_get_service;
    klass->add_service = gbinder_hwservicemanager_add_service;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
