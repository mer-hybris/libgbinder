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

#ifndef GBINDER_EVENTLOOP_H
#define GBINDER_EVENTLOOP_H

#include "gbinder_types.h"

G_BEGIN_DECLS

/* Since 1.0.40 */

typedef struct gbinder_eventloop_integration GBinderEventLoopIntegration;
typedef void (*GBinderEventLoopCallbackFunc)(gpointer data);

typedef struct gbinder_eventloop_timeout {
    const GBinderEventLoopIntegration* eventloop;
} GBinderEventLoopTimeout;

typedef struct gbinder_eventloop_callback {
    const GBinderEventLoopIntegration* eventloop;
} GBinderEventLoopCallback;

/**
 * Main event loop integration. There is only one main event loop in the
 * process (by definition).
 *
 * By default, GLib event loop is being used for callbacks and timeouts.
 *
 * It may be necessary to replace it with e.g. Qt event loop. Quite often
 * Qt event loop is implemented by QEventDispatcherGlib which is sitting
 * on top of GLib event and therefore works with the default implementation.
 * But it won't work with e.g. QEventDispatcherUNIX.
 *
 * For Qt programs that use QEventDispatcherUNIX, it needs to be replaced
 * with the one provided by libqbinder.
 */
typedef struct gbinder_eventloop_integration {

    /**
     * timeout_add
     *
     * Sets a function to be called at regular intervals (in milliseconds).
     * If the function returns G_SOURCE_REMOVE, timeout is automatically
     * destroyed (you must not call timeout_remove in this case). If the
     * function returns G_SOURCE_CONTINUE, it will be called again after
     * the same interval.
     */
    GBinderEventLoopTimeout* (*timeout_add)(guint millis, GSourceFunc func,
        gpointer data);

    /**
     * timeout_remove
     *
     * Removes a pending timeout and destroys it. The caller makes sure that
     * argument is not NULL. Note that timeout is automatically destroyed if
     * the callback function returns G_SOURCE_REMOVE.
     */
    void (*timeout_remove)(GBinderEventLoopTimeout* timeout);

    /**
     * callback_new
     *
     * Creates a callback object. It returns you a reference, you must
     * eventually pass the returned object to callback_unref to drop
     * this reference.
     *
     * Note that it doesn't automatically schedule the callback. You
     * must explicitly call callback_schedule to actually schedule it.
     * The finalize function is invoked regardless of whether callback
     * was cancelled or not.
     */
    GBinderEventLoopCallback* (*callback_new)(GBinderEventLoopCallbackFunc fun,
        gpointer data, GDestroyNotify finalize);

    /**
     * callback_ref
     *
     * Increments the reference count. That prevents the object from being
     * deleted before you drop this reference. The caller makes sure that
     * argument is not NULL.
     */
    void (*callback_ref)(GBinderEventLoopCallback* cb);

    /**
     * callback_unref
     *
     * Decrements the reference count (drops the reference). When reference
     * count reaches zero, the object gets deleted. The caller makes sure
     * that argument is not NULL.
     *
     * Note that calling callback_schedule temporarily adds an internal
     * reference until the callback is invoked or callback_cancel is called,
     * whichever happens first.
     */
    void (*callback_unref)(GBinderEventLoopCallback* cb);

    /**
     * callback_schedule
     *
     * Schedules the callback to be invoked in the main loop at some point
     * in the future (but as soon as possible). The caller makes sure that
     * argument is not NULL.
     *
     * This adds an internal reference to the GBinderEventLoopCallback object
     * until the callback is invoked or callback_cancel is called, whichever
     * happens first.
     */
    void (*callback_schedule)(GBinderEventLoopCallback* cb);

    /**
     * callback_cancel
     *
     * Makes sure that callback won't be invoked (if it hasn't been
     * invoked yet) and drops the internal reference. Does nothing
     * if the callback has already been invoked. The caller makes sure that
     * argument is not NULL.
     */
    void (*callback_cancel)(GBinderEventLoopCallback* cb);

    /**
     * cleanup
     *
     * This function is called when event loop integration is being replaced
     * with a different one, or libgbinder is being unloaded.
     */
    void (*cleanup)(void);

    /* Padding for future expansion */
    void (*_reserved1)(void);
    void (*_reserved2)(void);
    void (*_reserved3)(void);
    void (*_reserved4)(void);
    void (*_reserved5)(void);
    void (*_reserved6)(void);
    void (*_reserved7)(void);
    void (*_reserved8)(void);
    void (*_reserved9)(void);

    /*
     * api_level will remain zero (and ignored) until we run out of
     * the above placeholders. Hopefully, forever.
     */
    int api_level;
} GBinderEventLoopIntegration;

/**
 * gbinder_eventloop_set should be called before libgbinder creates any of
 * its internal threads. And it must be done from the main thread.
 */
void
gbinder_eventloop_set(
    const GBinderEventLoopIntegration* loop);

G_END_DECLS

#endif /* GBINDER_EVENTLOOP_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
