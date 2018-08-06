/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
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

#include "gbinder_ipc.h"
#include "gbinder_driver.h"
#include "gbinder_handler.h"
#include "gbinder_io.h"
#include "gbinder_object_registry.h"
#include "gbinder_local_object_p.h"
#include "gbinder_local_reply.h"
#include "gbinder_local_request_p.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_remote_reply_p.h"
#include "gbinder_remote_request_p.h"
#include "gbinder_writer.h"
#include "gbinder_log.h"

#include <gutil_idlepool.h>
#include <gutil_macros.h>

#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <errno.h>

typedef struct gbinder_ipc_looper GBinderIpcLooper;

struct gbinder_ipc_priv {
    GBinderIpc* self;
    GThreadPool* tx_pool;
    GHashTable* tx_table;
    GMainContext* context;
    char* key;
    GBinderObjectRegistry object_registry;

    GMutex remote_objects_mutex;
    GHashTable* remote_objects;

    GMutex local_objects_mutex;
    GHashTable* local_objects;

    /* We may need more loopers... But let's start with just one */
    GMutex looper_mutex;
    GBinderIpcLooper* looper;
};

typedef GObjectClass GBinderIpcClass;
G_DEFINE_TYPE(GBinderIpc, gbinder_ipc, G_TYPE_OBJECT)
#define GBINDER_TYPE_IPC (gbinder_ipc_get_type())
#define GBINDER_IPC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        GBINDER_TYPE_IPC, GBinderIpc))

/*
 * Binder requests are blocking, worker threads are needed in order to
 * implement asynchronous requests, hence the synchronization.
 */
static GHashTable* gbinder_ipc_table = NULL;
static pthread_mutex_t gbinder_ipc_mutex = PTHREAD_MUTEX_INITIALIZER;

#define GBINDER_IPC_MAX_TX_THREADS (15)
#define GBINDER_IPC_MAX_LOOPERS (15)

/*
 * When looper receives the transaction:
 *
 * 1. Finds the target object and allocates GBinderIpcLooperTx.
 * 2. Posts the GBinderIpcLooperTx reference to the main thread
 * 4. Waits for (any) byte from the receiving end of the tx pipe.
 *
 * When the main thread receives GBinderIpcLooperTx:
 *
 * 1. Lets the object to process it and produce the response (GBinderOutput).
 * 2. Writes one byte to the sending end of the tx pipe.
 * 3. Unreferences GBinderIpcLooperTx
 *
 * When tx pipe wakes up the looper:
 *
 * 1. Sends the transaction to the kernel.
 * 2. Unreferences GBinderIpcLooperTx
 *
 * Note that GBinderIpcLooperTx can be deallocated on either looper or
 * main thread, depending on whether looper gives up on the transaction
 * before it gets processed.
 */

#define TX_DONE (0x2a)

typedef struct gbinder_ipc_looper_tx {
    /* Reference count */
    gint refcount;
    /* These are filled by the looper: */
    int pipefd[2];
    guint32 code;
    guint32 flags;
    GBinderLocalObject* obj;
    GBinderRemoteRequest* req;
    /* And these by the main thread processing the transaction: */
    GBinderLocalReply* reply;
    int status;
} GBinderIpcLooperTx;

struct gbinder_ipc_looper {
    gint refcount;
    GBinderHandler handler;
    GBinderDriver* driver;
    GBinderIpc* ipc; /* Not a reference! */
    GThread* thread;
    int pipefd[2];
    int txfd[2];
};

typedef struct gbinder_ipc_tx_priv GBinderIpcTxPriv;

typedef
void
(*GBinderIpcTxPrivFunc)(
    GBinderIpcTxPriv* tx);

typedef struct gbinder_ipc_tx_priv {
    GBinderIpcTx pub;
    GBinderIpcTxPrivFunc fn_exec;
    GBinderIpcTxPrivFunc fn_done;
    GBinderIpcTxPrivFunc fn_free;
} GBinderIpcTxPriv;

typedef struct gbinder_ipc_tx_internal {
    GBinderIpcTxPriv tx;
    guint32 handle;
    guint32 code;
    guint32 flags;
    int status;
    GBinderLocalRequest* req;
    GBinderRemoteReply* reply;
    GBinderIpcReplyFunc fn_reply;
    GDestroyNotify fn_destroy;
} GBinderIpcTxInternal;

typedef struct gbinder_ipc_tx_custom {
    GBinderIpcTxPriv tx;
    GBinderIpcTxFunc fn_custom_exec;
    GBinderIpcTxFunc fn_custom_done;
    GDestroyNotify fn_custom_destroy;
} GBinderIpcTxCustom;

GBINDER_INLINE_FUNC const char* gbinder_ipc_name(GBinderIpc* self)
    { return gbinder_driver_dev(self->driver); }

