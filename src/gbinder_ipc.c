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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gbinder_ipc.h"
#include "gbinder_driver.h"
#include "gbinder_handler.h"
#include "gbinder_io.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_object_registry.h"
#include "gbinder_local_object_p.h"
#include "gbinder_local_reply.h"
#include "gbinder_local_request_p.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_remote_reply_p.h"
#include "gbinder_remote_request_p.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_writer.h"
#include "gbinder_log.h"

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
    char* key;
    GBinderObjectRegistry object_registry;

    GMutex remote_objects_mutex;
    GHashTable* remote_objects;

    GMutex local_objects_mutex;
    GHashTable* local_objects;

    GMutex looper_mutex;
    GBinderIpcLooper* primary_loopers;
    GBinderIpcLooper* blocked_loopers;
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
#define GBINDER_IPC_MAX_PRIMARY_LOOPERS (5)
#define GBINDER_IPC_LOOPER_START_TIMEOUT_SEC (2)

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
 * 2. Writes one byte (TX_DONE) to the sending end of the tx pipe.
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
 *
 * When transaction is blocked by gbinder_remote_request_block() call, it
 * gets slightly more complicated. Then the main thread writes TX_BLOCKED
 * to the pipe (rather than TX_DONE) and then looper thread spawn another
 * looper and keeps waiting for TX_DONE.
 */

#define TX_DONE (0x2a)
#define TX_BLOCKED (0x3b)

typedef enum gbinder_ipc_looper_tx_state {
    GBINDER_IPC_LOOPER_TX_SCHEDULED,
    GBINDER_IPC_LOOPER_TX_PROCESSING,
    GBINDER_IPC_LOOPER_TX_PROCESSED,
    GBINDER_IPC_LOOPER_TX_BLOCKING,
    GBINDER_IPC_LOOPER_TX_BLOCKED,
    GBINDER_IPC_LOOPER_TX_COMPLETE
} GBINDER_IPC_LOOPER_TX_STATE;

struct gbinder_ipc_looper_tx {
    /* Reference count */
    gint refcount;
    /* These are filled by the looper: */
    int pipefd[2];
    guint32 code;
    guint32 flags;
    GBinderLocalObject* obj;
    GBinderRemoteRequest* req;
    /* And these by the main thread processing the transaction: */
    GBINDER_IPC_LOOPER_TX_STATE state;
    GBinderLocalReply* reply;
    int status;
} /* GBinderIpcLooperTx */;

struct gbinder_ipc_looper {
    gint refcount;
    GBinderIpcLooper* next;
    char* name;
    GBinderHandler handler;
    GBinderDriver* driver;
    GBinderIpc* ipc; /* Not a reference! */
    GThread* thread;
    GMutex mutex;
    GCond start_cond;
    gint exit;
    gint started;
    int pipefd[2];
    int txfd[2];
};

typedef struct gbinder_ipc_tx_handler {
    int pipefd[2];
    int txfd[2];
} GBinderIpcTxHandler;

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
    GBinderEventLoopCallback* completion;
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

static
GBinderIpcLooper*
gbinder_ipc_looper_new(
    GBinderIpc* ipc);

/*==========================================================================*
 * Utilities
 *==========================================================================*/

static
gboolean
gbinder_ipc_wait(
    int fd_wakeup,
    int fd_read,
    guint8* out)
{
    struct pollfd fds[2];

    memset(fds, 0, sizeof(fds));
    fds[0].fd = fd_wakeup;
    fds[0].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
    fds[1].fd = fd_read;
    fds[1].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
    if (poll(fds, 2, -1) < 0) {
        GWARN("Transaction pipe polling error: %s", strerror(errno));
    } else if (fds[1].revents & POLLIN) {
        const ssize_t n = read(fds[1].fd, out, 1);

        if (n == 1) {
            return TRUE;
        } else if (n < 0) {
            GWARN("Transaction pipe read error: %s", strerror(errno));
        } else {
            GWARN("Nothing was read from the transaction pipe");
        }
    }
    return FALSE;
}

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
 * State machine of transaction handling. All this is happening on the event
 * thread and therefore doesn't need to be synchronized.
 *
 * SCHEDULED
 * =========
 *     |
 * PROCESSING
 * ==========
 *     |
 * --------------------- handler is called ---------------------------------
 *     |
 *     +---------------- request doesn't need to be blocked ----------+
 *     |                                                              |
 *   gbinder_remote_request_block()                                   |
 *     |                                                              |
 * BLOCKING -- gbinder_remote_request_complete() --> PROCESSED        |
 * ========                                          =========        |
 *     |                                                 |            |
 * --------------------- handler returns -----------------------------------
 *     |                                                 |            |
 * BLOCKED                                           COMPLETE <-------+
 * =======                                           ========
 *                                                       ^
 *   ...                                                 |
 * gbinder_remote_request_complete() is called later ----+
 *==========================================================================*/

