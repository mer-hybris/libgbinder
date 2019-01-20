/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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

#include "gbinder_system.h"

#define GLOG_MODULE_NAME test_binder_log
#include <gutil_log.h>
GLOG_MODULE_DEFINE2("test_binder", gutil_log_default);

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

static GHashTable* test_fd_map = NULL;
static GHashTable* test_node_map = NULL;
static GPrivate test_looper;

#define public_fd  fd[0]
#define private_fd fd[1]

#define BINDER_VERSION _IOWR('b', 9, gint32)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, guint32)

#define TF_ONE_WAY     0x01
#define TF_ROOT_OBJECT 0x04
#define TF_STATUS_CODE 0x08
#define TF_ACCEPT_FDS  0x10

typedef struct test_binder_io TestBinderIo;
typedef struct test_binder TestBinder;

typedef
void
(*TestBinderPushDataFunc)(
    int fd,
    const void* data);

typedef struct test_binder_submit_thread {
    GThread* thread;
    GCond cond;
    GMutex mutex;
    gboolean run;
    GSList* queue;
    TestBinder* binder;
} TestBinderSubmitThread;

typedef struct test_binder_node {
    char* path;
    int refcount;
    const TestBinderIo* io;
    GHashTable* destroy_map;
} TestBinderNode;

typedef struct test_binder {
    TestBinderNode* node;
    TestBinderSubmitThread* submit_thread;
    gboolean looper_enabled;
    int fd[2];
} TestBinder;

struct test_binder_io {
    int version;
    int write_read_request;
    int (*handle_write_read)(TestBinder* binder, void* data);
};

typedef struct binder_write_read_64 {
    guint64 write_size;
    guint64 write_consumed;
    guint64 write_buffer;
    guint64 read_size;
    guint64 read_consumed;
    guint64 read_buffer;
} BinderWriteRead64;

