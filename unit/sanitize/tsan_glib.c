/*
 * Copyright (C) 2026 Herman van Hazendonk <github.com@herrie.org>
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


/*
 * ThreadSanitizer support: GLib on Linux implements GMutex and GCond on
 * raw futexes, which TSan cannot see, so every GMutex-protected access in
 * libgbinder shows up as a data race. Linked into the TSan build only, this
 * file defines the GMutex/GCond functions on top of pthreads. Definitions
 * in the executable take precedence over the ones exported by libglib, so
 * libgbinder and the tests use these instead.
 *
 * GMutex and GCond are 8 bytes, too small for the pthread objects, so the
 * first pointer-sized field holds a lazily allocated pthread object. A
 * statically zero-initialised GMutex is valid, as with GLib.
 *
 * Work handed to another thread through GLib (a GThreadPool item, a GSource
 * attached to the main context) is synchronised inside libglib too, out of
 * TSan's sight, and the same goes for it. For those, the wrappers below tell
 * TSan about the happens-before edge (release on hand-off, acquire right
 * before the callback runs), but only for calls made from this executable,
 * i.e. from libgbinder and the tests, so GLib's own users are unaffected.
 * The same goes for the GObject reference count, see g_object_unref().
 */

#define _GNU_SOURCE
#include <glib.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

void __tsan_acquire(void* addr);
void __tsan_release(void* addr);