void
gbinder_remote_request_block(
    GBinderRemoteRequest* req) /* Since 1.0.20 */
{
    if (G_LIKELY(req)) {
        GBinderIpcLooperTx* tx = req->tx;

        GASSERT(tx);
        if (G_LIKELY(tx)) {
            GASSERT(tx->state == GBINDER_IPC_LOOPER_TX_PROCESSING);
            if (tx->state == GBINDER_IPC_LOOPER_TX_PROCESSING) {
                tx->state = GBINDER_IPC_LOOPER_TX_BLOCKING;
            }
        }
    }
}

void
gbinder_remote_request_complete(
    GBinderRemoteRequest* req,
    GBinderLocalReply* reply,
    int status) /* Since 1.0.20 */
{
    if (G_LIKELY(req)) {
        GBinderIpcLooperTx* tx = req->tx;

        GASSERT(tx);
        if (G_LIKELY(tx)) {
            const guint8 done = TX_DONE;

            switch (tx->state) {
            case GBINDER_IPC_LOOPER_TX_BLOCKING:
                /* Called by the transaction handler */
                tx->status = status;
                tx->reply = gbinder_local_reply_ref(reply);
                tx->state = GBINDER_IPC_LOOPER_TX_PROCESSED;
                break;
            case GBINDER_IPC_LOOPER_TX_BLOCKED:
                /* Really asynchronous completion */
                tx->status = status;
                tx->reply = gbinder_local_reply_ref(reply);
                tx->state = GBINDER_IPC_LOOPER_TX_COMPLETE;
                /* Wake up the looper */
                if (write(tx->pipefd[1], &done, sizeof(done)) <= 0) {
                    GWARN("Failed to wake up the looper");
                }
                break;
            default:
                GWARN("Unexpected state %d in request completion", tx->state);
                break;
            }

            /* Clear the transaction reference */
            gbinder_ipc_looper_tx_unref(tx, FALSE);
            req->tx = NULL;
       }
    }
}

/*==========================================================================*
 * GBinderIpcLooper
 *==========================================================================*/

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
    g_free(looper->name);
    g_cond_clear(&looper->start_cond);
    g_mutex_clear(&looper->mutex);
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
void
gbinder_ipc_looper_tx_handle(
    gpointer data)
{
    GBinderIpcLooperTx* tx = data;
    GBinderRemoteRequest* req = tx->req;
    GBinderLocalReply* reply;
    int status = GBINDER_STATUS_OK;
    guint8 done;

    /*
     * Transaction reference for gbinder_remote_request_block()
     * and gbinder_remote_request_complete().
     */
    req->tx = gbinder_ipc_looper_tx_ref(tx);

    /* See state machine */
    GASSERT(tx->state == GBINDER_IPC_LOOPER_TX_SCHEDULED);
    tx->state = GBINDER_IPC_LOOPER_TX_PROCESSING;

    /* Actually handle the transaction */
    reply = gbinder_local_object_handle_transaction(tx->obj, req,
        tx->code, tx->flags, &status);

    /* Handle all possible return states */
    switch (tx->state) {
    case GBINDER_IPC_LOOPER_TX_PROCESSING:
        /* Result was returned by the handler */
        tx->reply = reply;
        tx->status = status;
        tx->state = GBINDER_IPC_LOOPER_TX_COMPLETE;
        reply = NULL;
        break;
    case GBINDER_IPC_LOOPER_TX_PROCESSED:
        /* Result has been provided to gbinder_remote_request_complete() */
        tx->state = GBINDER_IPC_LOOPER_TX_COMPLETE;
        break;
    case GBINDER_IPC_LOOPER_TX_BLOCKING:
        /* Result will be provided to gbinder_remote_request_complete() */
        tx->state = GBINDER_IPC_LOOPER_TX_BLOCKED;
        break;
    default:
        break;
    }

    /* In case handler returns a reply which it wasn't expected to return */
    GASSERT(!reply);
    gbinder_local_reply_unref(reply);

    /* Drop the transaction reference unless blocked */
    if (tx->state == GBINDER_IPC_LOOPER_TX_BLOCKED) {
        done = TX_BLOCKED;
        /*
         * From this point on, it's GBinderRemoteRequest who's holding
         * reference to GBinderIpcLooperTx, not the other way around and
         * not both ways. Even if gbinder_remote_request_complete() never
         * gets called, transaction will still be completed when the last
         * reference to GBinderRemoteRequest goes away. And if request
         * never gets deallocated... oh well.
         */
        gbinder_remote_request_unref(tx->req);
        tx->req = NULL;
    } else {
        done = TX_DONE;
        if (req->tx) {
            gbinder_ipc_looper_tx_unref(req->tx, FALSE);
            req->tx = NULL;
        }
    }

    /* And wake up the looper */
    if (write(tx->pipefd[1], &done, sizeof(done)) <= 0) {
        GWARN("Failed to wake up the looper");
    }
}