typedef struct binder_transaction_data_64 {
    guint64 handle;
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

typedef struct binder_pre_cookie_64 {
    guint64 ptr;
    guint64 cookie;
} BinderPtrCookie64;

typedef struct binder_handle_cookie_64 {
  guint32 handle;
  guint64 cookie;
} __attribute__((packed)) BinderHandleCookie64;

#define BC_TRANSACTION_64       _IOW('c', 0, BinderTransactionData64)
#define BC_REPLY_64             _IOW('c', 1, BinderTransactionData64)
#define BC_FREE_BUFFER_64       _IOW('c', 3, guint64)
#define BC_INCREFS              _IOW('c', 4, guint32)
#define BC_ACQUIRE              _IOW('c', 5, guint32)
#define BC_RELEASE              _IOW('c', 6, guint32)
#define BC_DECREFS              _IOW('c', 7, guint32)
#define BC_ENTER_LOOPER          _IO('c', 12)
#define BC_EXIT_LOOPER           _IO('c', 13)
#define BC_REQUEST_DEATH_NOTIFICATION_64 _IOW('c', 14, BinderHandleCookie64)
#define BC_CLEAR_DEATH_NOTIFICATION_64   _IOW('c', 15, BinderHandleCookie64)

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
#define BR_FAILED_REPLY          _IO('r', 17)

static
gpointer
test_binder_submit_thread_proc(
    gpointer data)
{
    TestBinderSubmitThread* submit = data;
    TestBinder* binder = submit->binder;
    GMutex* mutex = &submit->mutex;
    GCond* cond = &submit->cond;

    GDEBUG("Submit thread started");
    g_mutex_lock(mutex);
    while (submit->run) {
        GBytes* next = NULL;

        while (submit->run && !next) {
            if (submit->queue) {
                next = submit->queue->data;
                submit->queue = g_slist_remove(submit->queue, next);
                break;
            } else {
                g_cond_wait(cond, mutex);
            }
        }

        if (next) {
            int bytes_available = 0;
            int err = ioctl(binder->public_fd, FIONREAD, &bytes_available);

            /* Wait until the queue is empty */
            g_assert(err >= 0);
            while (bytes_available > 0 && submit->run) {
                /* Wait a bit between polls */
                g_cond_wait_until(cond, mutex, g_get_monotonic_time () +
                    100 * G_TIME_SPAN_MILLISECOND);
                err = ioctl(binder->public_fd, FIONREAD, &bytes_available);
                g_assert(err >= 0);
            }

            if (submit->run) {
                gsize len;
                gconstpointer data = g_bytes_get_data(next, &len);

                GDEBUG("Submitting command 0x%08x", *(guint32*)data);
                g_assert(write(binder->private_fd, data, len) == len);
            }
            g_bytes_unref(next);
        }
    }
    g_mutex_unlock(mutex);
    GDEBUG("Submit thread exiting");
    return NULL;
}

static
TestBinderSubmitThread*
test_binder_submit_thread_new(
    TestBinder* binder)
{
    TestBinderSubmitThread* submit = g_new0(TestBinderSubmitThread, 1);

    submit->run = TRUE;
    submit->binder = binder;
    g_cond_init(&submit->cond);
    g_mutex_init(&submit->mutex);
    submit->thread = g_thread_new(binder->node->path,
        test_binder_submit_thread_proc, submit);
    return submit;
}

static
void
test_binder_submit_thread_free(
    TestBinderSubmitThread* submit)
{
    if (submit) {
        g_mutex_lock(&submit->mutex);
        submit->run = FALSE;
        g_cond_signal(&submit->cond);
        g_mutex_unlock(&submit->mutex);
        g_thread_join(submit->thread);

        g_slist_free_full(submit->queue, (GDestroyNotify) g_bytes_unref);
        g_cond_clear(&submit->cond);
        g_mutex_clear(&submit->mutex);
        g_free(submit);
    }
}

static
void
test_binder_submit_later(
    TestBinderSubmitThread* submit,
    const void* data)
{
    const guint32* cmd = data;

    g_mutex_lock(&submit->mutex);
    submit->queue = g_slist_append(submit->queue,
        g_bytes_new(cmd, sizeof(*cmd) + _IOC_SIZE(*cmd)));
    g_cond_signal(&submit->cond);
    g_mutex_unlock(&submit->mutex);
}

static
void
test_io_free_buffer(
    TestBinder* binder,
    void* ptr)
{
    if (ptr) {
        TestBinderNode* node = binder->node;
        GDestroyNotify destroy = g_hash_table_lookup(node->destroy_map, ptr);

        if (destroy) {
            g_hash_table_remove(node->destroy_map, ptr);
            destroy(ptr);
        } else {
            g_free(ptr);
        }
    }
}

static
int
test_io_handle_write_read_64(
    TestBinder* binder,
    void* data)
{
    BinderWriteRead64* wr = data;
    gssize bytes_left = wr->write_size - wr->write_consumed;
    const guint8* write_ptr = (void*)(gsize)
        (wr->write_buffer + wr->write_consumed);
    gboolean is_looper;

    while (bytes_left >= sizeof(guint32)) {
        const guint cmd = *(guint32*)write_ptr;
        const guint cmdsize = _IOC_SIZE(cmd);

        GASSERT(bytes_left >= (sizeof(guint32) + cmdsize));
        if (bytes_left >= (sizeof(guint32) + cmdsize)) {
            wr->write_consumed += sizeof(guint32);
            write_ptr += sizeof(guint32);
            bytes_left -= sizeof(guint32);

            switch (cmd) {
            case BC_TRANSACTION_64:
            case BC_REPLY_64:
                /* Is there anything special about transactions and replies? */
                break;
            case BC_FREE_BUFFER_64:
                test_io_free_buffer(binder,
                    GSIZE_TO_POINTER(*(guint64*)write_ptr));
                break;
            case BC_ENTER_LOOPER:
                 g_private_set(&test_looper, GINT_TO_POINTER(TRUE));
                 break;
            case BC_EXIT_LOOPER:
                 g_private_set(&test_looper, NULL);
                 break;
            case BC_REQUEST_DEATH_NOTIFICATION_64:
            case BC_CLEAR_DEATH_NOTIFICATION_64:
            case BC_INCREFS:
            case BC_ACQUIRE:
            case BC_RELEASE:
            case BC_DECREFS:
                break;
            default:
#pragma message("TODO: implement more BINDER_WRITE_READ commands")
                GDEBUG("Unhandled command 0x%08x", cmd);
                break;
            }
            wr->write_consumed += cmdsize;
            write_ptr += cmdsize;
            bytes_left -= cmdsize;
        } else {
            /* Partial command in the buffer */
            errno = EINVAL;
            return -1;
        }
    }

    is_looper = g_private_get(&test_looper) ? TRUE : FALSE;
    if (binder->looper_enabled || !is_looper) {
        /* Now read the data from the socket */
        int bytes_available = 0;
        int err = ioctl(binder->public_fd, FIONREAD, &bytes_available);

        if (err >= 0) {
            int bytes_read = 0;

            if (bytes_available >= 4) {
                bytes_read = read(binder->public_fd,
                    (void*)(gsize)(wr->read_buffer + wr->read_consumed),
                    wr->read_size - wr->read_consumed);
            } else {
                struct timespec wait;

                wait.tv_sec = 0;
                wait.tv_nsec = 10 * 1000000; /* 10 ms */
                nanosleep(&wait, &wait);
            }

            if (bytes_read >= 0) {
                wr->read_consumed += bytes_read;
                return 0;
            } else {
                err = bytes_read;
            }
        }
        return err;
    } else {
        if (wr->read_size > 0) {
            struct timespec wait;

            wait.tv_sec = 0;
            wait.tv_nsec = 100 * 1000000; /* 100 ms */
            nanosleep(&wait, &wait);
        }
        return 0;
    }
}

static const TestBinderIo test_io_64 = {
    .version = 8,
    .write_read_request = _IOWR('b', 1, BinderWriteRead64),
    .handle_write_read = test_io_handle_write_read_64
};

static
int
test_binder_ioctl_version(
    TestBinder* binder,
    int* version)
{
    *version = binder->node->io->version;
    return 0;
}

static
void
test_binder_node_unref(
    TestBinderNode* node)
{
    node->refcount--;
    if (!node->refcount) {
        g_hash_table_remove(test_node_map, node->path);
        g_hash_table_destroy(node->destroy_map);
        g_free(node->path);
        g_free(node);
    }
    if (!g_hash_table_size(test_node_map)) {
        g_hash_table_unref(test_node_map);
        test_node_map = NULL;
    }
}

static
TestBinder*
test_binder_from_fd(
    int fd)
{
    TestBinder* binder = NULL;
    GASSERT(test_fd_map);
    if (test_fd_map) {
        binder = g_hash_table_lookup(test_fd_map, GINT_TO_POINTER(fd));
        GASSERT(binder);
    }
    return binder;
}

static
void
test_io_destroy_none(
    gpointer data)
{
    GDEBUG("Not freeing %p", data);
}

void
test_binder_set_looper_enabled(
    int fd,
    gboolean enabled)
{
    TestBinder* binder = test_binder_from_fd(fd);

    g_assert(binder);
    binder->looper_enabled = enabled;
}

void
test_binder_set_destroy(
    int fd,
    gpointer ptr,
    GDestroyNotify destroy)
{
    TestBinder* binder = test_binder_from_fd(fd);

    if (binder) {
        TestBinderNode* node = binder->node;

        g_hash_table_replace(node->destroy_map, ptr,
            destroy ? destroy : test_io_destroy_none);
    }
}

static
void
test_binder_push_data(
    int fd,
    const void* data)
{
    const guint32* cmd = data;
    const int len = sizeof(*cmd) + _IOC_SIZE(*cmd);
    TestBinder* binder = test_binder_from_fd(fd);

    g_assert(binder);
    g_assert(write(binder->private_fd, data, len) == len);
}

static
void
test_binder_push_data_later(
    int fd,
    const void* data)
{
    TestBinder* binder = test_binder_from_fd(fd);

    g_assert(binder);
    if (!binder->submit_thread) {
        binder->submit_thread = test_binder_submit_thread_new(binder);
    }
    test_binder_submit_later(binder->submit_thread, data);
}

void
test_binder_push_ptr_cookie(
    int fd,
    guint32 cmd,
    void* ptr)
{
    guint8 buf[sizeof(guint32) + sizeof(BinderPtrCookie64)];
    BinderPtrCookie64* data = (void*)(buf + sizeof(cmd));

    memcpy(buf, &cmd, sizeof(cmd));
    memset(data, 0, sizeof(*data));
    data->ptr = (gsize)ptr;
    test_binder_push_data(fd, buf);
}

void
test_binder_br_noop(
    int fd)
{
    guint32 cmd = BR_NOOP;

    test_binder_push_data(fd, &cmd);
}

void
test_binder_br_increfs(
    int fd,
    void* ptr)
{
    test_binder_push_ptr_cookie(fd, BR_INCREFS_64, ptr);
}

void
test_binder_br_acquire(
    int fd,
    void* ptr)
{
    test_binder_push_ptr_cookie(fd, BR_ACQUIRE_64, ptr);
}

void
test_binder_br_release(
    int fd,
    void* ptr)
{
    test_binder_push_ptr_cookie(fd, BR_RELEASE_64, ptr);
}

void
test_binder_br_decrefs(
    int fd,
    void* ptr)
{
    test_binder_push_ptr_cookie(fd, BR_DECREFS_64, ptr);
}

void
test_binder_br_transaction_complete(
    int fd)
{
    guint32 cmd = BR_TRANSACTION_COMPLETE;

    test_binder_push_data(fd, &cmd);
}

void
test_binder_br_transaction_complete_later(
    int fd)
{
    guint32 cmd = BR_TRANSACTION_COMPLETE;

    test_binder_push_data_later(fd, &cmd);
}

void
test_binder_br_dead_binder(
    int fd,
    guint handle)
{
    const guint64 handle64 = handle;
    guint32 buf[3];

    buf[0] = BR_DEAD_BINDER_64;
    memcpy(buf + 1, &handle64, sizeof(handle64));

    test_binder_push_data(fd, buf);
}

void
test_binder_br_dead_reply(
    int fd)
{
    guint32 cmd = BR_DEAD_REPLY;

    test_binder_push_data(fd, &cmd);
}

void
test_binder_br_failed_reply(
    int fd)
{
    guint32 cmd = BR_FAILED_REPLY;

    test_binder_push_data(fd, &cmd);
}

static
void
test_binder_fill_transaction_data(
    BinderTransactionData64* tr,
    guint64 handle,
    guint32 code,
    const GByteArray* bytes)
{
    memset(tr, 0, sizeof(*tr));
    tr->handle = handle;
    tr->code = code;
    tr->data_size = bytes ? bytes->len : 0;
    tr->sender_pid = getpid();
    tr->sender_euid = geteuid();
    /* This memory should eventually get deallocated with BC_FREE_BUFFER_64 */
    tr->data_buffer = (gsize)g_memdup(bytes ? (void*)bytes->data : (void*)"",
        tr->data_size);
}

void
test_binder_br_transaction(
    int fd,
    void* target,
    guint32 code,
    const GByteArray* bytes)
{
    guint32 cmd = BR_TRANSACTION_64;
    guint8 buf[sizeof(guint32) + sizeof(BinderTransactionData64)];

    memcpy(buf, &cmd, sizeof(cmd));
    test_binder_fill_transaction_data((void*)(buf + sizeof(cmd)),
        (gsize)target, code, bytes);

    test_binder_push_data(fd, buf);
}

static
void
test_binder_br_reply1(
    int fd,
    guint32 handle,
    guint32 code,
    const GByteArray* bytes,
    TestBinderPushDataFunc push)
{
    guint32 cmd = BR_REPLY_64;
    guint8 buf[sizeof(guint32) + sizeof(BinderTransactionData64)];

    memcpy(buf, &cmd, sizeof(cmd));
    test_binder_fill_transaction_data((void*)(buf + sizeof(cmd)),
        handle, code, bytes);

    push(fd, buf);
}

void
test_binder_br_reply(
    int fd,
    guint32 handle,
    guint32 code,
    const GByteArray* bytes)
{
    test_binder_br_reply1(fd, handle, code, bytes, test_binder_push_data);
}

void
test_binder_br_reply_later(
    int fd,
    guint32 handle,
    guint32 code,
    const GByteArray* bytes)
{
    test_binder_br_reply1(fd, handle, code, bytes, test_binder_push_data_later);
}

void
test_binder_br_reply_status1(
    int fd,
    gint32 status,
    TestBinderPushDataFunc push)
{
    guint8 buf[sizeof(guint32) + sizeof(BinderTransactionData64)];
    guint32* cmd = (void*)buf;
    BinderTransactionData64* tr = (void*)(buf + sizeof(*cmd));

    memset(buf, 0, sizeof(buf));
    *cmd = BR_REPLY_64;
    tr->flags = TF_STATUS_CODE;
    tr->data_size = sizeof(status);
    /* This memory should eventually get deallocated with BC_FREE_BUFFER_64 */
    tr->data_buffer = (gsize)g_memdup(&status, sizeof(status));

    push(fd, buf);
}

void
test_binder_br_reply_status(
    int fd,
    gint32 status)
{
    test_binder_br_reply_status1(fd, status, test_binder_push_data);
}

void
test_binder_br_reply_status_later(
    int fd,
    gint32 status)
{
    test_binder_br_reply_status1(fd, status, test_binder_push_data_later);
}

int
gbinder_system_open(
    const char* path,
    int flags)
{
    if (path && g_str_has_prefix(path, "/dev") &&
        g_str_has_suffix(path, "binder")) {
        TestBinderNode* node = NULL;
        TestBinder* binder = NULL;
        int fd;

        if (test_node_map) {
            node = g_hash_table_lookup(test_node_map, path);
        }
        if (node) {
            node->refcount++;
        } else {
            node = g_new0(TestBinderNode, 1);
            node->path = g_strdup(path);
            node->refcount = 1;
            node->io = &test_io_64;
            node->destroy_map = g_hash_table_new(g_direct_hash, g_direct_equal);
            if (!test_node_map) {
                test_node_map = g_hash_table_new(g_str_hash, g_str_equal);
            }
            g_hash_table_replace(test_node_map, node->path, node);
        }

        binder = g_new0(TestBinder, 1);
        binder->node = node;
        socketpair(AF_UNIX, SOCK_STREAM, 0, binder->fd);
        fd = binder->public_fd;

        if (!test_fd_map) {
            test_fd_map = g_hash_table_new(g_direct_hash, g_direct_equal);
        }
        g_hash_table_replace(test_fd_map, GINT_TO_POINTER(fd), binder);
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
    TestBinder* binder = test_binder_from_fd(fd);

    if (binder) {
        g_hash_table_remove(test_fd_map, GINT_TO_POINTER(fd));
        if (!g_hash_table_size(test_fd_map)) {
            g_hash_table_unref(test_fd_map);
            test_fd_map = NULL;
        }
        test_binder_submit_thread_free(binder->submit_thread);
        test_binder_node_unref(binder->node);
        close(binder->public_fd);
        close(binder->private_fd);
        g_free(binder);
        return 0;
    }
    errno = EBADF;
    return -1;
}

int
gbinder_system_ioctl(
    int fd,
    int request,
    void* data)
{
    TestBinder* binder = test_binder_from_fd(fd);
    if (binder) {
        const TestBinderIo* io = binder->node->io;

        switch (request) {
        case BINDER_VERSION:
            return test_binder_ioctl_version(binder, data);
        case BINDER_SET_MAX_THREADS:
            return 0;
        default:
            if (request == io->write_read_request) {
                return io->handle_write_read(binder, data);
            } else {
                errno = EINVAL;
                return -1;
            }
        }
    }
    errno = EBADF;
    return -1;
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