/*==========================================================================*
 * GBinderIpcLooperTx
 *==========================================================================*/

static
GBinderIpcLooperTx*
gbinder_ipc_looper_tx_new(
    GBinderLocalObject* obj,
    guint32 code,
    guint32 flags,
    GBinderRemoteRequest* req,
    const int* fd)
{
    GBinderIpcLooperTx* tx = g_slice_new0(GBinderIpcLooperTx);

    g_atomic_int_set(&tx->refcount, 1);
    memcpy(tx->pipefd, fd, sizeof(tx->pipefd));
    tx->code = code;
    tx->flags = flags;
    tx->obj = gbinder_local_object_ref(obj);
    tx->req = gbinder_remote_request_ref(req);
    return tx;
}

static
void
gbinder_ipc_looper_tx_free(
    GBinderIpcLooperTx* tx)
{
    if (tx->pipefd[0] >= 0) {
        close(tx->pipefd[0]);
        close(tx->pipefd[1]);
    }
    gbinder_local_object_unref(tx->obj);
    gbinder_remote_request_unref(tx->req);
    gbinder_local_reply_unref(tx->reply);
    g_slice_free(GBinderIpcLooperTx, tx);
}

static
GBinderIpcLooperTx*
gbinder_ipc_looper_tx_ref(
    GBinderIpcLooperTx* tx)
{
    GASSERT(tx->refcount > 0);
    g_atomic_int_inc(&tx->refcount);
    return tx;
}

static
gboolean
gbinder_ipc_looper_tx_unref(
    GBinderIpcLooperTx* tx,
    gboolean dropfd)
{
    gboolean dropped = FALSE;
    GASSERT(tx->refcount > 0);
    if (g_atomic_int_dec_and_test(&tx->refcount)) {
        if (dropfd) {
            tx->pipefd[0] = tx->pipefd[1] = -1;
            dropped = TRUE;
        }
        gbinder_ipc_looper_tx_free(tx);
    }
    return dropped;
}

/*==========================================================================*
 * GBinderIpcLooper
 *==========================================================================*/

static
gboolean
gbinder_ipc_looper_tx_handle(
    gpointer data)
{
    GBinderIpcLooperTx* tx = data;
    guint8 done = TX_DONE;

    /* Actually handle the transaction */
    tx->reply = gbinder_local_object_handle_transaction(tx->obj, tx->req,
        tx->code, tx->flags, &tx->status);

    /* And wake up the looper */
    (void)write(tx->pipefd[1], &done, sizeof(done));
    return G_SOURCE_REMOVE;
}

static
void
gbinder_ipc_looper_tx_done(
    gpointer data)
{
    gbinder_ipc_looper_tx_unref(data, FALSE);
}

static
GBinderLocalReply*
gbinder_ipc_looper_transact(
    GBinderHandler* handler,
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* result)
{
    GBinderIpcLooper* looper = G_CAST(handler,GBinderIpcLooper,handler);
    GBinderIpc* ipc = looper->ipc;
    GBinderLocalReply* reply = NULL;
    int status = -EFAULT;

    if (looper->txfd[0] < 0 && pipe(looper->txfd)) {
        GERR("Failed to create a tx pipe: %s", strerror(errno));
    }

    if (looper->txfd[0] >= 0) {
        GBinderIpcLooperTx* tx = gbinder_ipc_looper_tx_new(obj, code, flags,
            req, looper->txfd);
        GBinderIpcPriv* priv = ipc->priv;
        struct pollfd fds[2];
        guint8 done = 0;
        GSource* source = g_idle_source_new();

        /* Let GBinderLocalObject handle the transaction on the main thread */
        g_source_set_callback(source, gbinder_ipc_looper_tx_handle,
            gbinder_ipc_looper_tx_ref(tx), gbinder_ipc_looper_tx_done);
        g_source_attach(source, priv->context);
        g_source_unref(source);

        /* Wait for either transaction completion or looper shutdown */
        memset(fds, 0, sizeof(fds));
        fds[0].fd = looper->pipefd[0];
        fds[0].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
        fds[1].fd = tx->pipefd[0];
        fds[1].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
        poll(fds, 2, -1);

        if ((fds[1].revents & POLLIN) &&
            read(fds[1].fd, &done, sizeof(done)) == 1) {
            /* Normal completion */
            GASSERT(done == TX_DONE);
            reply = gbinder_local_reply_ref(tx->reply);
            status = tx->status;
            if (!gbinder_ipc_looper_tx_unref(tx, TRUE)) {
                /* gbinder_ipc_looper_tx_free() will close those */
                looper->txfd[0] = looper->txfd[1] = -1;
            }
        } else {
            gbinder_ipc_looper_tx_unref(tx, FALSE);
        }
    }
    *result = status;
    return reply;
}