static
void
gbinder_ipc_looper_tx_done(
    gpointer data)
{
    gbinder_ipc_looper_tx_unref(data, FALSE);
}

static
void
gbinder_ipc_looper_start(
    GBinderIpcLooper* looper)
{
    if (!g_atomic_int_get(&looper->started)) {
        /* Lock */
        g_mutex_lock(&looper->mutex);
        if (!g_atomic_int_get(&looper->started)) {
            g_cond_wait_until(&looper->start_cond, &looper->mutex,
                g_get_monotonic_time() + GBINDER_IPC_LOOPER_START_TIMEOUT_SEC *
                    G_TIME_SPAN_SECOND);
            GASSERT(g_atomic_int_get(&looper->started));
        }
        g_mutex_unlock(&looper->mutex);
        /* Unlock */
    }
}

static
gboolean
gbinder_ipc_looper_remove_from_list(
    GBinderIpcLooper* looper,
    GBinderIpcLooper** list)
{
    /* Caller holds looper_mutex */
    if (*list) {
        if ((*list) == looper) {
            (*list) = looper->next;
            looper->next = NULL;
            return TRUE;
        } else {
            GBinderIpcLooper* prev = (*list);

            while (prev->next) {
                if (prev->next == looper) {
                    prev->next = looper->next;
                    looper->next = NULL;
                    return TRUE;
                }
                prev = prev->next;
            }
        }
    }
    return FALSE;
}

static
gboolean
gbinder_ipc_looper_remove_primary(
    GBinderIpcLooper* looper)
{
    return gbinder_ipc_looper_remove_from_list(looper,
        &looper->ipc->priv->primary_loopers);
}

static
gboolean
gbinder_ipc_looper_remove_blocked(
    GBinderIpcLooper* looper)
{
    return gbinder_ipc_looper_remove_from_list(looper,
        &looper->ipc->priv->blocked_loopers);
}

static
guint
gbinder_ipc_looper_count_primary(
    GBinderIpcLooper* looper)
{
    const GBinderIpcLooper* ptr = looper->ipc->priv->primary_loopers;
    guint n = 0;

    for (n = 0; ptr; ptr = ptr->next) n++;
    return n;
}

