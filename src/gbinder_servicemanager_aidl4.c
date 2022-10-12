/*
 * Copyright (C) 2020-2022 Jolla Ltd.
 * Copyright (C) 2020-2022 Slava Monich <slava.monich@jolla.com>
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

/* Variant of AIDL servicemanager appeared in Android 12 (API level 31) */

typedef GBinderServiceManagerAidl GBinderServiceManagerAidl4;
typedef GBinderServiceManagerAidlClass GBinderServiceManagerAidl4Class;

G_DEFINE_TYPE(GBinderServiceManagerAidl4,
    gbinder_servicemanager_aidl4,
    GBINDER_TYPE_SERVICEMANAGER_AIDL)

#define PARENT_CLASS gbinder_servicemanager_aidl4_parent_class

#define BINDER_WIRE_FORMAT_VERSION (1)

static
GBinderLocalRequest*
gbinder_servicemanager_aidl4_add_service_req(
    GBinderClient* client,
    const char* name,
    GBinderLocalObject* obj)
{
    GBinderLocalRequest* req = gbinder_client_new_request(client);

    gbinder_local_request_append_string16(req, name);
    gbinder_local_request_append_local_object(req, obj);

    /*
     * When reading nullable strong binder, from Android 12, the format of
     * the `stability` field passed on the wire was changed and evolved to
     * `struct Category`, which consists of the following members with 4 bytes
     * long.
     *
     * struct Category {
     *   uint8_t version;
     *   uint8_t reserved[2];
     *   Level level;        <- bitmask of Stability::Level
     * }
     *
     * Hmmm, is that ^ really true?
     */
    gbinder_local_request_append_int32(req,
        GBINDER_FOURCC(GBINDER_STABILITY_SYSTEM, 0, 0,
            BINDER_WIRE_FORMAT_VERSION));
    gbinder_local_request_append_int32(req, 0);
    gbinder_local_request_append_int32(req, DUMP_FLAG_PRIORITY_DEFAULT);

    return req;
}

static
void
gbinder_servicemanager_aidl4_init(
    GBinderServiceManagerAidl* self)
{
}

static
void
gbinder_servicemanager_aidl4_class_init(
    GBinderServiceManagerAidl4Class* cls)
{
    GBinderServiceManagerClass* manager = GBINDER_SERVICEMANAGER_CLASS(cls);
    cls->add_service_req = gbinder_servicemanager_aidl4_add_service_req;
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

