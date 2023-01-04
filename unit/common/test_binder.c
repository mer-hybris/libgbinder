/*
 * Copyright (C) 2018-2022 Jolla Ltd.
 * Copyright (C) 2018-2022 Slava Monich <slava.monich@jolla.com>
 * Copyright (C) 2023 Slava Monich <slava@monich.com>
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

#include "test_binder.h"
#include "gbinder_ipc.h"
#include "gbinder_local_object_p.h"
#include "gbinder_system.h"

#define GLOG_MODULE_NAME test_binder_log
#include <gutil_log.h>
GLOG_MODULE_DEFINE2("test_binder", gutil_log_default);

#include <gutil_intarray.h>
#include <gutil_misc.h>

#include <glib-object.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/ioctl.h>

#define gettid() ((int)syscall(SYS_gettid))
#define BINDER_PACKED __attribute__((packed))
#define BINDER_CMDSIZE(code) (_IOC_SIZE(code) + 4)

static GHashTable* test_fd_map = NULL; /* fd => TestBinderFd */
static GHashTable* test_path_map = NULL; /* path => TestBinderNode */
static GPrivate test_tx_stack = G_PRIVATE_INIT(NULL);
static gint32 last_auto_handle = 0;

G_LOCK_DEFINE_STATIC(test_binder);
static GMainLoop* test_binder_exit_loop = NULL;

#define public_fd  fd[0]
#define private_fd fd[1]

#define BINDER_VERSION _IOWR('b', 9, gint32)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, guint32)
#define BINDER_BUFFER_FLAG_HAS_PARENT 0x01

#define TF_ONE_WAY     0x01
#define TF_ROOT_OBJECT 0x04
#define TF_STATUS_CODE 0x08
#define TF_ACCEPT_FDS  0x10

typedef struct test_binder_io TestBinderIo;
typedef struct test_binder_node TestBinderNode;
typedef struct test_binder_cmd TestBinderCmd;

/*
 * tid > 0  for this specific thread
 * tid = 0  for any thread (ANY_THREAD)
 * tid < 0  special value (TX_THREAD etc)
 */
struct test_binder_cmd {
    int tid;
    const guint32* data;
    TestBinderCmd* next;
};

struct test_binder_io {
    int version;
    int write_read_request;
    int (*handle_write_read)(TestBinderNode* node, void* data);
};

struct test_binder_node {
    gint refcount;
    int owner;
    int fd[2];
    char* path;
    gint ignore_dead_object;
    const char* name;
    const TestBinderIo* io;
    GMutex mutex;
    GUtilIntArray* loopers;   /* tids of the loopers */
    GHashTable* object_map;   /* GBinderLocalObject* => handle */
    GHashTable* handle_map;   /* handle => GBinderLocalObject* */
    GHashTable* destroy_map;  /* gpointer => GDestroyNotify */
    TestBinderCmd* cmd_first;
    TestBinderCmd* cmd_last;
};

typedef struct test_binder_fd {
    int fd;
    TestBinderNode* node;
} TestBinderFd;

typedef
void
(*TestBinderPushDataFunc)(
    int fd,
    const void* data);

typedef struct binder_write_read_64 {
    guint64 write_size;
    guint64 write_consumed;
    guint64 write_buffer;
    guint64 read_size;
    guint64 read_consumed;
    guint64 read_buffer;
} BinderWriteRead64;

typedef union binder_target_64 {
    guint32 handle;
    guint64 ptr;
} BinderTarget64;

typedef struct binder_transaction_data_64 {
    BinderTarget64 target;
    guint64 cookie;
    guint32 code;
    guint32 flags;
    gint32 sender_pid;
    gint32 sender_euid;
    guint64 data_size;
    guint64 offsets_size;
    guint64 data_buffer;
    guint64 data_offsets;
} BinderTransactionData64;

typedef struct binder_transaction_data_sg_64 {
    BinderTransactionData64 tx;
    guint64 buffers_size;
} BinderTransactionDataSg64;

typedef struct binder_pre_cookie_64 {
    guint64 ptr;
    guint64 cookie;
} BinderPtrCookie64;

typedef struct binder_handle_cookie_64 {
    guint32 handle;
    guint64 cookie;
} BINDER_PACKED BinderHandleCookie64;

typedef struct binder_object_64 {
    guint32 type;  /* BINDER_TYPE_BINDER */
    guint32 flags;
    union {
        guint32 handle;
        guint64 ptr;
    };
    guint64 cookie;
} BinderObject64;

typedef  struct binder_buffer_64 {
    guint32 type;  /* BINDER_TYPE_PTR */
    guint32 flags;
    guint64 buffer;
    guint64 length;
    guint64 parent;
    guint64 parent_offset;
} BinderBuffer64;

#define BC_TRANSACTION_64       _IOW('c', 0, BinderTransactionData64)
#define BC_REPLY_64             _IOW('c', 1, BinderTransactionData64)
#define BC_FREE_BUFFER_64       _IOW('c', 3, guint64)
#define BC_INCREFS              _IOW('c', 4, guint32)
#define BC_ACQUIRE              _IOW('c', 5, guint32)
#define BC_RELEASE              _IOW('c', 6, guint32)
#define BC_DECREFS              _IOW('c', 7, guint32)
#define BC_ACQUIRE_DONE_64      _IOW('c', 9, BinderPtrCookie64)
#define BC_ENTER_LOOPER          _IO('c', 12)
#define BC_EXIT_LOOPER           _IO('c', 13)
#define BC_REQUEST_DEATH_NOTIFICATION_64 _IOW('c', 14, BinderHandleCookie64)
#define BC_CLEAR_DEATH_NOTIFICATION_64   _IOW('c', 15, BinderHandleCookie64)
#define BC_DEAD_BINDER_DONE     _IOW('c', 16, guint64)
#define BC_TRANSACTION_SG_64    _IOW('c', 17, BinderTransactionDataSg64)
#define BC_REPLY_SG_64          _IOW('c', 18, BinderTransactionDataSg64)