static
gboolean
gbinder_ipc_looper_can_loop(
    GBinderHandler* handler)
{
    GBinderIpcLooper* looper = G_CAST(handler,GBinderIpcLooper,handler);

    return !g_atomic_int_get(&looper->exit);
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
        guint8 done = 0;
        gboolean was_blocked = FALSE;
        /* Let GBinderLocalObject handle the transaction on the main thread */
        GBinderEventLoopCallback* callback =
            gbinder_idle_callback_schedule_new(gbinder_ipc_looper_tx_handle,
                gbinder_ipc_looper_tx_ref(tx), gbinder_ipc_looper_tx_done);

        /* Wait for either transaction completion or looper shutdown */
        if (gbinder_ipc_wait(looper->pipefd[0], tx->pipefd[0], &done) &&
            done == TX_BLOCKED) {
            /*
             * We are going to block this looper for potentially
             * significant period of time. Start new looper to
             * accept normal incoming requests and terminate this
             * one when we are done with this transaction.
             *
             * For the duration of the transaction, this looper is
             * moved to the blocked_loopers list.
             */
            GBinderIpcPriv* priv = looper->ipc->priv;
            GBinderIpcLooper* new_looper = NULL;

            /* Lock */
            g_mutex_lock(&priv->looper_mutex);
            if (gbinder_ipc_looper_remove_primary(looper)) {
                GDEBUG("Primary looper %s is blocked", looper->name);
                looper->next = priv->blocked_loopers;
                priv->blocked_loopers = looper;
                was_blocked = TRUE;

                /* If there's no more primary loopers left, create one */
                if (!priv->primary_loopers) {
                    new_looper = gbinder_ipc_looper_new(ipc);
                    if (new_looper) {
                        /* Will unref it after it gets started */
                        gbinder_ipc_looper_ref(new_looper);
                        priv->primary_loopers = new_looper;
                    }
                }
            }
            g_mutex_unlock(&priv->looper_mutex);
            /* Unlock */

            if (new_looper) {
                /* Wait until it gets started */
                gbinder_ipc_looper_start(new_looper);
                gbinder_ipc_looper_unref(new_looper);
            }

            /* Block until asynchronous transaction gets completed. */
            done = 0;
            if (gbinder_ipc_wait(looper->pipefd[0], tx->pipefd[0], &done)) {
                GDEBUG("Looper %s is released", looper->name);
                GASSERT(done == TX_DONE);
            }
        }

        if (done) {
            GASSERT(done == TX_DONE);
            reply = gbinder_local_reply_ref(tx->reply);
            status = tx->status;
        }

        if (!gbinder_ipc_looper_tx_unref(tx, TRUE)) {
            /*
             * This wasn't the last reference meaning that
             * gbinder_ipc_looper_tx_free() will close the
             * descriptors and we will have to create a new
             * pipe for the next transaction.
             */
            looper->txfd[0] = looper->txfd[1] = -1;
        }

        gbinder_idle_callback_destroy(callback);

        if (was_blocked) {
            guint n;

            g_mutex_lock(&priv->looper_mutex);
            n = gbinder_ipc_looper_count_primary(looper);
            if (n >= GBINDER_IPC_MAX_PRIMARY_LOOPERS) {
                /* Looper will exit once transaction completes */
                GDEBUG("Too many primary loopers (%u)", n);
                g_atomic_int_set(&looper->exit, 1);
            } else {
                /* Move it back to the primary list */
                gbinder_ipc_looper_remove_blocked(looper);
                looper->next = priv->primary_loopers;
                priv->primary_loopers = looper;
            }
            g_mutex_unlock(&priv->looper_mutex);
        }
    }
    *result = status;
    return reply;
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
        int res;

        GDEBUG("Looper %s running", looper->name);
        g_mutex_lock(&looper->mutex);
        g_atomic_int_set(&looper->started, TRUE);
        g_cond_broadcast(&looper->start_cond);
        g_mutex_unlock(&looper->mutex);

        memset(&pipefd, 0, sizeof(pipefd));
        pipefd.fd = looper->pipefd[0]; /* read end of the pipe */
        pipefd.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;

        res = gbinder_driver_poll(driver, &pipefd);
        while (!g_atomic_int_get(&looper->exit) && ((res & POLLIN) || !res)) {
            if (res & POLLIN) {
                /*
                 * No need to synchronize access to looper->ipc because
                 * the other thread would wait until this thread exits
                 * before setting looper->ipc to NULL.
                 */
                GBinderIpc* ipc = gbinder_ipc_ref(looper->ipc);
                GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
                /* But that gbinder_driver_read() may unref GBinderIpc */
                int ret = gbinder_driver_read(driver, reg, &looper->handler);

                /* And this gbinder_ipc_unref() may release the last ref: */
                gbinder_ipc_unref(ipc);
                /* And at this point looper->ipc may be NULL */
                if (ret < 0) {
                    GDEBUG("Looper %s failed", looper->name);
                    break;
                }
            }
            /* Any event from this pipe terminates the loop */
            if (pipefd.revents || g_atomic_int_get(&looper->exit)) {
                GDEBUG("Looper %s is requested to exit", looper->name);
                break;
            }
            res = gbinder_driver_poll(driver, &pipefd);
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
            if (gbinder_ipc_looper_remove_blocked(looper) ||
                gbinder_ipc_looper_remove_primary(looper)) {
                /* Spontaneous exit */
                GDEBUG("Looper %s exits", looper->name);
                gbinder_ipc_looper_unref(looper);
            } else {
                /* Main thread is shutting it down */
                GDEBUG("Looper %s done", looper->name);
            }
            g_mutex_unlock(&priv->looper_mutex);
            /* Unlock */
        } else {
            GDEBUG("Looper %s is abandoned", looper->name);
        }
    } else {
        g_mutex_lock(&looper->mutex);
        g_atomic_int_set(&looper->started, TRUE);
        g_cond_broadcast(&looper->start_cond);
        g_mutex_unlock(&looper->mutex);
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
            .can_loop = gbinder_ipc_looper_can_loop,
            .transact = gbinder_ipc_looper_transact
        };
        GError* error = NULL;
        GBinderIpcLooper* looper = g_slice_new0(GBinderIpcLooper);
        static gint gbinder_ipc_next_looper_id = 1;
        guint id = (guint)g_atomic_int_add(&gbinder_ipc_next_looper_id, 1);

        memcpy(looper->pipefd, fd, sizeof(fd));
        looper->txfd[0] = looper->txfd[1] = -1;
        g_atomic_int_set(&looper->refcount, 1);
        g_cond_init(&looper->start_cond);
        g_mutex_init(&looper->mutex);
        looper->name = g_strdup_printf("%s#%u", gbinder_ipc_name(ipc), id);
        looper->handler.f = &handler_functions;
        looper->ipc = ipc;
        looper->driver = gbinder_driver_ref(ipc->driver);
        looper->thread = g_thread_try_new(looper->name,
            gbinder_ipc_looper_thread, looper, &error);
        if (looper->thread) {
            /* gbinder_ipc_looper_thread() will release this reference: */
            gbinder_ipc_looper_ref(looper);
            GDEBUG("Starting looper %s", looper->name);
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

        if (!priv->primary_loopers) {
            GBinderIpcLooper* looper;

            /* Lock */
            g_mutex_lock(&priv->looper_mutex);
            if (!priv->primary_loopers) {
                priv->primary_loopers = gbinder_ipc_looper_new(self);
            }
            looper = priv->primary_loopers;
            if (looper) {
                gbinder_ipc_looper_ref(looper);
            }
            g_mutex_unlock(&priv->looper_mutex);
            /* Unlock */

            /* We are not ready to accept incoming transactions until
             * looper has started. We may need to wait a bit. */
            if (looper) {
                gbinder_ipc_looper_start(looper);
                gbinder_ipc_looper_unref(looper);
            }
        }
    }
}

