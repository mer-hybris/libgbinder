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

#include "gbinder_servicepoll.h"
#include "gbinder_servicemanager.h"
#include "gbinder_eventloop_p.h"

#include <gutil_strv.h>

#include <glib-object.h>

/* This is configurable mostly so that unit testing doesn't take too long */
guint gbinder_servicepoll_interval_ms = 2000;

typedef GObjectClass GBinderServicePollClass;
struct gbinder_servicepoll {
    GObject object;
    GBinderServiceManager* manager;
    char** list;
    gulong list_id;
    GBinderEventLoopTimeout* timer;
};

G_DEFINE_TYPE(GBinderServicePoll, gbinder_servicepoll, G_TYPE_OBJECT)
#define GBINDER_TYPE_SERVICEPOLL (gbinder_servicepoll_get_type())
#define GBINDER_SERVICEPOLL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        GBINDER_TYPE_SERVICEPOLL, GBinderServicePoll))

enum gbinder_servicepoll_signal {
    SIGNAL_NAME_ADDED,
    SIGNAL_COUNT
};

static const char SIGNAL_NAME_ADDED_NAME[] = "servicepoll-name-added";

static guint gbinder_servicepoll_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

/* GBinderServiceManagerListFunc callback returns TRUE to keep the services
 * list, otherwise the caller will deallocate it. */
gboolean
gbinder_servicepoll_list(
    GBinderServiceManager* sm,
    char** services,
    void* user_data)
{
    GBinderServicePoll* self = GBINDER_SERVICEPOLL(user_data);

    gbinder_servicepoll_ref(self);
    self->list_id = 0;
    if (services) {
        const GStrV* ptr_new;

        ptr_new = services = gutil_strv_sort(services, TRUE);
        if (self->list) {
            const GStrV* ptr_old = self->list;

            while (*ptr_new && *ptr_old) {
                const int i = gutil_strv_find(ptr_old, *ptr_new);

                if (i < 0) {
                    /* New name */
                    g_signal_emit(self, gbinder_servicepoll_signals
                        [SIGNAL_NAME_ADDED], 0, *ptr_new);
                } else {
                    int k;

                    /* If some names have disappeared, then i may be > 0 */
                    for (k = 0; k < i; k ++) ptr_old++;
                    ptr_old++;
                }
                ptr_new++;
            }
        }
        while (*ptr_new) {
            g_signal_emit(self, gbinder_servicepoll_signals
                [SIGNAL_NAME_ADDED], 0, *ptr_new);
            ptr_new++;
        }
    }

    g_strfreev(self->list);
    self->list = services;
    gbinder_servicepoll_unref(self);
    return TRUE;
}

static
gboolean
gbinder_servicepoll_timer(
    gpointer user_data)
{
    GBinderServicePoll* self = GBINDER_SERVICEPOLL(user_data);

    if (!self->list_id) {
        self->list_id = gbinder_servicemanager_list(self->manager,
            gbinder_servicepoll_list, self);
    }
    return G_SOURCE_CONTINUE;
}

static
GBinderServicePoll*
gbinder_servicepoll_create(
    GBinderServiceManager* manager)
{
    GBinderServicePoll* self = g_object_new(GBINDER_TYPE_SERVICEPOLL, NULL);

    self->manager = gbinder_servicemanager_ref(manager);
    self->list_id = gbinder_servicemanager_list(manager,
        gbinder_servicepoll_list, self);
    return self;
}

/*==========================================================================*
 * API
 *==========================================================================*/

GBinderServicePoll*
gbinder_servicepoll_new(
    GBinderServiceManager* manager,
    GBinderServicePoll** weakptr)
{
    if (weakptr) {
        if (*weakptr) {
            gbinder_servicepoll_ref(*weakptr);
        } else {
            *weakptr = gbinder_servicepoll_create(manager);
            g_object_add_weak_pointer(G_OBJECT(*weakptr), (gpointer*)weakptr);
        }
        return *weakptr;
    } else {
        return gbinder_servicepoll_create(manager);
    }
}

GBinderServicePoll*
gbinder_servicepoll_ref(
    GBinderServicePoll* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(GBINDER_SERVICEPOLL(self));
        return self;
    } else {
        return NULL;
    }
}

void
gbinder_servicepoll_unref(
    GBinderServicePoll* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(GBINDER_SERVICEPOLL(self));
    }
}

GBinderServiceManager*
gbinder_servicepoll_manager(
    GBinderServicePoll* self)
{
    return G_LIKELY(self) ? self->manager : NULL;
}

gboolean
gbinder_servicepoll_is_known_name(
    GBinderServicePoll* self,
    const char* name)
{
    return G_LIKELY(self) && gutil_strv_contains(self->list, name);
}

gulong
gbinder_servicepoll_add_handler(
    GBinderServicePoll* self,
    GBinderServicePollFunc fn,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_NAME_ADDED_NAME, G_CALLBACK(fn), user_data) : 0;
}

void
gbinder_servicepoll_remove_handler(
    GBinderServicePoll* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
gbinder_servicepoll_init(
    GBinderServicePoll* self)
{
    self->timer = gbinder_timeout_add(gbinder_servicepoll_interval_ms,
        gbinder_servicepoll_timer, self);
}

static
void
gbinder_servicepoll_finalize(
    GObject* object)
{
    GBinderServicePoll* self = GBINDER_SERVICEPOLL(object);

    gbinder_timeout_remove(self->timer);
    gbinder_servicemanager_cancel(self->manager, self->list_id);
    gbinder_servicemanager_unref(self->manager);
    g_strfreev(self->list);
}

static
void
gbinder_servicepoll_class_init(
    GBinderServicePollClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = gbinder_servicepoll_finalize;
    gbinder_servicepoll_signals[SIGNAL_NAME_ADDED] =
        g_signal_new(SIGNAL_NAME_ADDED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE,
            1, G_TYPE_STRING);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