static
void
gbinder_ipc_looper_free(
    GBinderIpcLooper* looper)
{
    if (looper->thread) {
        g_thread_unref(looper->thread);
    }
    close(looper->pipefd[0]);
    close(looper->pipefd[1]);
    if (looper->txfd[0] >= 0) {
        close(looper->txfd[0]);
        close(looper->txfd[1]);
    }
    gbinder_driver_unref(looper->driver);
    g_slice_free(GBinderIpcLooper, looper);
}

static
GBinderIpcLooper*
gbinder_ipc_looper_ref(
    GBinderIpcLooper* looper)
{
    GASSERT(looper->refcount > 0);
    g_atomic_int_inc(&looper->refcount);
    return looper;
}

static
void
gbinder_ipc_looper_unref(
    GBinderIpcLooper* looper)
{
    GASSERT(looper->refcount > 0);
    if (g_atomic_int_dec_and_test(&looper->refcount)) {
        gbinder_ipc_looper_free(looper);
    }
}

static
gpointer
gbinder_ipc_looper_thread(
    gpointer data)
{
    GBinderIpcLooper* looper = data;
    GBinderDriver* driver = looper->driver;

    if (gbinder_driver_enter_looper(driver)) {
        struct pollfd pipefd;
        int result;

        GDEBUG("Looper %s running", gbinder_driver_dev(driver));
        memset(&pipefd, 0, sizeof(pipefd));
        pipefd.fd = looper->pipefd[0]; /* read end of the pipe */
        pipefd.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;

        result = gbinder_driver_poll(driver, &pipefd);
        while (looper->ipc && ((result & POLLIN) || !result)) {
            if (result & POLLIN) {
                /* No need to synchronize access to looper->ipc because
                 * the other thread would wait until this thread exits
                 * before setting looper->ipc to NULL */
                GBinderIpc* ipc = gbinder_ipc_ref(looper->ipc);
                GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
                /* But that gbinder_driver_read() may unref GBinderIpc */
                int ret = gbinder_driver_read(driver, reg, &looper->handler);
                /* And this gbinder_ipc_unref() may release the last ref: */
                gbinder_ipc_unref(ipc);
                /* And at this point looper->ipc may be NULL */
                if (ret < 0) {
                    GDEBUG("Looper %s failed", gbinder_driver_dev(driver));
                    break;
                }
            }
            if (pipefd.revents) {
                /* Any event from this pipe terminates the loop */
                GDEBUG("Looper %s is asked to exit",
                    gbinder_driver_dev(driver));
                break;
            }
            result = gbinder_driver_poll(driver, &pipefd);
        }

        gbinder_driver_exit_looper(driver);

        /*
         * Again, there's no need to synchronize access to looper->ipc
         * because the other thread would wait until this thread exits
         * before setting looper->ipc to NULL
         */
        if (looper->ipc) {
            GBinderIpcPriv* priv = looper->ipc->priv;
            /* Lock */
            g_mutex_lock(&priv->looper_mutex);
            if (priv->looper == looper) {
                /* Spontaneous exit */
                priv->looper = NULL;
                GDEBUG("Looper %s exits", gbinder_driver_dev(driver));
            } else {
                /* Main thread is shutting it down */
                GDEBUG("Looper %s done", gbinder_driver_dev(driver));
            }
            g_mutex_unlock(&priv->looper_mutex);
            /* Unlock */
        } else {
            GDEBUG("Looper %s is abandoned", gbinder_driver_dev(driver));
        }
    }

    gbinder_ipc_looper_unref(looper);
    return NULL;
}

static
GBinderIpcLooper*
gbinder_ipc_looper_new(
    GBinderIpc* ipc)
{
    int fd[2];

    /* Note: this call can actually fail */
    if (!pipe(fd)) {
        static const GBinderHandlerFunctions handler_functions = {
            .transact = gbinder_ipc_looper_transact
        };
        GError* error = NULL;
        GBinderIpcLooper* looper = g_slice_new0(GBinderIpcLooper);

        memcpy(looper->pipefd, fd, sizeof(fd));
        looper->txfd[0] = looper->txfd[1] = -1;
        g_atomic_int_set(&looper->refcount, 1);
        looper->handler.f = &handler_functions;
        looper->ipc = ipc;
        looper->driver = gbinder_driver_ref(ipc->driver);
        looper->thread = g_thread_try_new(gbinder_ipc_name(ipc),
            gbinder_ipc_looper_thread, looper, &error);
        if (looper->thread) {
            /* gbinder_ipc_looper_thread() will release this reference: */
            gbinder_ipc_looper_ref(looper);
            return looper;
        } else {
            GERR("Failed to create looper thread: %s", GERRMSG(error));
            g_error_free(error);
        }
        gbinder_ipc_looper_unref(looper);
    } else {
        GERR("Failed to create looper pipe: %s", strerror(errno));
    }
    return NULL;
}