static
void
gbinder_ipc_looper_stop(
    GBinderIpcLooper* looper)
{
    /* Caller checks looper for NULL */
    if (looper->thread) {
        GDEBUG("Stopping looper %s", looper->name);
        g_atomic_int_set(&looper->exit, TRUE);
        if (looper->thread != g_thread_self()) {
            guint8 done = TX_DONE;

            if (write(looper->pipefd[1], &done, sizeof(done)) <= 0) {
                looper->thread = NULL;
            }
        }
    }
}

static
GBinderIpcLooper*
gbinder_ipc_looper_stop_all(
    GBinderIpcLooper* loopers,
    GBinderIpcLooper* list)
{
    while (list) {
        GBinderIpcLooper* looper = list;
        GBinderIpcLooper* next = looper->next;

        gbinder_ipc_looper_stop(looper);
        looper->next = loopers;
        loopers = looper;
        list = next;
    }
    return loopers;
}

static
void
gbinder_ipc_looper_join(
    GBinderIpcLooper* looper)
{
    /* Caller checks looper for NULL */
    if (looper->thread && looper->thread != g_thread_self()) {
        g_thread_join(looper->thread);
        looper->thread = NULL;
    }
    looper->ipc = NULL;
}

/*==========================================================================*
 * GBinderIpcTxHandler
 *
 * It's needed to handle the following scenario:
 *
 * 1. Asynchronous call is made. The actual transaction is performed on
 *    gbinder_ipc_tx_proc thread.
 * 2. While we were are waiting for completion of our transaction, we
 *    receive a valid incoming transation.
 * 3. This transaction is handled by gbinder_ipc_tx_handler_transact.
 *
 * This seems to be quite a rare scenario, so we allocate a new
 * GBinderIpcTxHandler (and new pipes) for each such transaction,
 * to keep things as simple as possible.
 *
 *==========================================================================*/

static
GBinderIpcTxHandler*
gbinder_ipc_tx_handler_new(
    void)
{
    GBinderIpcTxHandler* h = g_slice_new0(GBinderIpcTxHandler);

    /* Note: pipe() calls can actually fail */
    if (!pipe(h->txfd)) {
        if (!pipe(h->pipefd)) {
            return h;
        } else {
            GERR("Failed to create a tx pipe: %s", strerror(errno));
        }
        close(h->txfd[0]);
        close(h->txfd[1]);
    } else {
        GERR("Failed to create a tx pipe: %s", strerror(errno));
    }
    g_slice_free(GBinderIpcTxHandler, h);
    return NULL;
}

static
void
gbinder_ipc_tx_handler_free(
    GBinderIpcTxHandler* h)
{
    close(h->pipefd[0]);
    close(h->pipefd[1]);
    if (h->txfd[0] >= 0) {
        close(h->txfd[0]);
        close(h->txfd[1]);
    }
    g_slice_free(GBinderIpcTxHandler, h);
}