#define BR_TRANSACTION_64       _IOR('r', 2, BinderTransactionData64)
#define BR_REPLY_64             _IOR('r', 3, BinderTransactionData64)
#define BR_DEAD_REPLY            _IO('r', 5)
#define BR_TRANSACTION_COMPLETE  _IO('r', 6)
#define BR_INCREFS_64           _IOR('r', 7, BinderPtrCookie64)
#define BR_ACQUIRE_64           _IOR('r', 8, BinderPtrCookie64)
#define BR_RELEASE_64           _IOR('r', 9, BinderPtrCookie64)
#define BR_DECREFS_64           _IOR('r', 10, BinderPtrCookie64)
#define BR_NOOP                  _IO('r', 12)
#define BR_DEAD_BINDER_64       _IOR('r', 15, guint64)
#define BR_CLEAR_DEATH_NOTIFICATION_DONE_64 _IOR('r', 16, guint64)
#define BR_FAILED_REPLY          _IO('r', 17)

typedef enum cmd_dequeue_flags {
    CMD_DEQUEUE_FLAGS_NONE = 0,
    CMD_DEQUEUE_FLAG_STOP_AT_TRANSACTION = 0x01,
    CMD_DEQUEUE_FLAG_STOP_AT_TRANSACTION_COMPLETE = 0x02,
    CMD_DEQUEUE_FLAG_STOP_AT_REPLY = 0x04,
} CMD_DEQUEUE_FLAGS;

#define test_binder_cmd_free(cmd) g_free(cmd)

/* See comment in test_binder_node_free() explaining what this is for */
typedef struct test_binder_destroy_data {
    GDestroyNotify destroy;
    void* data;
} TestBinderDestroyData;

static
void
test_binder_destroy_data_free1(
    gpointer list_data)
{
    TestBinderDestroyData* entry = list_data;

    entry->destroy(entry->data);
    g_free(entry);
}

static
void
test_binder_destroy_data_free(
    gpointer list)
{
    g_slist_free_full(list, test_binder_destroy_data_free1);
}

static GPrivate test_destroy_data = G_PRIVATE_INIT
    (test_binder_destroy_data_free);

static
void
test_binder_destroy_data_add(
    GDestroyNotify destroy,
    void* data)
{
    GSList* list = g_private_get(&test_destroy_data);
    TestBinderDestroyData* entry = g_new(TestBinderDestroyData, 1);

    entry->destroy = destroy ? destroy : g_free;
    entry->data = data;
    g_private_set(&test_destroy_data, g_slist_append(list, entry));
}

static
void
test_binder_destroy_data_flush()
{
    GSList* list = g_private_get(&test_destroy_data);

    g_private_set(&test_destroy_data, NULL);
    test_binder_destroy_data_free(list);
}

static
TestBinderNode*
test_binder_node_new(
    const char* path,
    const TestBinderIo* io)
{
    TestBinderNode* node = g_new0(TestBinderNode, 1);
    const char* name;

    g_atomic_int_set(&node->refcount, 1);
    g_assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, node->fd));
    node->path = g_strdup(path);
    name = strrchr(node->path, '/');
    node->name = (name ? (name + 1) : node->path);
    node->io = io;
    g_mutex_init(&node->mutex);
    node->loopers = gutil_int_array_new();
    node->object_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    node->handle_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    node->destroy_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    GDEBUG("Created node %s (%d)", path, node->public_fd);
    return node;
}

static
void
test_binder_node_free(
    TestBinderNode* node)
{
    TestBinderCmd* cmd;
    GHashTableIter it;
    gpointer key, value;

    /*
     * There's a tiny race condition on exit. The last handle may be
     * closed before all looper threads have finished. Those loopers
     * may still keep pointers to the buffers received from the binder.
     *
     * We have to postpone destruction of those buffers until
     * gbinder_ipc_exit() is done (or until the thread holding
     * the last reference to TestBinderNode has exited).
     */
    g_hash_table_iter_init(&it, node->destroy_map);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        test_binder_destroy_data_add(value, key);
    }

    g_assert_cmpuint(g_hash_table_size(node->object_map), == ,0);
    g_assert_cmpuint(g_hash_table_size(node->handle_map), == ,0);

    while ((cmd = node->cmd_first)) {
        node->cmd_first = cmd->next;
        test_binder_cmd_free(cmd);
    }

    GDEBUG("Destroyed node %s (%d)", node->path, node->public_fd);
    g_hash_table_unref(node->object_map);
    g_hash_table_unref(node->handle_map);
    g_hash_table_unref(node->destroy_map);
    gutil_int_array_free(node->loopers, TRUE);
    g_assert(!close(node->fd[0]));
    g_assert(!close(node->fd[1]));
    g_mutex_clear(&node->mutex);
    g_free(node->path);
    g_free(node);
}

static
void
test_binder_node_unref(
    TestBinderNode* node)
{
    if (node && g_atomic_int_dec_and_test(&node->refcount)) {
        G_LOCK(test_binder);
        if (test_path_map) {
            if (g_hash_table_lookup(test_path_map, node->path) != node ||
                g_atomic_int_get(&node->refcount) > 0) {
                /* Someone else has re-referenced or deleted this object */
                node = NULL;
            } else {
                /* Remove it from the map and check if it's the last one */
                g_assert(g_hash_table_remove(test_path_map, node->path));
                if (!g_hash_table_size(test_path_map)) {
                    g_hash_table_unref(test_path_map);
                    test_path_map = NULL;
                    if (test_binder_exit_loop) {
                        GDEBUG("All nodes are gone");
                        test_quit_later(test_binder_exit_loop);
                    }
                }
            }
        }
        G_UNLOCK(test_binder);
        if (node) {
            test_binder_node_free(node);
        }
    }
}

static
TestBinderNode*
test_binder_node_ref(
    TestBinderNode* node)
{
    if (node) {
        g_atomic_int_inc(&node->refcount);
    }
    return node;
}

static
void
test_binder_node_lock(
    TestBinderNode* node)
{
    g_mutex_lock(&node->mutex);
    g_assert_cmpint(node->owner, == ,0);
    node->owner = gettid();
}

static
void
test_binder_node_unlock(
    TestBinderNode* node)
{
    g_assert_cmpint(node->owner, == ,gettid());
    node->owner = 0;
    g_mutex_unlock(&node->mutex);
}

static
TestBinderFd*
test_binder_fd_new(
    TestBinderNode* node)
{
    TestBinderFd* binder_fd = g_new0(TestBinderFd, 1);

    binder_fd->fd = dup(node->public_fd);
    binder_fd->node = test_binder_node_ref(node);
    GDEBUG("Opened fd %d %s", binder_fd->fd, node->path);
    return binder_fd;
}

static
void
test_binder_fd_free(
    TestBinderFd* binder_fd)
{
    test_binder_node_unref(binder_fd->node);
    close(binder_fd->fd);
    g_free(binder_fd);
}