static
pthread_mutex_t*
shim_mutex(
    GMutex* m)
{
    pthread_mutex_t* pm = __atomic_load_n((pthread_mutex_t**)&m->p,
        __ATOMIC_ACQUIRE);

    if (!pm) {
        pthread_mutex_t* expected = NULL;

        pm = malloc(sizeof(*pm));
        g_assert(pm);
        pthread_mutex_init(pm, NULL);
        if (!__atomic_compare_exchange_n((pthread_mutex_t**)&m->p, &expected,
            pm, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            pthread_mutex_destroy(pm);
            free(pm);
            pm = expected;
        }
    }
    return pm;
}

static
pthread_cond_t*
shim_cond(
    GCond* c)
{
    pthread_cond_t* pc = __atomic_load_n((pthread_cond_t**)&c->p,
        __ATOMIC_ACQUIRE);

    if (!pc) {
        pthread_cond_t* expected = NULL;
        pthread_condattr_t attr;

        pc = malloc(sizeof(*pc));
        g_assert(pc);
        pthread_condattr_init(&attr);
        /* g_cond_wait_until() takes a CLOCK_MONOTONIC deadline */
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        pthread_cond_init(pc, &attr);
        pthread_condattr_destroy(&attr);
        if (!__atomic_compare_exchange_n((pthread_cond_t**)&c->p, &expected,
            pc, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            pthread_cond_destroy(pc);
            free(pc);
            pc = expected;
        }
    }
    return pc;
}

void g_mutex_init(GMutex* m) { m->p = NULL; shim_mutex(m); }
void g_mutex_lock(GMutex* m) { pthread_mutex_lock(shim_mutex(m)); }
void g_mutex_unlock(GMutex* m) { pthread_mutex_unlock(shim_mutex(m)); }
gboolean g_mutex_trylock(GMutex* m)
    { return !pthread_mutex_trylock(shim_mutex(m)); }

void
g_mutex_clear(
    GMutex* m)
{
    pthread_mutex_t* pm = m->p;

    if (pm) {
        m->p = NULL;
        pthread_mutex_destroy(pm);
        free(pm);
    }
}

void g_cond_init(GCond* c) { c->p = NULL; shim_cond(c); }
void g_cond_signal(GCond* c) { pthread_cond_signal(shim_cond(c)); }
void g_cond_broadcast(GCond* c) { pthread_cond_broadcast(shim_cond(c)); }
void g_cond_wait(GCond* c, GMutex* m)
    { pthread_cond_wait(shim_cond(c), shim_mutex(m)); }

gboolean
g_cond_wait_until(
    GCond* c,
    GMutex* m,
    gint64 end_time)
{
    struct timespec ts;

    /* end_time is in g_get_monotonic_time() units (microseconds) */
    ts.tv_sec = end_time / G_USEC_PER_SEC;
    ts.tv_nsec = (end_time % G_USEC_PER_SEC) * 1000;
    return pthread_cond_timedwait(shim_cond(c), shim_mutex(m), &ts) !=
        ETIMEDOUT;
}

void
g_cond_clear(
    GCond* c)
{
    pthread_cond_t* pc = c->p;

    if (pc) {
        c->p = NULL;
        pthread_cond_destroy(pc);
        free(pc);
    }
}

/*==========================================================================*
 * Happens-before annotations for GLib hand-offs
 *==========================================================================*/

/* Lazy lookup of the libglib implementation. Racing lookups store the same
 * value, and the atomics keep that from being a data race itself. */
#define REAL(name) \
    static __typeof__(name)* real_##name##_cached; \
    __typeof__(name)* real_##name = __atomic_load_n(&real_##name##_cached, \
        __ATOMIC_ACQUIRE); \
    if (!real_##name) { \
        real_##name = (__typeof__(name)*) dlsym(RTLD_NEXT, #name); \
        __atomic_store_n(&real_##name##_cached, real_##name, \
            __ATOMIC_RELEASE); \
    }

static
gboolean
shim_called_from_here(
    const void* ret)
{
    Dl_info self, caller;

    return dladdr((void*)shim_called_from_here, &self) &&
        dladdr(ret, &caller) && self.dli_fbase == caller.dli_fbase;
}

typedef struct shim_pool_func {
    GFunc func;
    gpointer user_data;
} ShimPoolFunc;

static
void
shim_pool_proc(
    gpointer data,
    gpointer user_data)
{
    ShimPoolFunc* pf = user_data;

    __tsan_acquire(data);
    pf->func(data, pf->user_data);
}

GThreadPool*
g_thread_pool_new(
    GFunc func,
    gpointer user_data,
    gint max_threads,
    gboolean exclusive,
    GError** error)
{
    REAL(g_thread_pool_new);
    if (shim_called_from_here(__builtin_return_address(0))) {
        /* Freed never, a few bytes per pool */
        ShimPoolFunc* pf = g_new(ShimPoolFunc, 1);

        pf->func = func;
        pf->user_data = user_data;
        return real_g_thread_pool_new(shim_pool_proc, pf, max_threads,
            exclusive, error);
    }
    return real_g_thread_pool_new(func, user_data, max_threads, exclusive,
        error);
}

gboolean
g_thread_pool_push(
    GThreadPool* pool,
    gpointer data,
    GError** error)
{
    REAL(g_thread_pool_push);
    __tsan_release(data);
    return real_g_thread_pool_push(pool, data, error);
}

typedef struct shim_source_cb {
    GSourceFunc func;
    gpointer data;
    GDestroyNotify destroy;
    GSource* source;
} ShimSourceCb;

static
gboolean
shim_source_proc(
    gpointer user_data)
{
    ShimSourceCb* cb = user_data;

    __tsan_acquire(cb);
    __tsan_acquire(cb->source);
    return cb->func(cb->data);
}

static
void
shim_source_destroy(
    gpointer user_data)
{
    ShimSourceCb* cb = user_data;

    __tsan_acquire(cb);
    __tsan_acquire(cb->source);
    if (cb->destroy) {
        cb->destroy(cb->data);
    }
    g_free(cb);
}

void
g_source_set_callback(
    GSource* source,
    GSourceFunc func,
    gpointer data,
    GDestroyNotify notify)
{
    REAL(g_source_set_callback);
    if (shim_called_from_here(__builtin_return_address(0))) {
        ShimSourceCb* cb = g_new(ShimSourceCb, 1);

        cb->func = func;
        cb->data = data;
        cb->destroy = notify;
        cb->source = source;
        __tsan_release(cb);
        real_g_source_set_callback(source, shim_source_proc, cb,
            shim_source_destroy);
    } else {
        real_g_source_set_callback(source, func, data, notify);
    }
}

guint
g_source_attach(
    GSource* source,
    GMainContext* context)
{
    REAL(g_source_attach);
    __tsan_release(source);
    return real_g_source_attach(source, context);
}

/*
 * libglib sets the callback of its own idle and timeout sources through
 * an internal call that the g_source_set_callback() above doesn't see.
 * Build those sources here so the hand-off is modelled.
 */
static
guint
shim_source_add(
    GSource* source,
    gint priority,
    GSourceFunc func,
    gpointer data,
    GDestroyNotify notify)
{
    guint id;

    g_source_set_priority(source, priority);
    g_source_set_callback(source, func, data, notify);
    id = g_source_attach(source, NULL);
    g_source_unref(source);
    return id;
}

guint
g_idle_add_full(
    gint priority,
    GSourceFunc func,
    gpointer data,
    GDestroyNotify notify)
{
    return shim_source_add(g_idle_source_new(), priority, func, data, notify);
}

guint
g_idle_add(
    GSourceFunc func,
    gpointer data)
{
    return g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, func, data, NULL);
}

guint
g_timeout_add_full(
    gint priority,
    guint interval,
    GSourceFunc func,
    gpointer data,
    GDestroyNotify notify)
{
    return shim_source_add(g_timeout_source_new(interval), priority, func,
        data, notify);
}

guint
g_timeout_add(
    guint interval,
    GSourceFunc func,
    gpointer data)
{
    return g_timeout_add_full(G_PRIORITY_DEFAULT, interval, func, data, NULL);
}

/*
 * GObject reference counting is atomic (with full barriers) inside
 * libgobject. Whichever thread drops the last reference finalizes the
 * object, after everything the other threads did before their unrefs.
 * Model it like an acquire-release reference count.
 */
void
g_object_unref(
    gpointer object)
{
    REAL(g_object_unref);
    if (object && shim_called_from_here(__builtin_return_address(0))) {
        __tsan_acquire(object);
        __tsan_release(object);
    }
    real_g_object_unref(object);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