static
GBinderLocalReply*
gbinder_ipc_tx_handler_transact(
    GBinderHandler* handler,
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* result)
{
    GBinderIpcTxHandler* h = gbinder_ipc_tx_handler_new();
    GBinderLocalReply* reply = NULL;
    int status = -EFAULT;

    if (h) {
        GBinderIpcLooperTx* tx = gbinder_ipc_looper_tx_new(obj, code, flags,
            req, h->txfd);
        guint8 done = 0;
        /* Handle transaction on the main thread */
        GBinderEventLoopCallback* callback =
            gbinder_idle_callback_schedule_new(gbinder_ipc_looper_tx_handle,
                gbinder_ipc_looper_tx_ref(tx), gbinder_ipc_looper_tx_done);

        /* Wait for completion */
        if (gbinder_ipc_wait(h->pipefd[0], tx->pipefd[0], &done) &&
            done == TX_BLOCKED) {
            /* Block until asynchronous transaction gets completed. */
            done = 0;
            if (gbinder_ipc_wait(h->pipefd[0], tx->pipefd[0], &done)) {
                GASSERT(done == TX_DONE);
            }
        }

        if (done) {
            GASSERT(done == TX_DONE);
            reply = gbinder_local_reply_ref(tx->reply);
            status = tx->status;
        }

        if (!gbinder_ipc_looper_tx_unref(tx, TRUE)) {
            /*
             * This wasn't the last references meaning that
             * gbinder_ipc_looper_tx_free() will close the
             * descriptors and we will have to create a new
             * pipe for the next transaction.
             */
            h->txfd[0] = h->txfd[1] = -1;
        }

        gbinder_idle_callback_destroy(callback);
        gbinder_ipc_tx_handler_free(h);
    }

    *result = status;
    return reply;
}

/*==========================================================================*
 * GBinderObjectRegistry
 *==========================================================================*/

static
void
gbinder_ipc_invalidate_remote_handle_locked(
    GBinderIpcPriv* priv,
    guint32 handle)
{
    /* Caller holds priv->remote_objects_mutex */
    if (priv->remote_objects) {
        GVERBOSE_("handle %u", handle);
        g_hash_table_remove(priv->remote_objects, GINT_TO_POINTER(handle));
        if (g_hash_table_size(priv->remote_objects) == 0) {
            g_hash_table_unref(priv->remote_objects);
            priv->remote_objects = NULL;
        }
    }
}

void
gbinder_ipc_invalidate_remote_handle(
    GBinderIpc* self,
    guint32 handle)
{
    GBinderIpcPriv* priv = self->priv;

    /* Lock */
    g_mutex_lock(&priv->remote_objects_mutex);
    gbinder_ipc_invalidate_remote_handle_locked(priv, handle);
    g_mutex_unlock(&priv->remote_objects_mutex);
    /* Unlock */
}

/**
 * Internal functions called by gbinder_object_dispose(). Among other things,
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
    if (obj->object.ref_count == 1) {
        gbinder_ipc_invalidate_remote_handle_locked(priv, obj->handle);
    }
    g_mutex_unlock(&priv->remote_objects_mutex);
    /* Unlock */
}

