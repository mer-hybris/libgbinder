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

#ifndef GBINDER_SERVICEMANAGER_PRIVATE_H
#define GBINDER_SERVICEMANAGER_PRIVATE_H

#include "gbinder_types_p.h"

#include <gbinder_servicemanager.h>

#include <glib-object.h>

/* As a special case, ServiceManager's handle is zero */
#define GBINDER_SERVICEMANAGER_HANDLE (0)

typedef struct gbinder_servicemanager_priv GBinderServiceManagerPriv;

typedef struct gbinder_servicemanager {
    GObject parent;
    GBinderServiceManagerPriv* priv;
    const char* dev;
    GBinderClient* client;
} GBinderServiceManager;

typedef enum gbinder_servicemanager_name_check {
    GBINDER_SERVICEMANAGER_NAME_OK,
    GBINDER_SERVICEMANAGER_NAME_NORMALIZE,
    GBINDER_SERVICEMANAGER_NAME_INVALID,
} GBINDER_SERVICEMANAGER_NAME_CHECK;

typedef struct gbinder_servicemanager_class {
    GObjectClass parent;
    GMutex mutex;
    GHashTable* table;

    const char* iface;
    const char* default_device;

    /* Methods (synchronous) */
    char** (*list)(GBinderServiceManager* self);
    GBinderRemoteObject* (*get_service)
        (GBinderServiceManager* self, const char* name, int* status);
    int (*add_service)
        (GBinderServiceManager* self, const char* name,
            GBinderLocalObject* obj);

    /* Checking/normalizing watch names */
    GBINDER_SERVICEMANAGER_NAME_CHECK (*check_name)
        (GBinderServiceManager* self, const char* name);
    char* (*normalize_name)(GBinderServiceManager* self, const char* name);

    /* If watch() returns FALSE, unwatch() is not called */
    gboolean (*watch)(GBinderServiceManager* self, const char* name);
    void (*unwatch)(GBinderServiceManager* self, const char* name);
} GBinderServiceManagerClass;

GType gbinder_servicemanager_get_type(void) GBINDER_INTERNAL;
#define GBINDER_TYPE_SERVICEMANAGER (gbinder_servicemanager_get_type())
#define GBINDER_SERVICEMANAGER_CLASS(klass) \
    G_TYPE_CHECK_CLASS_CAST((klass), GBINDER_TYPE_SERVICEMANAGER, \
    GBinderServiceManagerClass)

GBinderServiceManager*
gbinder_servicemanager_new_with_type(
    GType type,
    const char* dev)
    GBINDER_INTERNAL;

void
gbinder_servicemanager_service_registered(
    GBinderServiceManager* self,
    const char* name)
    GBINDER_INTERNAL;

/* Declared for unit tests */
void
gbinder_servicemanager_exit(
    void)
    GBINDER_INTERNAL
    GBINDER_DESTRUCTOR;

/* Derived types */

GType gbinder_servicemanager_aidl_get_type(void) GBINDER_INTERNAL;
GType gbinder_servicemanager_aidl2_get_type(void) GBINDER_INTERNAL;
GType gbinder_servicemanager_hidl_get_type(void) GBINDER_INTERNAL;

#endif /* GBINDER_SERVICEMANAGER_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