static
TestBinderNode*
test_binder_node_ref_from_fd(
    int fd)
{
    TestBinderNode* node = NULL;

    /*
     * On exit, gbinder_system_ioctl() may be called after the binder
     * device has been closed.
     */
    if (fd >= 0) {
        G_LOCK(test_binder);
        if (test_fd_map) {
            TestBinderFd* binder_fd = g_hash_table_lookup
                (test_fd_map, GINT_TO_POINTER(fd));

            if (binder_fd) {
                node = test_binder_node_ref(binder_fd->node);
            }
        }
        G_UNLOCK(test_binder);
    }

    return node;
}

static
int
test_binder_cmd_size(
    const TestBinderCmd* cmd)
{
    return cmd ? BINDER_CMDSIZE(cmd->data[0]) : 0;
}

static
guint32
test_binder_cmd_code(
    const TestBinderCmd* cmd)
{
    return cmd ? cmd->data[0] : 0;
}

static
void*
test_binder_cmd_payload(
    const TestBinderCmd* cmd)
{
    return cmd ? (void*)(cmd->data + 1) : NULL;
}

static
TestBinderCmd*
test_binder_cmd_new(
    int tid,
    const void* data)
{
    const gsize size = _IOC_SIZE(((guint32*)data)[0]) + 4;
    TestBinderCmd* cmd = g_malloc(sizeof(TestBinderCmd) + size);
    void* storage = cmd + 1;

    memcpy(storage, data, size);
    cmd->tid = tid;
    cmd->data = storage;
    cmd->next = NULL;
    return cmd;
}

static
void
test_binder_node_queue_cmd_locked(
    TestBinderNode* node,
    TestBinderCmd* cmd)
{
    guint8 ping = 0;

    g_assert(!cmd->next);
    if (node->cmd_last) {
        g_assert(node->cmd_first);
        node->cmd_last->next = cmd;
        node->cmd_last = cmd;
    } else {
        g_assert(!node->cmd_first);
        node->cmd_first = node->cmd_last = cmd;
    }
    g_assert_cmpint(write(node->private_fd, &ping, 1), > ,0);
}

static
void
test_binder_node_queue_cmd(
    TestBinderNode* node,
    TestBinderCmd* cmd)
{
    /* Lock */
    test_binder_node_lock(node);
    test_binder_node_queue_cmd_locked(node, cmd);
    test_binder_node_unlock(node);
    /* Unlock */
}

#define test_binder_node_queue_cmd_data(node,tid,ptr) \
    test_binder_node_queue_cmd(node, test_binder_cmd_new(tid, ptr))
#define test_binder_node_queue_cmd_data_locked(node,tid,ptr) \
    test_binder_node_queue_cmd_locked(node, test_binder_cmd_new(tid, ptr))

static
TestBinderCmd*
test_binder_node_dequeue_cmd_locked(
    TestBinderNode* node,
    int max_size,
    CMD_DEQUEUE_FLAGS flags)
{
    TestBinderCmd* prev = NULL;
    TestBinderCmd* cmd;
    const int tid = gettid();

    for (cmd = node->cmd_first; cmd; cmd = cmd->next) {
        if (!cmd->tid || cmd->tid == tid ||
           (cmd->tid == LOOPER_THREAD &&
            gutil_int_array_contains(node->loopers, tid))) {
            const guint32 code = test_binder_cmd_code(cmd);
            BinderTransactionData64* tx = NULL;

            /* The command matches the tid criteria */
            switch (code) {
            case BR_TRANSACTION_64:
                if (flags & CMD_DEQUEUE_FLAG_STOP_AT_TRANSACTION) {
                    cmd = NULL;
                } else {
                    tx = test_binder_cmd_payload(cmd);
                }
                break;
            case BR_TRANSACTION_COMPLETE:
                if (flags & CMD_DEQUEUE_FLAG_STOP_AT_TRANSACTION_COMPLETE) {
                    cmd = NULL;
                }
                break;
            case BR_REPLY_64:
                if (flags & CMD_DEQUEUE_FLAG_STOP_AT_REPLY) {
                    cmd = NULL;
                } else {
                    tx = test_binder_cmd_payload(cmd);
                }
                break;
            case BR_FAILED_REPLY:
            case BR_DEAD_REPLY:
                if (flags & CMD_DEQUEUE_FLAG_STOP_AT_REPLY) {
                    cmd = NULL;
                }
                break;
            }

            if (cmd && BINDER_CMDSIZE(code) > max_size) {
                GDEBUG("Cmd 0x%08x is too large (%d > %d)", code,
                    BINDER_CMDSIZE(code), max_size);
                cmd = NULL;
            } else if (tx && !tx->cookie) {
                /* We use cookie to store tid of the sender */
                tx->cookie = (cmd->tid > 0) ? cmd->tid : tid;
            }
            break;
        }
        prev = cmd;
    }

    if (cmd) {
        TestBinderCmd* next = cmd->next;
        guint8 pong = 0;

        if (prev) {
            if (!(prev->next = next)) {
                node->cmd_last = prev;
            }
        } else {
            if (!(node->cmd_first = next)) {
                node->cmd_last = NULL;
            }
        }
        g_assert_cmpint(read(node->public_fd, &pong, 1), > ,0);
    }

    return cmd;
}

static
void
test_binder_node_fix_affinity(
    TestBinderNode* node,
    TEST_BR_THREAD mark)
{
    const int tid = gettid();
    TestBinderCmd* cmd;

    /* Lock */
    test_binder_node_lock(node);
    for (cmd = node->cmd_first; cmd; cmd = cmd->next) {
        if (cmd->tid == mark) {
            const guint32 code = test_binder_cmd_code(cmd);

            cmd->tid = tid;
            GDEBUG("Cmd 0x%08x affinity changed to %d", code, tid);
            if (code == BR_REPLY_64 || code == BR_TRANSACTION_64) {
                BinderTransactionData64* tx = test_binder_cmd_payload(cmd);

                g_assert(!tx->cookie);
                tx->cookie = tid;
            }
        }
    }
    test_binder_node_unlock(node);
    /* Unlock */
}

static
void
test_binder_short_wait()
{
    usleep(100000); /* 100 ms */
}

static
gboolean
test_binder_node_ignore_dead_object(
    TestBinderNode* node)
{
    gboolean ignore;

    /* Lock */
    test_binder_node_lock(node);
    if (node->ignore_dead_object > 0) {
        node->ignore_dead_object--;
        ignore = TRUE;
    } else {
        ignore = FALSE;
    }
    test_binder_node_unlock(node);
    /* Unlock */

    return ignore;
}