void
gbinder_ipc_register_local_object(
    GBinderIpc* self,
    GBinderLocalObject* obj)
{
    GBinderIpcPriv* priv = self->priv;

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
    guint32 handle,
    gboolean maybe_dead)
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
        /*
         * If maybe_dead is TRUE, the caller is supposed to try reanimating
         * the object on the main thread not holding any global locks.
         */
        obj = gbinder_remote_object_new(priv->self, handle, maybe_dead);
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
    guint32 handle,
    gboolean maybe_dead)
{
    /* GBinderServiceManager makes sure that GBinderIpc pointer is not NULL */
    return gbinder_ipc_priv_get_remote_object(self->priv, handle, maybe_dead);
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
        (gbinder_ipc_priv_from_object_registry(reg), handle, FALSE);
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
gbinder_ipc_tx_free(
    gpointer data)
{
    GBinderIpcTxPriv* tx = data;
    GBinderIpcTx* pub = &tx->pub;
    GBinderIpc* self = pub->ipc;
    GBinderIpcPriv* priv = self->priv;

    gbinder_idle_callback_unref(tx->completion);
    g_hash_table_remove(priv->tx_table, GINT_TO_POINTER(pub->id));
    tx->fn_free(tx);

    /* This may actually deallocate GBinderIpc object: */
    gbinder_ipc_unref(self);
}

static
void
gbinder_ipc_tx_done(
    gpointer data)
{
    GBinderIpcTxPriv* tx = data;
    GBinderIpcTx* pub = &tx->pub;

    if (!pub->cancelled) {
        tx->fn_done(tx);
    }
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
void
gbinder_ipc_tx_priv_init(
    GBinderIpcTxPriv* priv,
    GBinderIpc* self,
    gulong id,
    void* user_data,
    GBinderIpcTxPrivFunc fn_exec,
    GBinderIpcTxPrivFunc fn_done,
    GBinderIpcTxPrivFunc fn_free)
{
    gbinder_ipc_tx_pub_init(&priv->pub, self, id, user_data);
    priv->fn_exec = fn_exec;
    priv->fn_done = fn_done;
    priv->fn_free = fn_free;
    priv->completion = gbinder_idle_callback_new(gbinder_ipc_tx_done, priv,
        gbinder_ipc_tx_free);
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
    static const GBinderHandlerFunctions handler_fn = {
        .can_loop = NULL,
        .transact = gbinder_ipc_tx_handler_transact
    };
    GBinderIpcTxInternal* tx = gbinder_ipc_tx_internal_cast(priv);
    GBinderIpcTx* pub = &priv->pub;
    GBinderIpc* self = pub->ipc;
    GBinderObjectRegistry* reg = &self->priv->object_registry;
    GBinderHandler handler = { &handler_fn };

    /* Perform synchronous transaction */
    if (tx->flags & GBINDER_TX_FLAG_ONEWAY) {
        tx->status = gbinder_driver_transact(self->driver, reg, &handler,
            tx->handle, tx->code, tx->req, NULL);
    } else {
        tx->reply = gbinder_remote_reply_new(&self->priv->object_registry);
        tx->status = gbinder_driver_transact(self->driver, reg, &handler,
            tx->handle, tx->code, tx->req, tx->reply);
        if (tx->status != GBINDER_STATUS_OK &&
            gbinder_remote_reply_is_empty(tx->reply)) {
            /* Drop useless reply */
            gbinder_remote_reply_unref(tx->reply);
            tx->reply = NULL;
        }
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

    gbinder_ipc_tx_priv_init(priv, self, id, user_data,
        gbinder_ipc_tx_internal_exec, gbinder_ipc_tx_internal_done,
        gbinder_ipc_tx_internal_free);

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

    gbinder_ipc_tx_priv_init(priv, self, id, user_data,
        gbinder_ipc_tx_custom_exec, gbinder_ipc_tx_custom_done,
        gbinder_ipc_tx_custom_free);

    tx->fn_custom_exec = exec;
    tx->fn_custom_done = done;
    tx->fn_custom_destroy = destroy;

    return priv;
}

/* Invoked on a thread from tx_pool */
static
void
gbinder_ipc_tx_proc(
    gpointer data,
    gpointer object)
{
    GBinderIpcTxPriv* tx = data;

    if (!tx->pub.cancelled) {
        tx->fn_exec(tx);
    } else {
        GVERBOSE_("not executing transaction %lu (cancelled)", tx->pub.id);
    }

    /* The result is handled by the main thread */
    gbinder_idle_callback_schedule(tx->completion);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderIpc*
gbinder_ipc_new(
    const char* dev)
{
    GBinderIpc* self = NULL;
    const GBinderRpcProtocol* protocol;

    if (!dev || !dev[0]) dev = GBINDER_DEFAULT_BINDER;
    protocol = gbinder_rpc_protocol_for_device(dev); /* Never returns NULL */

    /* Lock */
    pthread_mutex_lock(&gbinder_ipc_mutex);
    if (gbinder_ipc_table) {
        self = g_hash_table_lookup(gbinder_ipc_table, dev);
    }
    if (self) {
        gbinder_ipc_ref(self);
    } else {
        GBinderDriver* driver = gbinder_driver_new(dev, protocol);

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
        int ret = gbinder_driver_transact(self->driver, reg, NULL, handle,
            code, req, reply);

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
            NULL, handle, code, req, NULL);
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
            tx->cancelled = TRUE;
            GVERBOSE_("%lu", id);
        } else {
            GWARN("Invalid transaction id %lu", id);
        }
    }
}

gboolean
gbinder_ipc_set_max_threads(
    GBinderIpc* self,
    gint max)
{
    return g_thread_pool_set_max_threads(self->priv->tx_pool, max, NULL);
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
    priv->tx_table = g_hash_table_new(g_direct_hash, g_direct_equal);
    priv->tx_pool = g_thread_pool_new(gbinder_ipc_tx_proc, self,
        GBINDER_IPC_MAX_TX_THREADS, FALSE, NULL);
    priv->object_registry.f = &object_registry_functions;
    priv->self = self;
    self->priv = priv;
}

static
void
gbinder_ipc_stop_loopers(
    GBinderIpc* self)
{
    GBinderIpcPriv* priv = self->priv;
    GBinderIpcLooper* loopers = NULL;

    do {
        GBinderIpcLooper* tmp;

        /* Lock */
        g_mutex_lock(&priv->looper_mutex);
        loopers = gbinder_ipc_looper_stop_all(gbinder_ipc_looper_stop_all(NULL,
            priv->primary_loopers), priv->blocked_loopers);
        priv->blocked_loopers = NULL;
        priv->primary_loopers = NULL;
        g_mutex_unlock(&priv->looper_mutex);
        /* Unlock */

        tmp = loopers;
        while (tmp) {
            GBinderIpcLooper* looper = tmp;

            tmp = looper->next;
            looper->next = NULL;
            gbinder_ipc_looper_join(looper);
            gbinder_ipc_looper_unref(looper);
        }
    } while (loopers);
}

static
void
gbinder_ipc_dispose(
    GObject* object)
{
    GBinderIpc* self = GBINDER_IPC(object);

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

    gbinder_ipc_stop_loopers(self);
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
    if (priv->tx_pool) {
        g_thread_pool_free(priv->tx_pool, FALSE, TRUE);
    }
    GASSERT(!g_hash_table_size(priv->tx_table));
    g_hash_table_unref(priv->tx_table);
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

/* Runs at exit */
void
gbinder_ipc_exit()
{
    GHashTableIter it;
    gpointer key, value;
    GSList* ipcs = NULL;
    GSList* i;

    /* Lock */
    pthread_mutex_lock(&gbinder_ipc_mutex);
    if (gbinder_ipc_table) {
        g_hash_table_iter_init(&it, gbinder_ipc_table);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            ipcs = g_slist_append(ipcs, gbinder_ipc_ref(value));
        }
    }
    pthread_mutex_unlock(&gbinder_ipc_mutex);
    /* Unlock */

    for (i = ipcs; i; i = i->next) {
        GBinderIpc* ipc = GBINDER_IPC(i->data);
        GBinderIpcPriv* priv = ipc->priv;
        GThreadPool* pool = priv->tx_pool;
        GSList* local_objs = NULL;
        GSList* tx_keys = NULL;
        GSList* k;
        GSList* l;

        /* Terminate looper threads */
        GVERBOSE_("%s", ipc->dev);
        gbinder_ipc_stop_loopers(ipc);

        /* Make sure pooled transaction complete too */
        priv->tx_pool = NULL;
        g_thread_pool_free(pool, FALSE, TRUE);

        /*
         * Since this function is supposed to be invoked on the main thread,
         * there's no need to synchronize access to priv->tx_table. In any
         * case, this must be the last thread associated with this object.
         */
        g_hash_table_iter_init(&it, priv->tx_table);
        while (g_hash_table_iter_next(&it, &key, NULL)) {
            tx_keys = g_slist_append(tx_keys, key);
        }
        for (k = tx_keys; k; k = k->next) {
            GBinderIpcTxPriv* tx = g_hash_table_lookup(priv->tx_table, k->data);

            GVERBOSE_("tx %lu", tx->pub.id);
            gbinder_idle_callback_cancel(tx->completion);
        }

        /* The above loop must destroy all uncompleted transactions */
        GASSERT(!g_hash_table_size(priv->tx_table));
        g_slist_free(tx_keys);

        /* Lock */
        g_mutex_lock(&priv->local_objects_mutex);
        if (priv->local_objects) {
            g_hash_table_iter_init(&it, priv->local_objects);
            while (g_hash_table_iter_next(&it, NULL, &value)) {
                local_objs = g_slist_append(local_objs,
                    gbinder_local_object_ref(value));
            }
        }
        g_mutex_unlock(&priv->local_objects_mutex);
        /* Unlock */

        /* Drop remote references */
        for (l = local_objs; l; l = l->next) {
            GBinderLocalObject* obj = GBINDER_LOCAL_OBJECT(l->data);

            while (obj->strong_refs > 0) {
                obj->strong_refs--;
                gbinder_local_object_unref(obj);
            }
        }
        g_slist_free_full(local_objs, g_object_unref);
    }
    g_slist_free_full(ipcs, g_object_unref);
    gbinder_eventloop_set(NULL);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
