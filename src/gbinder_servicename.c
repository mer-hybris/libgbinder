/*
 * Copyright (C) 2019-2021 Jolla Ltd.
 * Copyright (C) 2019-2021 Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_types_p.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_servicename.h"
#include "gbinder_servicemanager.h"
#include "gbinder_local_object.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

/* Since 1.0.26 */

#define GBINDER_SERVICENAME_RETRY_INTERVAL_MS (500)

typedef struct gbinder_servicename_priv {
    GBinderServiceName pub;
    gint refcount;
    char* name;
    GBinderLocalObject* object;
    GBinderServiceManager* sm;
    GBinderEventLoopTimeout* retry_timer;
    gulong presence_id;
    gulong add_call_id;
} GBinderServiceNamePriv;

static
void
gbinder_servicename_add_service(
    GBinderServiceNamePriv* priv);

GBINDER_INLINE_FUNC GBinderServiceNamePriv*
gbinder_servicename_cast(GBinderServiceName* pub)
    { return G_CAST(pub, GBinderServiceNamePriv, pub); }

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
gboolean
gbinder_servicename_add_service_retry(
    gpointer user_data)
{
    GBinderServiceNamePriv* priv = user_data;

    priv->retry_timer = NULL;
    gbinder_servicename_add_service(priv);
    return G_SOURCE_REMOVE;
}

static
void
gbinder_servicename_add_service_done(
    GBinderServiceManager* sm,
    int status,
    void* user_data)
{
    GBinderServiceNamePriv* priv = user_data;

    GASSERT(priv->add_call_id);
    priv->add_call_id = 0;
    if (status) {
        GWARN("Error %d adding name \"%s\"", status, priv->name);
        gbinder_timeout_remove(priv->retry_timer);
        priv->retry_timer =
            gbinder_timeout_add(GBINDER_SERVICENAME_RETRY_INTERVAL_MS,
                gbinder_servicename_add_service_retry, priv);
    } else {
        GDEBUG("Service \"%s\" has been registered", priv->name);
    }
}

static
void
gbinder_servicename_add_service(
    GBinderServiceNamePriv* priv)
{
    GDEBUG("Adding service \"%s\"", priv->name);
    gbinder_servicemanager_cancel(priv->sm, priv->add_call_id);
    priv->add_call_id = gbinder_servicemanager_add_service(priv->sm,
        priv->name, priv->object, gbinder_servicename_add_service_done, priv);
}

static
void
gbinder_servicename_presence_handler(
    GBinderServiceManager* sm,
    void* user_data)
{
    GBinderServiceNamePriv* priv = user_data;

    if (gbinder_servicemanager_is_present(sm)) {
        gbinder_servicename_add_service(priv);
    } else {
        if (priv->add_call_id) {
            gbinder_servicemanager_cancel(priv->sm, priv->add_call_id);
            priv->add_call_id = 0;
        }
        if (priv->retry_timer) {
            gbinder_timeout_remove(priv->retry_timer);
            priv->retry_timer = NULL;
        }
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderServiceName*
gbinder_servicename_new(
    GBinderServiceManager* sm,
    GBinderLocalObject* object,
    const char* name)
{
    if (G_LIKELY(sm) && G_LIKELY(object) && G_LIKELY(name)) {
        GBinderServiceNamePriv* priv = g_slice_new0(GBinderServiceNamePriv);
        GBinderServiceName* self = &priv->pub;

        g_atomic_int_set(&priv->refcount, 1);
        priv->object = gbinder_local_object_ref(object);
        priv->sm = gbinder_servicemanager_ref(sm);
        self->name = priv->name = g_strdup(name);
        priv->presence_id = gbinder_servicemanager_add_presence_handler(sm,
            gbinder_servicename_presence_handler, priv);
        if (gbinder_servicemanager_is_present(sm)) {
            gbinder_servicename_add_service(priv);
        }
        return self;
    } else {
        return NULL;
    }
}

GBinderServiceName*
gbinder_servicename_ref(
    GBinderServiceName* self)
{
    if (G_LIKELY(self)) {
        GBinderServiceNamePriv* priv = gbinder_servicename_cast(self);

        GASSERT(priv->refcount > 0);
        g_atomic_int_inc(&priv->refcount);
    }
    return self;
}

void
gbinder_servicename_unref(
    GBinderServiceName* self)
{
    if (G_LIKELY(self)) {
        GBinderServiceNamePriv* priv = gbinder_servicename_cast(self);

        GASSERT(priv->refcount > 0);
        if (g_atomic_int_dec_and_test(&priv->refcount)) {
            gbinder_servicemanager_cancel(priv->sm, priv->add_call_id);
            gbinder_servicemanager_remove_handler(priv->sm, priv->presence_id);
            gbinder_servicemanager_unref(priv->sm);
            gbinder_local_object_unref(priv->object);
            gbinder_timeout_remove(priv->retry_timer);
            g_free(priv->name);
            gutil_slice_free(priv);
        }
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