static
void
test_binder_node_br_dead_object_64(
    TestBinderNode* node,
    guint64 handle)
{
    guint8 br[BINDER_CMDSIZE(BR_DEAD_BINDER_64)];
    guint32* code = (guint32*)br;
    guint64* payload = (guint64*)(code + 1);

    *code = BR_DEAD_BINDER_64;
    *payload = handle;
    test_binder_node_queue_cmd_data(node, ANY_THREAD, br);
}

static
void
test_binder_local_object_gone(
    gpointer data,
    GObject* obj)
{
    TestBinderNode* node = data;
    gssize handle = -1;

    /*
     * TestBinderNode must be alive because if gets detroyed after
     * all local objects are gone.
     */

    /* Lock */
    test_binder_node_lock(node);
    GDEBUG("Object %p is gone", obj);
    if (g_hash_table_contains(node->object_map, obj)) {
        gpointer key = g_hash_table_lookup(node->object_map, obj);

        handle = GPOINTER_TO_SIZE(key);
        g_hash_table_remove(node->handle_map, key);
        g_hash_table_remove(node->object_map, obj);
    }
    test_binder_node_unlock(node);
    /* Unlock */

    if (handle >= 0) {
        test_binder_node_br_dead_object_64(node, handle);
    }

    test_binder_node_unref(node);
}

static
guint
test_binder_register_object_locked(
    TestBinderNode* node,
    GBinderLocalObject* obj,
    guint h)
{
    g_assert(G_TYPE_CHECK_INSTANCE_TYPE(obj, GBINDER_TYPE_LOCAL_OBJECT));
    g_assert(!g_hash_table_contains(node->object_map, obj));
    g_assert(!g_hash_table_contains(node->handle_map, GINT_TO_POINTER(h)));
    if (h == AUTO_HANDLE) {
        h = g_atomic_int_add(&last_auto_handle, 1) + 1;
        while (g_hash_table_contains(node->handle_map, GINT_TO_POINTER(h)) ||
            g_hash_table_contains(node->object_map, GINT_TO_POINTER(h))) {
            h = g_atomic_int_add(&last_auto_handle, 1) + 1;
        }
    } else if (last_auto_handle < h) {
        /* Avoid re-using handles, to make debugging easier */
        g_atomic_int_set(&last_auto_handle, h);
    }
    GDEBUG("Object %p <=> handle %u %s", obj, h, node->name);
    g_hash_table_insert(node->handle_map, GINT_TO_POINTER(h), obj);
    g_hash_table_insert(node->object_map, obj, GINT_TO_POINTER(h));
    g_object_weak_ref(G_OBJECT(obj), test_binder_local_object_gone,
        test_binder_node_ref(node));
    return h;
}

static
guint64
test_binder_node_object_to_handle_locked(
    TestBinderNode* node,
    guint64 obj)
{
    gpointer key = GSIZE_TO_POINTER(obj);

    if (g_hash_table_contains(node->object_map, key)) {
        gpointer value = g_hash_table_lookup(node->object_map, key);
        guint64 handle = GPOINTER_TO_SIZE(value);

        GDEBUG("Object %p => handle %u %s", key, (guint) handle, node->name);
        return handle;
    } else if (key) {
        GDEBUG("Auto-registering object %p %s", key, node->name);
        return test_binder_register_object_locked(node, key, AUTO_HANDLE);
    } else {
        GDEBUG("Unexpected object %p %s", key, node->name);
        return 0;
    }
}

static
guint64
test_binder_node_handle_to_object_locked(
    TestBinderNode* node,
    guint64 handle)
{
    gpointer key = GSIZE_TO_POINTER(handle);

    if (g_hash_table_contains(node->handle_map, key)) {
        gpointer obj = g_hash_table_lookup(node->handle_map, key);

        GDEBUG("Handle %u => object %p %s", (guint) handle, obj, node->name);
        return GPOINTER_TO_SIZE(obj);
    }
    return 0;
}

static
guint64
test_binder_node_handle_to_object(
    TestBinderNode* node,
    guint64 handle)
{
    guint64 obj;

    /* Lock */
    test_binder_node_lock(node);
    obj = test_binder_node_handle_to_object_locked(node, handle);
    test_binder_node_unlock(node);
    /* Unlock */

    return obj;
}

static
void
test_binder_node_fixup_handles_64(
    TestBinderNode* node,
    BinderTransactionData64* tx)
{
    guint i;
    guint8* data_buffer = gutil_memdup
        (GSIZE_TO_POINTER(tx->data_buffer), tx->data_size);
    guint64* data_offsets = gutil_memdup
        (GSIZE_TO_POINTER(tx->data_offsets),  tx->offsets_size);

    /* Lock */
    test_binder_node_lock(node);
    for (i = 0; i < tx->offsets_size/sizeof(guint64); i++) {
        guint32* obj_ptr = (guint32*)(data_buffer + data_offsets[i]);

        if (*obj_ptr == BINDER_TYPE_BINDER) {
            BinderObject64* object = (BinderObject64*) obj_ptr;

            if (object->ptr) {
                object->type = BINDER_TYPE_HANDLE;
                object->handle = test_binder_node_object_to_handle_locked(node,
                    object->ptr);
            }
        } else if (*obj_ptr == BINDER_TYPE_PTR) {
            BinderBuffer64* buffer = (BinderBuffer64*) obj_ptr;

            if (buffer->buffer) {
                void* copy = gutil_memdup
                    (GSIZE_TO_POINTER(buffer->buffer), buffer->length);

                if (buffer->flags & BINDER_BUFFER_FLAG_HAS_PARENT) {
                    /* Fix pointer from the parent buffer */
                    BinderBuffer64* parent_buffer = (void*)
                        (data_buffer + data_offsets[buffer->parent]);
                    guint64* parent_ptr = GSIZE_TO_POINTER
                        (parent_buffer->buffer + buffer->parent_offset);

                    g_assert(parent_buffer->type == BINDER_TYPE_PTR);
                    g_assert(*parent_ptr == buffer->buffer);
                    *parent_ptr = GPOINTER_TO_SIZE(copy);
                }
                buffer->buffer = GPOINTER_TO_SIZE(copy);
                g_hash_table_replace(node->destroy_map, copy, NULL);
            }
        } else if (*obj_ptr != BINDER_TYPE_HANDLE) {
            GDEBUG("Unexpected object type 0x%08x", *obj_ptr);
        }
    }
    g_hash_table_replace(node->destroy_map, data_buffer, NULL);
    g_hash_table_replace(node->destroy_map, data_offsets, NULL);
    test_binder_node_unlock(node);
    /* Unlock */

    tx->data_buffer = GPOINTER_TO_SIZE(data_buffer);
    tx->data_offsets = GPOINTER_TO_SIZE(data_offsets);
}