void
gbinder_ipc_looper_check(
    GBinderIpc* self)
{
    if (G_LIKELY(self)) {
        GBinderIpcPriv* priv = self->priv;

        if (!priv->looper) {
            /* Lock */
            g_mutex_lock(&priv->looper_mutex);
            if (!priv->looper) {
                GDEBUG("Starting looper %s", gbinder_ipc_name(self));
                priv->looper = gbinder_ipc_looper_new(self);
            }
            g_mutex_unlock(&priv->looper_mutex);
            /* Unlock */
        }
    }
}

/*==========================================================================*
 * GBinderObjectRegistry
 *==========================================================================*/

/**
 * Internal function called by gbinder_object_dispose(). Among other things,
 * it means that it doesn't have to check GBinderIpc pointer for NULL.
 *
 * Note the following scenario (where object may be either local or remote):
 *
 * 1. Last reference to GBinderObject goes away.
 * 2. gbinder_ipc_object_disposed() is invoked by gbinder_object_dispose()
 * 3. Before gbinder_ipc_object_disposed() grabs the lock,
 *    gbinder_ipc_new_remote_object() gets there first, finds the
 *    object in the hashtable, bumps its refcount (under the lock)
 *    and returns new reference to the caller.
 * 4. gbinder_ipc_object_disposed() finally gets its lock, finds
 *    that the object's refcount is greater than zero and leaves
 *    the object in the table.
 *
 * It's OK for a GObject to get re-referenced in dispose. glib will
 * recheck the refcount once dispose returns, the object stays alive
 * and gbinder_object_finalize() won't be called this time around,
 */
void
gbinder_ipc_local_object_disposed(
    GBinderIpc* self,
    GBinderLocalObject* obj)
{
    GBinderIpcPriv* priv = self->priv;

    /* Lock */
    g_mutex_lock(&priv->local_objects_mutex);
    if (obj->object.ref_count == 1 && priv->local_objects) {
        g_hash_table_remove(priv->local_objects, obj);
        if (g_hash_table_size(priv->local_objects) == 0) {
            g_hash_table_unref(priv->local_objects);
            priv->local_objects = NULL;
        }
    }
    g_mutex_unlock(&priv->local_objects_mutex);
    /* Unlock */
}

void
gbinder_ipc_remote_object_disposed(
    GBinderIpc* self,
    GBinderRemoteObject* obj)
{
    GBinderIpcPriv* priv = self->priv;

    /* Lock */
    g_mutex_lock(&priv->remote_objects_mutex);
    if (obj->object.ref_count == 1 && priv->remote_objects) {
        void* key = GINT_TO_POINTER(obj->handle);

        GVERBOSE_("handle %u", obj->handle);
        GASSERT(g_hash_table_contains(priv->remote_objects, key));
        g_hash_table_remove(priv->remote_objects, key);
        if (g_hash_table_size(priv->remote_objects) == 0) {
            g_hash_table_unref(priv->remote_objects);
            priv->remote_objects = NULL;
        }
    }
    g_mutex_unlock(&priv->remote_objects_mutex);
    /* Unlock */
}

