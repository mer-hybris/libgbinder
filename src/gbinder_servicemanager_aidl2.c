/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include <gbinder_client.h>
#include <gbinder_local_request.h>

/* Variant of AIDL servicemanager appeared in Android 9 (API level 28) */

typedef GBinderServiceManagerAidl GBinderServiceManagerAidl2;
typedef GBinderServiceManagerAidlClass GBinderServiceManagerAidl2Class;

G_DEFINE_TYPE(GBinderServiceManagerAidl2,
    gbinder_servicemanager_aidl2,
    GBINDER_TYPE_SERVICEMANAGER_AIDL)

#define PARENT_CLASS gbinder_servicemanager_aidl2_parent_class
#define DUMP_FLAG_PRIORITY_DEFAULT (0x08)
#define DUMP_FLAG_PRIORITY_ALL     (0x0f)

static
GBinderLocalRequest*
gbinder_servicemanager_aidl2_list_services_req(
    GBinderClient* client,
    gint32 index)
{
    GBinderLocalRequest* req = gbinder_client_new_request(client);

    gbinder_local_request_append_int32(req, index);
    gbinder_local_request_append_int32(req, DUMP_FLAG_PRIORITY_ALL);
    return req;
}

static
GBinderLocalRequest*
gbinder_servicemanager_aidl2_add_service_req(
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
void
gbinder_servicemanager_aidl2_init(
    GBinderServiceManagerAidl* self)
{
}

static
void
gbinder_servicemanager_aidl2_class_init(
    GBinderServiceManagerAidl2Class* cls)
{
    cls->list_services_req = gbinder_servicemanager_aidl2_list_services_req;
    cls->add_service_req = gbinder_servicemanager_aidl2_add_service_req;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