static
void
test_binder_node_br_clear_death_notification_done_64(
    TestBinderNode* node,
    guint64 cookie)
{
    guint8 br[BINDER_CMDSIZE(BR_CLEAR_DEATH_NOTIFICATION_DONE_64)];
    guint32* code = (guint32*)br;
    guint64* payload = (guint64*)(code + 1);

    *code = BR_CLEAR_DEATH_NOTIFICATION_DONE_64;
    *payload = cookie;
    test_binder_node_queue_cmd_data(node, gettid(), br);
}

static
void
test_binder_node_bc_free_buffer_64(
    TestBinderNode* node,
    const guint64* payload)
{
    gpointer ptr = GSIZE_TO_POINTER(*payload);

    if (ptr) {
        GDestroyNotify destroy;

        /* Lock */
        test_binder_node_lock(node);
        destroy = g_hash_table_lookup(node->destroy_map, ptr);
        g_hash_table_remove(node->destroy_map, ptr);
        if (!destroy) {
            destroy = g_free;
        }
        test_binder_node_unlock(node);
        /* Unlock */

        destroy(ptr);
    }
}

static
void
test_binder_node_bc_clear_death_notification_64(
    TestBinderNode* node,
    const BinderHandleCookie64* bc)
{
    test_binder_node_br_clear_death_notification_done_64(node, bc->cookie);
}

static
void
test_binder_node_bc_transaction_64(
    TestBinderNode* node,
    const BinderTransactionData64* bc)
{
    const int tid = gettid();
    guint64 obj = test_binder_node_handle_to_object(node, bc->target.handle);

    test_binder_node_fix_affinity(node, TX_THREAD);
    if (obj) {
        guint8 br[BINDER_CMDSIZE(BR_TRANSACTION_64)];
        guint32* code = (guint32*)br;
        BinderTransactionData64* tx = (BinderTransactionData64*)(code + 1);

        *code = BR_TRANSACTION_64;
        *tx = *bc;
        g_assert(!tx->cookie); /* We use cookie to store tid of the sender */
        tx->cookie = tid;
        tx->sender_pid = getpid();
        tx->sender_euid = geteuid();
        tx->target.ptr = obj;
        test_binder_node_fixup_handles_64(node, tx);
        test_binder_node_queue_cmd_data(node, ANY_THREAD, br);
    } else if (test_binder_node_ignore_dead_object(node)) {
        GDEBUG("No object for handle %u (expected)", bc->target.handle);
    } else {
        const guint32 br_dead_reply = BR_DEAD_REPLY;

        GDEBUG("No object for handle %u", bc->target.handle);
        test_binder_node_queue_cmd_data(node, tid, &br_dead_reply);
    }
}

static
void
test_binder_node_bc_reply_64(
    TestBinderNode* node,
    const BinderTransactionData64* bc)
{
    const guint32 br_noop = BR_NOOP;
    const guint32 br_transaction_complete = BR_TRANSACTION_COMPLETE;
    guint8 br_reply[BINDER_CMDSIZE(BR_REPLY_64)];
    guint32* code = (guint32*)br_reply;
    BinderTransactionData64* tx = (BinderTransactionData64*)(code + 1);
    GUtilIntArray* tx_stack = g_private_get(&test_tx_stack);
    int tid;

    *code = BR_REPLY_64;
    *tx = *bc;
    tx->sender_pid = getpid();
    tx->sender_euid = geteuid();
    test_binder_node_fixup_handles_64(node, tx);

    /* Pop tid of the thread waiting for the completion */
    g_assert(tx_stack);
    tid = tx_stack->data[tx_stack->count - 1];
    GDEBUG("Reply thread %d", tid);
    if (tx_stack->count == 1) {
        /* This thread is done with its transactions */
        gutil_int_array_free(tx_stack, TRUE);
        g_private_set(&test_tx_stack, NULL);
    } else {
        gutil_int_array_set_count(tx_stack, tx_stack->count - 1);
    }

    /* Queue BR_TRANSACTION_COMPLETE, BR_NOOP and the reply */
    /* Lock */
    test_binder_node_lock(node);
    test_binder_node_queue_cmd_data_locked(node, tid, &br_transaction_complete);
    test_binder_node_queue_cmd_data_locked(node, tid, &br_noop);
    test_binder_node_queue_cmd_data_locked(node, tid, br_reply);
    test_binder_node_unlock(node);
    /* Unlock */
}

static
void
test_binder_node_bc_acquire_release(
    TestBinderNode* node,
    guint32 code,
    const guint32* handle)
{
    guint64 ptr;

    g_assert_cmpuint(_IOC_SIZE(code), == ,sizeof(BinderPtrCookie64));

    /* Lock */
    test_binder_node_lock(node);
    ptr = test_binder_node_handle_to_object_locked(node, *handle);
    if (ptr) {
        guint8 br[sizeof(guint32) + sizeof(BinderPtrCookie64)];
        BinderPtrCookie64* data = (void*)(br + sizeof(code));

        memcpy(br, &code, sizeof(code));
        memset(data, 0, sizeof(*data));
        data->ptr = ptr;

        /* These commands are not tied to a particular thread */
        test_binder_node_queue_cmd_data_locked(node, ANY_THREAD, br);
    } else {
        /* This may actually be expected in some weird unit tests */
        GDEBUG("Invalid handle %u %s", *handle, node->name);
    }
    test_binder_node_unlock(node);
    /* Unlock */
}

