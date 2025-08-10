/*
 * Copyright (C) 2021-2022 Jolla Ltd.
 * Copyright (C) 2021-2022 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2026 Jolla Mobile Ltd
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

#include <gutil_log.h>

/* Variant of AIDL servicemanager appeared in Android 16 (API level 36) */

typedef GBinderServiceManagerAidl GBinderServiceManagerAidl6;
typedef GBinderServiceManagerAidlClass GBinderServiceManagerAidl6Class;

G_DEFINE_TYPE(GBinderServiceManagerAidl6,
    gbinder_servicemanager_aidl6,
    GBINDER_TYPE_SERVICEMANAGER_AIDL)

#define PARENT_CLASS gbinder_servicemanager_aidl6_parent_class

GBinderRemoteObject*
gbinder_servicemanager_aidl6_get_service(
    GBinderServiceManager* self,
    const char* name,
    int* status,
    const GBinderIpcSyncApi* api)
{
    return gbinder_servicemanager_aidl3_get_service_internal(self, name,
        status, api, AIDL6_CHECK_SERVICE_TRANSACTION);
}

char**
gbinder_servicemanager_aidl6_list(
    GBinderServiceManager* manager,
    const GBinderIpcSyncApi* api)
{
    return gbinder_servicemanager_aidl3_list_internal(manager, api,
        AIDL6_LIST_SERVICES_TRANSACTION);
}

static
int
gbinder_servicemanager_aidl6_add_service(
    GBinderServiceManager* manager,
    const char* name,
    GBinderLocalObject* obj,
    const GBinderIpcSyncApi* api)
{
    return gbinder_servicemanager_aidl_add_service_internal(manager, name, obj,
        api, AIDL6_ADD_SERVICE_TRANSACTION);
}

static
void
gbinder_servicemanager_aidl6_init(
    GBinderServiceManagerAidl6* self)
{
}

static
void
gbinder_servicemanager_aidl6_class_init(
    GBinderServiceManagerAidl6Class* klass)
{
    GBinderServiceManagerClass* manager = GBINDER_SERVICEMANAGER_CLASS(klass);

    klass->add_service_req = gbinder_servicemanager_aidl2_add_service_req;
    manager->list = gbinder_servicemanager_aidl6_list;
    manager->get_service = gbinder_servicemanager_aidl6_get_service;
    manager->add_service = gbinder_servicemanager_aidl6_add_service;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