GBinderLocalObject*
gbinder_ipc_new_local_object(
    GBinderIpc* self,
    const char* iface,
    GBinderLocalTransactFunc txproc,
    void* data)
{
    GBinderIpcPriv* priv = self->priv;
    GBinderLocalObject* obj = gbinder_local_object_new
        (self, iface, txproc, data);

    /* Lock */
    g_mutex_lock(&priv->local_objects_mutex);
    if (!priv->local_objects) {
        priv->local_objects = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
    g_hash_table_insert(priv->local_objects, obj, obj);
    g_mutex_unlock(&priv->local_objects_mutex);
    /* Unlock */

    GVERBOSE_("%p", obj);
    gbinder_ipc_looper_check(self);
    return obj;
}

static
GBinderLocalObject*
gbinder_ipc_priv_get_local_object(
    GBinderIpcPriv* priv,
    void* pointer)
{
    GBinderLocalObject* obj = NULL;

    if (pointer) {
        /* Lock */
        g_mutex_lock(&priv->local_objects_mutex);
        if (priv->local_objects) {
            obj = g_hash_table_lookup(priv->local_objects, pointer);
            if (obj) {
                gbinder_local_object_ref(obj);
            } else {
                GWARN("Unknown local object %p", pointer);
            }
        } else {
            GWARN("Unknown local object %p", pointer);
        }
        g_mutex_unlock(&priv->local_objects_mutex);
        /* Unlock */
    }

    return obj;
}

static
GBinderRemoteObject*
gbinder_ipc_priv_get_remote_object(
    GBinderIpcPriv* priv,
    guint32 handle)
{
    GBinderRemoteObject* obj = NULL;
    void* key = GINT_TO_POINTER(handle);

    /* Lock */
    g_mutex_lock(&priv->remote_objects_mutex);
    if (priv->remote_objects) {
        obj = g_hash_table_lookup(priv->remote_objects, key);
    }
    if (obj) {
        gbinder_remote_object_ref(obj);
    } else {
        obj = gbinder_remote_object_new(priv->self, handle);
        if (!priv->remote_objects) {
            priv->remote_objects = g_hash_table_new
                (g_direct_hash, g_direct_equal);
        }
        g_hash_table_replace(priv->remote_objects, key, obj);
    }
    g_mutex_unlock(&priv->remote_objects_mutex);
    /* Unlock */

    return obj;
}

GBinderRemoteObject*
gbinder_ipc_get_remote_object(
    GBinderIpc* self,
    guint32 handle)
{
    /* GBinderServiceManager makes sure that GBinderIpc pointer is not NULL */
    return gbinder_ipc_priv_get_remote_object(self->priv, handle);
}

GBINDER_INLINE_FUNC
GBinderIpcPriv*
gbinder_ipc_priv_from_object_registry(
    GBinderObjectRegistry* reg)
{
    return G_CAST(reg, GBinderIpcPriv, object_registry);
}

GBINDER_INLINE_FUNC
GBinderIpc*
gbinder_ipc_from_object_registry(
    GBinderObjectRegistry* reg)
{
    return gbinder_ipc_priv_from_object_registry(reg)->self;
}

static
void
gbinder_ipc_object_registry_ref(
    GBinderObjectRegistry* reg)
{
    gbinder_ipc_ref(gbinder_ipc_from_object_registry(reg));
}

static
void
gbinder_ipc_object_registry_unref(
    GBinderObjectRegistry* reg)
{
    gbinder_ipc_unref(gbinder_ipc_from_object_registry(reg));
}

static
GBinderLocalObject*
gbinder_ipc_object_registry_get_local(
    GBinderObjectRegistry* reg,
    void* pointer)
{
    return gbinder_ipc_priv_get_local_object
        (gbinder_ipc_priv_from_object_registry(reg), pointer);
}

static
GBinderRemoteObject*
gbinder_ipc_object_registry_get_remote(
    GBinderObjectRegistry* reg,
    guint32 handle)
{
    return gbinder_ipc_priv_get_remote_object
        (gbinder_ipc_priv_from_object_registry(reg), handle);
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
gulong
gbinder_ipc_tx_new_id()
{
    static gint gbinder_ipc_next_id = 1;
    gulong id = (guint)g_atomic_int_add(&gbinder_ipc_next_id, 1);

    if (!id) id = (guint)g_atomic_int_add(&gbinder_ipc_next_id, 1);
    return id;
}

static
gulong
gbinder_ipc_tx_get_id(
    GBinderIpc* self)
{
    GBinderIpcPriv* priv = self->priv;
    gulong id = gbinder_ipc_tx_new_id();

    while (g_hash_table_contains(priv->tx_table, GINT_TO_POINTER(id))) {
        id = gbinder_ipc_tx_new_id();
    }
    return id;
}

static
void
gbinder_ipc_tx_pub_init(
    GBinderIpcTx* tx,
    GBinderIpc* self,
    gulong id,
    void* user_data)
{
    tx->id = id;
    tx->ipc = gbinder_ipc_ref(self);
    tx->user_data = user_data;
}

static
inline
GBinderIpcTxInternal*
gbinder_ipc_tx_internal_cast(
    GBinderIpcTxPriv* priv)
{
    return G_CAST(priv, GBinderIpcTxInternal, tx);
}

static
void
gbinder_ipc_tx_internal_free(
    GBinderIpcTxPriv* priv)
{
    GBinderIpcTxInternal* tx = gbinder_ipc_tx_internal_cast(priv);
    GBinderIpcTx* pub = &priv->pub;

    gbinder_local_request_unref(tx->req);
    gbinder_remote_reply_unref(tx->reply);
    if (tx->fn_destroy) {
        tx->fn_destroy(pub->user_data);
    }
    g_slice_free(GBinderIpcTxInternal, tx);
}

static
void
gbinder_ipc_tx_internal_done(
    GBinderIpcTxPriv* priv)
{
    GBinderIpcTxInternal* tx = gbinder_ipc_tx_internal_cast(priv);
    GBinderIpcTx* pub = &priv->pub;

    if (tx->fn_reply) {
        tx->fn_reply(pub->ipc, tx->reply, tx->status, pub->user_data);
    }
}

static
void
gbinder_ipc_tx_internal_exec(
    GBinderIpcTxPriv* priv)
{
    GBinderIpcTxInternal* tx = gbinder_ipc_tx_internal_cast(priv);
    GBinderIpcTx* pub = &priv->pub;
    GBinderIpc* self = pub->ipc;
    GBinderObjectRegistry* reg = &self->priv->object_registry;

    /* Perform synchronous transaction */
    tx->reply = gbinder_remote_reply_new(&self->priv->object_registry);
    tx->status = gbinder_driver_transact(self->driver, reg,
        tx->handle, tx->code, tx->req, tx->reply);
    if (tx->status != GBINDER_STATUS_OK &&
        gbinder_remote_reply_is_empty(tx->reply)) {
        /* Drop useless reply */
        gbinder_remote_reply_unref(tx->reply);
        tx->reply = NULL;
    }
}

static
GBinderIpcTxPriv*
gbinder_ipc_tx_internal_new(
    GBinderIpc* self,
    gulong id,
    guint32 handle,
    guint32 code,
    guint32 flags,
    GBinderLocalRequest* req,
    GBinderIpcReplyFunc reply,
    GDestroyNotify destroy,
    void* user_data)
{
    GBinderIpcTxInternal* tx = g_slice_new0(GBinderIpcTxInternal);
    GBinderIpcTxPriv* priv = &tx->tx;

    gbinder_ipc_tx_pub_init(&priv->pub, self, id, user_data);
    priv->fn_exec = gbinder_ipc_tx_internal_exec;
    priv->fn_done = gbinder_ipc_tx_internal_done;
    priv->fn_free = gbinder_ipc_tx_internal_free;

    tx->code = code;
    tx->flags = flags;
    tx->handle = handle;
    tx->req = gbinder_local_request_ref(req);
    tx->fn_reply = reply;
    tx->fn_destroy = destroy;

    return priv;
}

static
inline
GBinderIpcTxCustom*
gbinder_ipc_tx_custom_cast(
    GBinderIpcTxPriv* priv)
{
    return G_CAST(priv, GBinderIpcTxCustom, tx);
}

static
void
gbinder_ipc_tx_custom_free(
    GBinderIpcTxPriv* priv)
{
    GBinderIpcTxCustom* tx = gbinder_ipc_tx_custom_cast(priv);

    if (tx->fn_custom_destroy) {
        tx->fn_custom_destroy(priv->pub.user_data);
    }
    g_slice_free(GBinderIpcTxCustom, tx);
}

static
void
gbinder_ipc_tx_custom_done(
    GBinderIpcTxPriv* priv)
{
    GBinderIpcTxCustom* tx = gbinder_ipc_tx_custom_cast(priv);

    if (tx->fn_custom_done) {
        tx->fn_custom_done(&priv->pub);
    }
}

static
void
gbinder_ipc_tx_custom_exec(
    GBinderIpcTxPriv* priv)
{
    GBinderIpcTxCustom* tx = gbinder_ipc_tx_custom_cast(priv);

    if (tx->fn_custom_exec) {
        tx->fn_custom_exec(&priv->pub);
    }
}

static
GBinderIpcTxPriv*
gbinder_ipc_tx_custom_new(
    GBinderIpc* self,
    gulong id,
    GBinderIpcTxFunc exec,
    GBinderIpcTxFunc done,
    GDestroyNotify destroy,
    void* user_data)
{
    GBinderIpcTxCustom* tx = g_slice_new0(GBinderIpcTxCustom);
    GBinderIpcTxPriv* priv = &tx->tx;

    gbinder_ipc_tx_pub_init(&priv->pub, self, id, user_data);
    priv->fn_exec = gbinder_ipc_tx_custom_exec;
    priv->fn_done = gbinder_ipc_tx_custom_done;
    priv->fn_free = gbinder_ipc_tx_custom_free;

    tx->fn_custom_exec = exec;
    tx->fn_custom_done = done;
    tx->fn_custom_destroy = destroy;

    return priv;
}

static
void
gbinder_ipc_tx_free(
    gpointer data)
{
    GBinderIpcTxPriv* tx = data;
    GBinderIpcTx* pub = &tx->pub;
    GBinderIpc* self = pub->ipc;
    GBinderIpcPriv* priv = self->priv;

    g_hash_table_remove(priv->tx_table, GINT_TO_POINTER(pub->id));
    tx->fn_free(tx);

    /* This may actually deallocate GBinderIpc object: */
    gbinder_ipc_unref(self);
}

static
gboolean
gbinder_ipc_tx_done(
    gpointer data)
{
    GBinderIpcTxPriv* tx = data;
    GBinderIpcTx* pub = &tx->pub;
    GBinderIpc* self = pub->ipc;
    GBinderIpcPriv* priv = self->priv;

    if (g_hash_table_remove(priv->tx_table, GINT_TO_POINTER(tx->pub.id))) {
        GASSERT(!pub->cancelled);
        tx->fn_done(tx);
    }

    return G_SOURCE_REMOVE;
}

/* Invoked on a thread from tx_pool */
static
void
gbinder_ipc_tx_proc(
    gpointer data,
    gpointer object)
{
    GBinderIpcTxPriv* tx = data;
    GBinderIpc* self = GBINDER_IPC(object);
    GBinderIpcPriv* priv = self->priv;
    GSource* source = g_idle_source_new();

    if (!tx->pub.cancelled) {
        tx->fn_exec(tx);
    } else {
        GVERBOSE_("not executing transaction %lu (cancelled)", tx->pub.id);
    }

    /* The result is handled by the main thread */
    g_source_set_callback(source, gbinder_ipc_tx_done, tx, gbinder_ipc_tx_free);
    g_source_attach(source, priv->context);
    g_source_unref(source);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderIpc*
gbinder_ipc_new(
    const char* dev)
{
    GBinderIpc* self = NULL;

    if (!dev || !dev[0]) dev = GBINDER_DEFAULT_BINDER;
    /* Lock */
    pthread_mutex_lock(&gbinder_ipc_mutex);
    if (gbinder_ipc_table) {
        self = g_hash_table_lookup(gbinder_ipc_table, dev);
    }
    if (self) {
        gbinder_ipc_ref(self);
    } else {
        GBinderDriver* driver = gbinder_driver_new(dev);

        if (driver) {
            GBinderIpcPriv* priv;

            self = g_object_new(GBINDER_TYPE_IPC, NULL);
            priv = self->priv;
            self->driver = driver;
            self->dev = priv->key = g_strdup(dev);
            self->priv->object_registry.io = gbinder_driver_io(driver);
            /* gbinder_ipc_dispose will remove iself from the table */
            if (!gbinder_ipc_table) {
                gbinder_ipc_table = g_hash_table_new(g_str_hash, g_str_equal);
            }
            g_hash_table_replace(gbinder_ipc_table, priv->key, self);
        }
    }
    pthread_mutex_unlock(&gbinder_ipc_mutex);
    /* Unlock */
    return self;
}

GBinderIpc*
gbinder_ipc_ref(
    GBinderIpc* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(GBINDER_IPC(self));
        return self;
    } else {
        return NULL;
    }
}

void
gbinder_ipc_unref(
    GBinderIpc* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(GBINDER_IPC(self));
    }
}

GBinderObjectRegistry*
gbinder_ipc_object_registry(
    GBinderIpc* self)
{
    /* Only used by unit tests */
    return G_LIKELY(self) ? &self->priv->object_registry : NULL;
}

GBinderRemoteReply*
gbinder_ipc_transact_sync_reply(
    GBinderIpc* self,
    guint32 handle,
    guint32 code,
    GBinderLocalRequest* req,
    int* status)
{
    if (G_LIKELY(self)) {
        GBinderIpcPriv* priv = self->priv;
        GBinderObjectRegistry* reg = &priv->object_registry;
        GBinderRemoteReply* reply = gbinder_remote_reply_new(reg);
        int ret = gbinder_driver_transact(self->driver, reg, handle, code,
            req, reply);

        if (status) *status = ret;
        if (ret == GBINDER_STATUS_OK || !gbinder_remote_reply_is_empty(reply)) {
            return reply;
        } else {
            gbinder_remote_reply_unref(reply);
        }
    } else {
        if (status) *status = (-EINVAL);
    }
    return NULL;
}

int
gbinder_ipc_transact_sync_oneway(
    GBinderIpc* self,
    guint32 handle,
    guint32 code,
    GBinderLocalRequest* req)
{
    if (G_LIKELY(self)) {
        GBinderIpcPriv* priv = self->priv;

        return gbinder_driver_transact(self->driver, &priv->object_registry,
            handle, code, req, NULL);
    } else {
        return (-EINVAL);
    }
}

gulong
gbinder_ipc_transact(
    GBinderIpc* self,
    guint32 handle,
    guint32 code,
    guint32 flags,
    GBinderLocalRequest* req,
    GBinderIpcReplyFunc reply,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(self)) {
        GBinderIpcPriv* priv = self->priv;
        GBinderIpcTxPriv* tx = gbinder_ipc_tx_internal_new(self,
            gbinder_ipc_tx_get_id(self), handle, code, flags, req, reply,
            destroy, user_data);
        const gulong id = tx->pub.id;

        g_hash_table_insert(priv->tx_table, GINT_TO_POINTER(id), tx);
        g_thread_pool_push(priv->tx_pool, tx, NULL);
        return id;
    } else {
        return 0;
    }
}