static
void
test_binder_node_handle_cmd_64(
    TestBinderNode* node,
    const guint32* cmd)
{
    const guint32 code = cmd[0];
    const void* payload = cmd + 1;
    const int tid = gettid();

    switch (code) {
    case BC_ENTER_LOOPER:

        /* Lock */
        test_binder_node_lock(node);
        g_assert(!gutil_int_array_contains(node->loopers, tid));
        gutil_int_array_append(node->loopers, tid);
        test_binder_node_unlock(node);
        /* Unlock */

        GDEBUG("Thread %d is a looper", tid);
        break;
    case BC_EXIT_LOOPER:

        /* Lock */
        test_binder_node_lock(node);
        g_assert(gutil_int_array_remove(node->loopers, tid));
        test_binder_node_unlock(node);
        /* Unlock */

        GDEBUG("Thread %d is no longer a looper", tid);
        break;
    case BC_CLEAR_DEATH_NOTIFICATION_64:
        test_binder_node_bc_clear_death_notification_64(node, payload);
        break;
    case BC_FREE_BUFFER_64:
        test_binder_node_bc_free_buffer_64(node, payload);
        break;
    case BC_TRANSACTION_64:
    case BC_TRANSACTION_SG_64:
        test_binder_node_bc_transaction_64(node, payload);
        break;
    case BC_REPLY_64:
    case BC_REPLY_SG_64:
        test_binder_node_bc_reply_64(node, payload);
        break;
    case BC_ACQUIRE:
        test_binder_node_bc_acquire_release(node, BR_ACQUIRE_64, payload);
        break;
    case BC_RELEASE:
        test_binder_node_bc_acquire_release(node, BR_RELEASE_64, payload);
        break;
    case BC_ACQUIRE_DONE_64:
    case BC_DEAD_BINDER_DONE:
    case BC_REQUEST_DEATH_NOTIFICATION_64:
        /* Ignore these for now */
        break;
    default:
        GDEBUG("Ignoring cmd 0x%08x", code);
        break;
    }
}

static
void
test_binder_node_handle_br_transaction_64_locked(
    TestBinderNode* node,
    const BinderTransactionData64* tx)
{
    /* We use cookie to store tid of the sender */
    const int sender = (int) tx->cookie;

    g_assert(sender);
    GDEBUG("Thread %d receives %stransaction from %d", gettid(),
        (tx->flags & TF_ONE_WAY) ? "one-way "  : "", sender);

    if (tx->flags & TF_ONE_WAY) {
        const guint32 br_noop = BR_NOOP;
        const guint32 br_transaction_complete = BR_TRANSACTION_COMPLETE;

        /* Let the sender know that transaction has been delivered */
        test_binder_node_queue_cmd_data_locked(node, sender, &br_noop);
        test_binder_node_queue_cmd_data_locked(node, sender,
            &br_transaction_complete);
    } else {
        GUtilIntArray* tx_stack = g_private_get(&test_tx_stack);

        if (!tx_stack) {
            g_private_set(&test_tx_stack, tx_stack = gutil_int_array_new());
        }

        /* Push sender's tid to the transaction stack and wait for reply */
        gutil_int_array_append(tx_stack, sender);
    }
}

static
int
test_binder_io_handle_write_read_64(
    TestBinderNode* node,
    void* data)
{
    BinderWriteRead64* wr = data;
    gssize bytes_left = wr->write_size - wr->write_consumed;
    guint8* read_ptr = GSIZE_TO_POINTER(wr->read_buffer + wr->read_consumed);
    const guint8* write_ptr = GSIZE_TO_POINTER(wr->write_buffer +
        wr->write_consumed);
    int read_total = 0, avail = wr->read_size - wr->read_consumed;
    CMD_DEQUEUE_FLAGS flags = CMD_DEQUEUE_FLAGS_NONE;
    TestBinderCmd* cmd;

    /* Write */

    while (bytes_left >= sizeof(guint32)) {
        const guint* cmd = (guint32*)write_ptr;
        const gsize cmdsize = BINDER_CMDSIZE(*cmd);

        /* Not expecting partial commands in the buffer */
        g_assert(bytes_left >= cmdsize);
        test_binder_node_handle_cmd_64(node, cmd);
        wr->write_consumed += cmdsize;
        bytes_left -= cmdsize;
        write_ptr += cmdsize;
    }

    /* Read */

    /* Lock */
    test_binder_node_lock(node);
    while (avail && (cmd = test_binder_node_dequeue_cmd_locked(node,
        avail, flags))) {
        const int nbytes = test_binder_cmd_size(cmd);
        const guint32 code = test_binder_cmd_code(cmd);

        g_assert_cmpint(nbytes, <= ,avail);
        switch (code) {
        case BR_TRANSACTION_64:
            flags |= CMD_DEQUEUE_FLAG_STOP_AT_TRANSACTION;
            test_binder_node_handle_br_transaction_64_locked(node,
                test_binder_cmd_payload(cmd));
            break;
        case BR_TRANSACTION_COMPLETE:
            flags |= CMD_DEQUEUE_FLAG_STOP_AT_TRANSACTION_COMPLETE;
            break;
        case BR_REPLY_64:
        case BR_FAILED_REPLY:
        case BR_DEAD_REPLY:
            flags |= CMD_DEQUEUE_FLAG_STOP_AT_REPLY;
            break;
        }

        memcpy(read_ptr, cmd->data, nbytes);
        wr->read_consumed += nbytes;
        read_total += nbytes;
        avail -= nbytes;
        read_ptr += nbytes;
        test_binder_cmd_free(cmd);
    }
    test_binder_node_unlock(node);
    /* Unlock */

    if (!read_total) {
        /* Nothing was read */
        test_binder_short_wait();
    }
    return 0;
}

static const TestBinderIo test_binder_io_64 = {
    .version = 8,
    .write_read_request = _IOWR('b', 1, BinderWriteRead64),
    .handle_write_read = test_binder_io_handle_write_read_64
};

static
int
test_binder_ioctl_version(
    TestBinderNode* node,
    int* version)
{
    *version = node->io->version;
    return 0;
}

static
void
test_io_destroy_none(
    gpointer data)
{
    GDEBUG("Not freeing %p", data);
}

void
test_binder_set_destroy(
    int fd,
    gpointer ptr,
    GDestroyNotify destroy)
{
    TestBinderNode* node = test_binder_node_ref_from_fd(fd);

    if (node) {

        /* Lock */
        test_binder_node_lock(node);
        g_hash_table_replace(node->destroy_map, ptr,
            destroy ? destroy : test_io_destroy_none);
        test_binder_node_unlock(node);
        /* Unlock */

        test_binder_node_unref(node);
    }
}

static
void
test_binder_push_data(
    int fd,
    TEST_BR_THREAD dest,
    const void* data)
{
    TestBinderNode* node = test_binder_node_ref_from_fd(fd);

    if (node) {
        test_binder_node_queue_cmd_data(node, (dest == THIS_THREAD) ?
            gettid() : dest, data);
        test_binder_node_unref(node);
    }
}

