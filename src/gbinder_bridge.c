/*
 * Copyright (C) 2021 Jolla Ltd.
 * Copyright (C) 2021 Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_local_request.h"
#include "gbinder_local_reply.h"
#include "gbinder_proxy_object.h"
#include "gbinder_remote_request_p.h"
#include "gbinder_remote_reply.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_servicename.h"
#include "gbinder_servicemanager_p.h"
#include "gbinder_client_p.h"
#include "gbinder_bridge.h"
#include "gbinder_ipc.h"
#include "gbinder_log.h"

#include <gutil_strv.h>
#include <gutil_macros.h>

#include <errno.h>

typedef struct gbinder_bridge_interface {
    GBinderBridge* bridge;
    char* iface;
    char* fqname;
    char* src_name;
    char* dest_name;
    gulong dest_watch_id;
    gulong dest_death_id;
    GBinderRemoteObject* dest_obj;
    GBinderServiceName* src_service;
    GBinderProxyObject* proxy;
} GBinderBridgeInterface;

struct gbinder_bridge {
    GBinderBridgeInterface** ifaces;
    GBinderServiceManager* src;
    GBinderServiceManager* dest;
};

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
gbinder_bridge_dest_drop_remote_object(
    GBinderBridgeInterface* bi)
{
    if (bi->dest_obj) {
        GDEBUG("Detached from %s", bi->fqname);
        gbinder_remote_object_remove_handler(bi->dest_obj, bi->dest_death_id);
        gbinder_remote_object_unref(bi->dest_obj);
        bi->dest_death_id = 0;
        bi->dest_obj = NULL;
    }
}

static
void
gbinder_bridge_interface_deactivate(
    GBinderBridgeInterface* bi)
{
    gbinder_bridge_dest_drop_remote_object(bi);
    if (bi->proxy) {
        gbinder_local_object_drop(GBINDER_LOCAL_OBJECT(bi->proxy));
        bi->proxy = NULL;
    }
    if (bi->src_service) {
        gbinder_servicename_unref(bi->src_service);
        bi->src_service = NULL;
    }
}

static
void
gbinder_bridge_interface_free(
    GBinderBridgeInterface* bi)
{
    GBinderBridge* bridge = bi->bridge;

    gbinder_bridge_interface_deactivate(bi);
    gbinder_servicemanager_remove_handler(bridge->dest, bi->dest_watch_id);
    g_free(bi->iface);
    g_free(bi->fqname);
    g_free(bi->src_name);
    g_free(bi->dest_name);
    gutil_slice_free(bi);
}

static
void
gbinder_bridge_dest_death_proc(
    GBinderRemoteObject* obj,
    void* user_data)
{
    GBinderBridgeInterface* bi = user_data;

    GDEBUG("%s has died", bi->fqname);
    gbinder_bridge_interface_deactivate(bi);
}

static
void
gbinder_bridge_interface_activate(
    GBinderBridgeInterface* bi)
{
    GBinderBridge* bridge = bi->bridge;
    GBinderServiceManager* src = bridge->src;
    GBinderServiceManager* dest = bridge->dest;

    if (bi->dest_obj && bi->dest_obj->dead) {
        gbinder_bridge_dest_drop_remote_object(bi);
    }
    if (!bi->dest_obj) {
        bi->dest_obj = gbinder_servicemanager_get_service_sync(dest,
            bi->fqname, NULL);
        if (bi->dest_obj) {
            GDEBUG("Attached to %s", bi->fqname);
            gbinder_remote_object_ref(bi->dest_obj);
            bi->dest_death_id = gbinder_remote_object_add_death_handler
                (bi->dest_obj, gbinder_bridge_dest_death_proc, bi);
        }
    }
    if (bi->dest_obj && !bi->proxy) {
        bi->proxy = gbinder_proxy_object_new(gbinder_servicemanager_ipc(src),
            bi->dest_obj);
    }
    if (bi->proxy && !bi->src_service) {
        bi->src_service = gbinder_servicename_new(src,
            GBINDER_LOCAL_OBJECT(bi->proxy), bi->src_name);
    }
}

static
void
gbinder_bridge_dest_registration_proc(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data)
{
    GBinderBridgeInterface* bi = user_data;

    if (!g_strcmp0(name, bi->fqname)) {
        GDEBUG("%s has been registered", bi->fqname);
        gbinder_bridge_interface_activate(bi);
    }
}

static
GBinderBridgeInterface*
gbinder_bridge_interface_new(
    GBinderBridge* self,
    const char* src_name,
    const char* dest_name,
    const char* iface)
{
    GBinderBridgeInterface* bi = g_slice_new0(GBinderBridgeInterface);

    bi->bridge = self;
    bi->iface = g_strdup(iface);
    bi->fqname = g_strconcat(iface, "/", dest_name, NULL);
    bi->src_name = g_strdup(src_name);
    bi->dest_name = g_strdup(dest_name);
    bi->dest_watch_id = gbinder_servicemanager_add_registration_handler
        (self->dest, bi->fqname, gbinder_bridge_dest_registration_proc, bi);

    /* Try to activate at startup */
    gbinder_bridge_interface_activate(bi);
    return bi;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderBridge*
gbinder_bridge_new(
    const char* name,
    const char* const* ifaces,
    GBinderServiceManager* src,
    GBinderServiceManager* dest) /* Since 1.1.5 */
{
    return gbinder_bridge_new2(name, NULL, ifaces, src, dest);
}

GBinderBridge*
gbinder_bridge_new2(
    const char* src_name,
    const char* dest_name,
    const char* const* ifaces,
    GBinderServiceManager* src,
    GBinderServiceManager* dest) /* Since 1.1.6 */
{
    const guint n = gutil_strv_length((const GStrV*)ifaces);

    if (!src_name) {
        src_name = dest_name;
    } else if (!dest_name) {
        dest_name = src_name;
    }
    if (G_LIKELY(src_name) && G_LIKELY(n) && G_LIKELY(src) && G_LIKELY(dest)) {
        GBinderBridge* self = g_slice_new0(GBinderBridge);
        guint i;

        self->src = gbinder_servicemanager_ref(src);
        self->dest = gbinder_servicemanager_ref(dest);
        self->ifaces = g_new(GBinderBridgeInterface*, n + 1);
        for (i = 0; i < n; i++) {
            self->ifaces[i] = gbinder_bridge_interface_new(self,
                src_name, dest_name, ifaces[i]);
        }
        self->ifaces[i] = NULL;
        return self;
    }
    return NULL;
}

void
gbinder_bridge_free(
    GBinderBridge* self)
{
    if (G_LIKELY(self)) {
        GBinderBridgeInterface** bi = self->ifaces;

        while (*bi) {
            gbinder_bridge_interface_free(*bi);
            bi++;
        }
        gbinder_servicemanager_unref(self->src);
        gbinder_servicemanager_unref(self->dest);
        g_free(self->ifaces);
        gutil_slice_free(self);
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