gulong
gbinder_ipc_transact_custom(
    GBinderIpc* self,
    GBinderIpcTxFunc exec,
    GBinderIpcTxFunc done,
    GDestroyNotify destroy,
    void* user_data)
{
    if (G_LIKELY(self)) {
        GBinderIpcPriv* priv = self->priv;
        GBinderIpcTxPriv* tx = gbinder_ipc_tx_custom_new(self,
            gbinder_ipc_tx_get_id(self), exec, done, destroy, user_data);
        const gulong id = tx->pub.id;

        g_hash_table_insert(priv->tx_table, GINT_TO_POINTER(id), tx);
        g_thread_pool_push(priv->tx_pool, tx, NULL);
        return id;
    } else {
        return 0;
    }
}

void
gbinder_ipc_cancel(
    GBinderIpc* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        gconstpointer key = GINT_TO_POINTER(id);
        GBinderIpcPriv* priv = self->priv;
        GBinderIpcTx* tx = g_hash_table_lookup(priv->tx_table, key);

        if (tx) {
            GVERIFY(g_hash_table_remove(priv->tx_table, key));
            tx->cancelled = TRUE;
            GVERBOSE_("%lu", id);
        } else {
            GWARN("Invalid transaction id %lu", id);
        }
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
gbinder_ipc_init(
    GBinderIpc* self)
{
    static const GBinderObjectRegistryFunctions object_registry_functions = {
        .ref = gbinder_ipc_object_registry_ref,
        .unref = gbinder_ipc_object_registry_unref,
        .get_local = gbinder_ipc_object_registry_get_local,
        .get_remote = gbinder_ipc_object_registry_get_remote
    };
    GBinderIpcPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, GBINDER_TYPE_IPC,
        GBinderIpcPriv);

    g_mutex_init(&priv->looper_mutex);
    g_mutex_init(&priv->local_objects_mutex);
    g_mutex_init(&priv->remote_objects_mutex);
    priv->context = g_main_context_default();
    priv->tx_table = g_hash_table_new(g_direct_hash, g_direct_equal);
    priv->tx_pool = g_thread_pool_new(gbinder_ipc_tx_proc, self,
        GBINDER_IPC_MAX_TX_THREADS, FALSE, NULL);
    priv->object_registry.f = &object_registry_functions;
    priv->self = self;
    self->priv = priv;
    self->pool = gutil_idle_pool_new();
}