static
void
test_binder_push_ptr_cookie(
    int fd,
    TEST_BR_THREAD dest,
    guint32 cmd,
    void* ptr)
{
    guint8 buf[sizeof(guint32) + sizeof(BinderPtrCookie64)];
    BinderPtrCookie64* data = (void*)(buf + sizeof(cmd));

    g_assert_cmpuint(_IOC_SIZE(cmd), == ,sizeof(BinderPtrCookie64));
    memcpy(buf, &cmd, sizeof(cmd));
    memset(data, 0, sizeof(*data));
    data->ptr = GPOINTER_TO_SIZE(ptr);

    test_binder_push_data(fd, dest, buf);
}

void
test_binder_br_noop(
    int fd,
    TEST_BR_THREAD dest)
{
    guint32 cmd = BR_NOOP;

    test_binder_push_data(fd, dest, &cmd);
}

void
test_binder_br_increfs(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr)
{
    test_binder_push_ptr_cookie(fd, dest, BR_INCREFS_64, ptr);
}

void
test_binder_br_acquire(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr)
{
    test_binder_push_ptr_cookie(fd, dest, BR_ACQUIRE_64, ptr);
}

void
test_binder_br_release(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr)
{
    test_binder_push_ptr_cookie(fd, dest, BR_RELEASE_64, ptr);
}

void
test_binder_br_decrefs(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr)
{
    test_binder_push_ptr_cookie(fd, dest, BR_DECREFS_64, ptr);
}

void
test_binder_br_transaction_complete(
    int fd,
    TEST_BR_THREAD dest)
{
    guint32 cmd = BR_TRANSACTION_COMPLETE;

    test_binder_push_data(fd, dest, &cmd);
}

void
test_binder_br_dead_binder(
    int fd,
    TEST_BR_THREAD dest,
    guint handle)
{
    const guint64 handle64 = handle;
    guint32 br[3];

    br[0] = BR_DEAD_BINDER_64;
    memcpy(br + 1, &handle64, sizeof(handle64));

    test_binder_push_data(fd, dest, br);
}

void
test_binder_br_dead_binder_obj(
    int fd,
    GBinderLocalObject* obj)
{
    if (obj) {
        TestBinderNode* node = test_binder_node_ref_from_fd(fd);

        g_assert(node);

        /* Lock */
        test_binder_node_lock(node);
        if (g_hash_table_contains(node->object_map, obj)) {
            guint32 br[3];
            const guint64 handle64 = GPOINTER_TO_SIZE
                (g_hash_table_lookup(node->object_map, obj));

            br[0] = BR_DEAD_BINDER_64;
            memcpy(br + 1, &handle64, sizeof(handle64));
            test_binder_node_queue_cmd_data_locked(node, ANY_THREAD, br);
        }
        test_binder_node_unlock(node);
        /* Unlock */

        test_binder_node_unref(node);
    }
}

void
test_binder_br_dead_reply(
    int fd,
    TEST_BR_THREAD dest)
{
    guint32 cmd = BR_DEAD_REPLY;

    test_binder_push_data(fd, dest, &cmd);
}

void
test_binder_br_failed_reply(
    int fd,
    TEST_BR_THREAD dest)
{
    guint32 cmd = BR_FAILED_REPLY;

    test_binder_push_data(fd, dest, &cmd);
}

static
void
test_binder_fill_transaction_data(
    BinderTransactionData64* tr,
    const BinderTarget64* target,
    guint32 code,
    const GByteArray* bytes)
{
    memset(tr, 0, sizeof(*tr));
    tr->target = *target;
    tr->code = code;
    tr->data_size = bytes ? bytes->len : 0;
    tr->sender_pid = getpid();
    tr->sender_euid = geteuid();

    /* This memory should eventually get deallocated with BC_FREE_BUFFER_64 */
    tr->data_buffer = (gsize)gutil_memdup(bytes ? (void*) bytes->data :
        (void*) "", tr->data_size);
}

void
test_binder_br_transaction(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr,
    guint32 code,
    const GByteArray* bytes)
{
    guint32 cmd = BR_TRANSACTION_64;
    guint8 br[sizeof(guint32) + sizeof(BinderTransactionData64)];
    BinderTarget64 target;

    memset(&target, 0, sizeof(target));
    target.ptr = GPOINTER_TO_SIZE(ptr);

    memset(br, 0, sizeof(br));
    memcpy(br, &cmd, sizeof(cmd));
    test_binder_fill_transaction_data((void*)(br + sizeof(cmd)), &target,
        code, bytes);

    test_binder_push_data(fd, dest, br);
}

void
test_binder_br_reply(
    int fd,
    TEST_BR_THREAD dest,
    guint32 handle,
    guint32 code,
    const GByteArray* bytes)
{
    guint32 cmd = BR_REPLY_64;
    guint8 br[sizeof(guint32) + sizeof(BinderTransactionData64)];
    BinderTarget64 target;

    memset(&target, 0, sizeof(target));
    target.handle = handle;

    memset(br, 0, sizeof(br));
    memcpy(br, &cmd, sizeof(cmd));
    test_binder_fill_transaction_data((void*)(br + sizeof(cmd)), &target,
        code, bytes);

    test_binder_push_data(fd, dest, br);
}

void
test_binder_br_reply_status(
    int fd,
    TEST_BR_THREAD dest,
    gint32 status)
{
    guint32 cmd = BR_REPLY_64;
    guint8 br[sizeof(guint32) + sizeof(BinderTransactionData64)];
    BinderTransactionData64* tr = (void*)(br + sizeof(cmd));

    memset(br, 0, sizeof(br));
    memcpy(br, &cmd, sizeof(cmd));
    tr->flags = TF_STATUS_CODE;
    tr->data_size = sizeof(status);
    /* This memory should eventually get deallocated with BC_FREE_BUFFER_64 */
    tr->data_buffer = (gsize) gutil_memdup(&status, sizeof(status));

    test_binder_push_data(fd, dest, br);
}

int
test_binder_handle(
    int fd,
    GBinderLocalObject* obj)
{
    int h = -1;

    if (obj) {
        TestBinderNode* node = test_binder_node_ref_from_fd(fd);

        g_assert(node);

        /* Lock */
        test_binder_node_lock(node);
        if (g_hash_table_contains(node->object_map, obj)) {
            h = GPOINTER_TO_INT(g_hash_table_lookup(node->object_map, obj));
        }
        test_binder_node_unlock(node);
        /* Unlock */

        test_binder_node_unref(node);
    }
    return h;
}

