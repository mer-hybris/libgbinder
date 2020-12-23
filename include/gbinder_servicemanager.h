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

#ifndef GBINDER_SERVICEMANAGER_H
#define GBINDER_SERVICEMANAGER_H

#include <gbinder_types.h>

G_BEGIN_DECLS

typedef
void
(*GBinderServiceManagerFunc)(
    GBinderServiceManager* sm,
    void* user_data);

/* GBinderServiceManagerListFunc callback returns TRUE to keep the services
 * list, otherwise the caller will deallocate it. */
typedef
gboolean
(*GBinderServiceManagerListFunc)(
    GBinderServiceManager* sm,
    char** services,
    void* user_data);

typedef
void
(*GBinderServiceManagerGetServiceFunc)(
    GBinderServiceManager* sm,
    GBinderRemoteObject* obj,
    int status,
    void* user_data);

typedef
void
(*GBinderServiceManagerAddServiceFunc)(
    GBinderServiceManager* sm,
    int status,
    void* user_data);

typedef
void
(*GBinderServiceManagerRegistrationFunc)(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data);

GBinderServiceManager*
gbinder_servicemanager_new(
    const char* dev);

GBinderServiceManager*
gbinder_defaultservicemanager_new(
    const char* dev)
    G_DEPRECATED_FOR(gbinder_servicemanager_new);

GBinderServiceManager*
gbinder_hwservicemanager_new(
    const char* dev)
    G_DEPRECATED_FOR(gbinder_servicemanager_new);

GBinderLocalObject*
gbinder_servicemanager_new_local_object(
    GBinderServiceManager* sm,
    const char* iface,
    GBinderLocalTransactFunc handler,
    void* user_data)
    G_GNUC_WARN_UNUSED_RESULT;

GBinderLocalObject*
gbinder_servicemanager_new_local_object2(
    GBinderServiceManager* sm,
    const char* const* ifaces,
    GBinderLocalTransactFunc handler,
    void* user_data) /* Since 1.0.29 */
    G_GNUC_WARN_UNUSED_RESULT;

GBinderServiceManager*
gbinder_servicemanager_ref(
    GBinderServiceManager* sm);

void
gbinder_servicemanager_unref(
    GBinderServiceManager* sm);

gboolean
gbinder_servicemanager_is_present(
    GBinderServiceManager* sm); /* Since 1.0.25 */

gboolean
gbinder_servicemanager_wait(
    GBinderServiceManager* sm,
    long max_wait_ms); /* Since 1.0.25 */

gulong
gbinder_servicemanager_list(
    GBinderServiceManager* sm,
    GBinderServiceManagerListFunc func,
    void* user_data);

char**
gbinder_servicemanager_list_sync(
    GBinderServiceManager* sm)
    G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_MALLOC;

gulong
gbinder_servicemanager_get_service(
    GBinderServiceManager* sm,
    const char* name,
    GBinderServiceManagerGetServiceFunc func,
    void* user_data);

GBinderRemoteObject* /* autoreleased */
gbinder_servicemanager_get_service_sync(
    GBinderServiceManager* sm,
    const char* name,
    int* status);

gulong
gbinder_servicemanager_add_service(
    GBinderServiceManager* sm,
    const char* name,
    GBinderLocalObject* obj,
    GBinderServiceManagerAddServiceFunc func,
    void* user_data);

int
gbinder_servicemanager_add_service_sync(
    GBinderServiceManager* sm,
    const char* name,
    GBinderLocalObject* obj);

void
gbinder_servicemanager_cancel(
    GBinderServiceManager* sm,
    gulong id);

gulong
gbinder_servicemanager_add_presence_handler(
    GBinderServiceManager* sm,
    GBinderServiceManagerFunc func,
    void* user_data); /* Since 1.0.25 */

gulong
gbinder_servicemanager_add_registration_handler(
    GBinderServiceManager* sm,
    const char* name,
    GBinderServiceManagerRegistrationFunc func,
    void* user_data); /* Since 1.0.13 */

void
gbinder_servicemanager_remove_handler(
    GBinderServiceManager* sm,
    gulong id); /* Since 1.0.13 */

void
gbinder_servicemanager_remove_handlers(
    GBinderServiceManager* sm,
    gulong* ids,
    guint count); /* Since 1.0.25 */

#define gbinder_servicemanager_remove_all_handlers(r,ids) \
    gbinder_servicemanager_remove_handlers(sm, ids, G_N_ELEMENTS(ids))

G_END_DECLS

#endif /* GBINDER_SERVICEMANAGER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