static
void
gbinder_ipc_dispose(
    GObject* object)
{
    GBinderIpc* self = GBINDER_IPC(object);
    GBinderIpcPriv* priv = self->priv;
    GBinderIpcLooper* looper;

    GVERBOSE_("%s", self->dev);
    /* Lock */
    pthread_mutex_lock(&gbinder_ipc_mutex);
    GASSERT(gbinder_ipc_table);
    if (gbinder_ipc_table) {
        GBinderIpcPriv* priv = self->priv;

        GASSERT(g_hash_table_lookup(gbinder_ipc_table, priv->key) == self);
        g_hash_table_remove(gbinder_ipc_table, priv->key);
        if (g_hash_table_size(gbinder_ipc_table) == 0) {
            g_hash_table_unref(gbinder_ipc_table);
            gbinder_ipc_table = NULL;
        }
    }
    pthread_mutex_unlock(&gbinder_ipc_mutex);
    /* Unlock */

    /* Lock */
    g_mutex_lock(&priv->looper_mutex);
    looper = priv->looper;
    priv->looper = NULL;
    g_mutex_unlock(&priv->looper_mutex);
    /* Unlock */

    if (looper) {
        if (looper->thread && looper->thread != g_thread_self()) {
            guint8 done = TX_DONE;

            GDEBUG("Stopping looper %s", gbinder_ipc_name(looper->ipc));
            if (write(looper->pipefd[1], &done, sizeof(done)) > 0) {
                g_thread_join(looper->thread);
                looper->thread = NULL;
            }
        }
        looper->ipc = NULL;
        gbinder_ipc_looper_unref(looper);
    }

    G_OBJECT_CLASS(gbinder_ipc_parent_class)->finalize(object);
}

static
void
gbinder_ipc_finalize(
    GObject* object)
{
    GBinderIpc* self = GBINDER_IPC(object);
    GBinderIpcPriv* priv = self->priv;

    GASSERT(!priv->local_objects);
    GASSERT(!priv->remote_objects);
    g_mutex_clear(&priv->looper_mutex);
    g_mutex_clear(&priv->local_objects_mutex);
    g_mutex_clear(&priv->remote_objects_mutex);
    g_thread_pool_free(priv->tx_pool, FALSE, TRUE);
    GASSERT(!g_hash_table_size(priv->tx_table));
    g_hash_table_unref(priv->tx_table);
    gutil_idle_pool_unref(self->pool);
    gbinder_driver_unref(self->driver);
    g_free(priv->key);
    G_OBJECT_CLASS(gbinder_ipc_parent_class)->finalize(object);
}

static
void
gbinder_ipc_class_init(
    GBinderIpcClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GBinderIpcPriv));
    object_class->dispose = gbinder_ipc_dispose;
    object_class->finalize = gbinder_ipc_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
