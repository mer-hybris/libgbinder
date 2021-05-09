/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_servicemanager_p.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_log.h"

struct gbinder_remote_object_priv {
    gboolean acquired;
};

typedef GObjectClass GBinderRemoteObjectClass;
GType gbinder_remote_object_get_type(void) GBINDER_INTERNAL;
G_DEFINE_TYPE(GBinderRemoteObject, gbinder_remote_object, G_TYPE_OBJECT)

#define PARENT_CLASS gbinder_remote_object_parent_class
#define THIS_TYPE (gbinder_remote_object_get_type())
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj,THIS_TYPE,GBinderRemoteObject)

enum gbinder_remote_object_signal {
    SIGNAL_DEATH,
    SIGNAL_COUNT
};

#define SIGNAL_DEATH_NAME "death"

static guint gbinder_remote_object_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
gbinder_remote_object_handle_death_on_main_thread(
    gpointer user_data)
{
    GBinderRemoteObject* self = THIS(user_data);

    if (!self->dead) {
        GBinderIpc* ipc = self->ipc;
        GBinderDriver* driver = ipc->driver;
        GBinderRemoteObjectPriv* priv = self->priv;

        self->dead = TRUE;
        if (priv->acquired) {
            priv->acquired = FALSE;
            /* Release the dead node */
            gbinder_driver_release(driver, self->handle);
        }
        /* ServiceManager always has the same handle, and can be reanimated. */
        if (self->handle != GBINDER_SERVICEMANAGER_HANDLE) {
            gbinder_ipc_invalidate_remote_handle(ipc, self->handle);
        }
        gbinder_driver_dead_binder_done(driver, self);
        g_signal_emit(self, gbinder_remote_object_signals[SIGNAL_DEATH], 0);
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

gboolean
gbinder_remote_object_reanimate(
    GBinderRemoteObject* self)
{
    /*
     * Don't try to reanimate those who hasn't died yet. Reanimation is
     * kind of a special case and should only be used for servicemanager
     * objects.
     */
    if (self->dead) {
        GBinderIpc* ipc = self->ipc;
        guint32 handle = self->handle;

        /* Kick the horse */
        GASSERT(self->handle == GBINDER_SERVICEMANAGER_HANDLE);
        if (gbinder_ipc_ping_sync(ipc, handle, &gbinder_ipc_sync_main) == 0) {
            GBinderRemoteObjectPriv* priv = self->priv;
            GBinderDriver* driver = ipc->driver;

            /* Wow, it's alive! */
            self->dead = FALSE;
            priv->acquired = TRUE;
            gbinder_ipc_looper_check(ipc); /* For death notifications */
            gbinder_driver_acquire(driver, handle);
            gbinder_driver_request_death_notification(driver, self);
        }
    }
    return !self->dead;
}

void
gbinder_remote_object_handle_death_notification(
    GBinderRemoteObject* self)
{
    /* This function is invoked from the looper thread, the caller has
     * checked the object pointer */
    GVERBOSE_("%p %u", self, self->handle);
    gbinder_idle_callback_invoke_later
        (gbinder_remote_object_handle_death_on_main_thread,
            gbinder_remote_object_ref(self), g_object_unref);
}

void
gbinder_remote_object_commit_suicide(
    GBinderRemoteObject* self)
{
    /* This function is only invoked by GBinderProxyObject in context of
     * the main thread, the object pointer is checked by the caller */
    if (!self->dead) {
        GBinderIpc* ipc = self->ipc;
        GBinderDriver* driver = ipc->driver;
        GBinderRemoteObjectPriv* priv = self->priv;

        self->dead = TRUE;
        if (priv->acquired) {
            priv->acquired = FALSE;
            /* Release the dead node */
            gbinder_driver_release(driver, self->handle);
        }
        GVERBOSE_("%p %u", self, self->handle);
        gbinder_ipc_invalidate_remote_handle(self->ipc, self->handle);
        /* Don't submit BC_DEAD_BINDER_DONE because this is a suicide */
        g_signal_emit(self, gbinder_remote_object_signals[SIGNAL_DEATH], 0);
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderRemoteObject*
gbinder_remote_object_new(
    GBinderIpc* ipc,
    guint32 handle,
    REMOTE_OBJECT_CREATE create)
{
    if (G_LIKELY(ipc)) {
        GBinderRemoteObject* self = g_object_new(THIS_TYPE, NULL);
        GBinderRemoteObjectPriv* priv = self->priv;

        self->ipc = gbinder_ipc_ref(ipc);
        self->handle = handle;
        switch (create) {
        case REMOTE_OBJECT_CREATE_DEAD:
            self->dead = TRUE;
            break;
        case REMOTE_OBJECT_CREATE_ACQUIRED:
            priv->acquired = TRUE;
            /* fallthrough */
        case REMOTE_OBJECT_CREATE_ALIVE:
            break;
        }
        if (!self->dead) {
            gbinder_ipc_looper_check(self->ipc); /* For death notifications */
            if (priv->acquired) {
                gbinder_driver_acquire(ipc->driver, handle);
            }
            gbinder_driver_request_death_notification(ipc->driver, self);
        }
        return self;
    }
    return NULL;
}

GBinderRemoteObject*
gbinder_remote_object_ref(
    GBinderRemoteObject* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(THIS(self));
        return self;
    } else {
        return NULL;
    }
}

void
gbinder_remote_object_unref(
    GBinderRemoteObject* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(THIS(self));
    }
}

GBinderIpc*
gbinder_remote_object_ipc(
    GBinderRemoteObject* self) /* Since 1.0.30 */
{
    return G_LIKELY(self) ? self->ipc : NULL;
}

gboolean
gbinder_remote_object_is_dead(
    GBinderRemoteObject* self)
{
    return G_UNLIKELY(!self) || self->dead;
}

gulong
gbinder_remote_object_add_death_handler(
    GBinderRemoteObject* self,
    GBinderRemoteObjectNotifyFunc fn,
    void* data)
{
    if (G_LIKELY(self) && G_LIKELY(fn)) {
        /* To receive the notifications, we need to have looper running */
        gbinder_ipc_looper_check(self->ipc);
        return g_signal_connect(self, SIGNAL_DEATH_NAME, G_CALLBACK(fn), data);
    }
    return 0;
}

void
gbinder_remote_object_remove_handler(
    GBinderRemoteObject* self,
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
gbinder_remote_object_init(
    GBinderRemoteObject* self)
{
    GBinderRemoteObjectPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        THIS_TYPE, GBinderRemoteObjectPriv);

    self->priv = priv;
}

static
void
gbinder_remote_object_dispose(
    GObject* object)
{
    GBinderRemoteObject* self = THIS(object);

    gbinder_ipc_remote_object_disposed(self->ipc, self);
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
}

static
void
gbinder_remote_object_finalize(
    GObject* object)
{
    GBinderRemoteObject* self = THIS(object);
    GBinderRemoteObjectPriv* priv = self->priv;
    GBinderIpc* ipc = self->ipc;
    GBinderDriver* driver = ipc->driver;

    if (!self->dead) {
        gbinder_driver_clear_death_notification(driver, self);
    }
    if (priv->acquired) {
        gbinder_driver_release(driver, self->handle);
    }
    gbinder_ipc_unref(ipc);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
gbinder_remote_object_class_init(
    GBinderRemoteObjectClass* klass)
{
    GObjectClass* remote_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GBinderRemoteObjectPriv));
    remote_class->dispose = gbinder_remote_object_dispose;
    remote_class->finalize = gbinder_remote_object_finalize;

    gbinder_remote_object_signals[SIGNAL_DEATH] =
        g_signal_new(SIGNAL_DEATH_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