GBinderLocalObject*
test_binder_object(
    int fd,
    guint handle)
{
    GBinderLocalObject* obj = NULL;
    TestBinderNode* node = test_binder_node_ref_from_fd(fd);

    if (node) {
        gpointer key = GSIZE_TO_POINTER(handle);

        /* Lock */
        test_binder_node_lock(node);
        if (g_hash_table_contains(node->handle_map, key)) {
            obj = gbinder_local_object_ref
                (g_hash_table_lookup(node->handle_map, key));
        }
        test_binder_node_unlock(node);
        /* Unlock */

        test_binder_node_unref(node);
    }
    return obj;
}

guint
test_binder_register_object(
    int fd,
    GBinderLocalObject* obj,
    guint h)
{
    TestBinderNode* node = test_binder_node_ref_from_fd(fd);

    g_assert(node);

    /* Lock */
    test_binder_node_lock(node);
    h = test_binder_register_object_locked(node, obj, h);
    test_binder_node_unlock(node);
    /* Unlock */

    test_binder_node_unref(node);
    return h;
}

void
test_binder_ignore_dead_object(
    int fd)
{
    TestBinderNode* node = test_binder_node_ref_from_fd(fd);

    g_assert(node);

    /* Lock */
    test_binder_node_lock(node);
    node->ignore_dead_object++;
    test_binder_node_unlock(node);
    /* Unlock */

    test_binder_node_unref(node);
}

static
void
test_binder_node_unregister_objects(
    TestBinderNode* node,
    GUtilIntArray* handles)
{
    GHashTableIter it;
    gpointer handle, obj;

    /* Lock */
    test_binder_node_lock(node);
    g_hash_table_iter_init(&it, node->handle_map);
    while (g_hash_table_iter_next(&it, &handle, &obj)) {
        gutil_int_array_append(handles, GPOINTER_TO_INT(handle));
        g_hash_table_remove(node->object_map, obj);
        g_hash_table_iter_remove(&it);
    }
    test_binder_node_unlock(node);
    /* Unlock */
}

void
test_binder_unregister_objects(
    int fd)
{
    TestBinderNode* node = test_binder_node_ref_from_fd(fd);
    GUtilIntArray* handles = gutil_int_array_new();
    guint i;

    g_assert(node);
    test_binder_node_unregister_objects(node, handles);
    for (i = 0; i < handles->count; i++) {
        test_binder_node_br_dead_object_64(node, handles->data[i]);
    }
    test_binder_node_unref(node);
    gutil_int_array_free(handles, TRUE);
}

void
test_binder_exit_wait(
    const TestOpt* opt,
    GMainLoop* loop)
{
    gbinder_ipc_exit();

    G_LOCK(test_binder);
    if (test_path_map) {
        g_assert(!test_binder_exit_loop);
        if (loop) {
            g_main_loop_ref(loop);
        } else {
            loop = g_main_loop_new(NULL, FALSE);
        }
        test_binder_exit_loop = loop;
        GDEBUG("Waiting for all nodes to get destroyed...");
        G_UNLOCK(test_binder);

        test_run(opt, loop);

        G_LOCK(test_binder);
        g_assert(!test_path_map);
        test_binder_exit_loop = NULL;
        g_main_loop_unref(loop);
    }
    G_UNLOCK(test_binder);

    test_binder_destroy_data_flush();
}

int
gbinder_system_open(
    const char* path,
    int flags)
{
    static const char path_prefix[] = "/dev/";

    if (path && g_str_has_prefix(path, path_prefix)) {
        TestBinderNode* node;
        TestBinderFd* binder_fd;
        int fd;

        G_LOCK(test_binder);
        node = test_path_map ? g_hash_table_lookup(test_path_map, path) : NULL;
        if (node) {
            test_binder_node_ref(node);
        } else {
            node = test_binder_node_new(path, &test_binder_io_64);
            if (!test_path_map) {
                test_path_map = g_hash_table_new(g_str_hash, g_str_equal);
            }
            g_hash_table_replace(test_path_map, node->path, node);
        }
        binder_fd = test_binder_fd_new(node);
        test_binder_node_unref(node);  /* Drop temporary ref */
        if (!test_fd_map) {
            test_fd_map = g_hash_table_new(g_direct_hash, g_direct_equal);
        }
        fd = binder_fd->fd;
        g_hash_table_replace(test_fd_map, GINT_TO_POINTER(fd), binder_fd);
        G_UNLOCK(test_binder);

        return fd;
    } else {
        errno = ENOENT;
        return -1;
    }
}

int
gbinder_system_close(
    int fd)
{
    int ret;
    TestBinderFd* binder_fd;

    G_LOCK(test_binder);
    binder_fd = g_hash_table_lookup(test_fd_map, GINT_TO_POINTER(fd));
    if (binder_fd) {
        GDEBUG("Closed fd %d %s", fd, binder_fd->node->path);
        g_assert(g_hash_table_remove(test_fd_map, GINT_TO_POINTER(fd)));
        if (!g_hash_table_size(test_fd_map)) {
            g_hash_table_unref(test_fd_map);
            test_fd_map = NULL;
        }
        ret = 0;
    } else {
        errno = EBADF;
        ret = -1;
    }
    G_UNLOCK(test_binder);

    if (binder_fd) {
        test_binder_fd_free(binder_fd);
    }
    return ret;
}

int
gbinder_system_ioctl(
    int fd,
    int request,
    void* data)
{
    TestBinderNode* node = test_binder_node_ref_from_fd(fd);

    if (node) {
        int ret;
        const TestBinderIo* io = node->io;

        errno = 0;
        switch (request) {
        case BINDER_VERSION:
            ret = test_binder_ioctl_version(node, data);
            break;
        case BINDER_SET_MAX_THREADS:
            ret = 0;
            break;
        default:
            if (request == io->write_read_request) {
                ret = io->handle_write_read(node, data);
            } else {
                errno = EINVAL;
                ret = -1;
            }
            break;
        }
        test_binder_node_unref(node);
        return ret;
    } else {
        errno = EBADF;
        return -1;
    }
}

void*
gbinder_system_mmap(
   size_t length,
   int prot,
   int flags,
   int fd)
{
    return g_malloc(length);
}

int
gbinder_system_munmap(
    void* addr,
    size_t length)
{
    g_free(addr);
    return 0;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
