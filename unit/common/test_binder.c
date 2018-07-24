/*
 * Copyright (C) 2018 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_system.h"

#include <gutil_log.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

static GHashTable* test_fd_map = NULL;
static GHashTable* test_node_map = NULL;

#define public_fd  fd[0]
#define private_fd fd[1]

#define BINDER_VERSION _IOWR('b', 9, gint32)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, guint32)

#define TF_ONE_WAY     0x01
#define TF_ROOT_OBJECT 0x04
#define TF_STATUS_CODE 0x08
#define TF_ACCEPT_FDS  0x10

typedef struct test_binder_io TestBinderIo;

typedef struct test_binder_node {
    char* path;
    int refcount;
    const TestBinderIo* io;
} TestBinderNode;

typedef struct test_binder {
    TestBinderNode* node;
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

#define BC_TRANSACTION_64       _IOW('c', 0, BinderTransactionData64)
#define BC_REPLY_64             _IOW('c', 1, BinderTransactionData64)
#define BC_FREE_BUFFER_64       _IOW('c', 3, guint64)
#define BC_INCREFS              _IOW('c', 4, guint32)
#define BC_ACQUIRE              _IOW('c', 5, guint32)
#define BC_RELEASE              _IOW('c', 6, guint32)
#define BC_DECREFS              _IOW('c', 7, guint32)
#define BC_ENTER_LOOPER          _IO('c', 12)
#define BC_EXIT_LOOPER           _IO('c', 13)

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
int
test_io_handle_write_read_64(
    TestBinder* binder,
    void* data)
{
    int err, bytes_available = 0;
    BinderWriteRead64* wr = data;
    gssize bytes_left = wr->write_size - wr->write_consumed;
    const guint8* write_ptr = (void*)(gsize)
        (wr->write_buffer + wr->write_consumed);

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
                g_free((void*)(gsize)(*(guint64*)write_ptr));
                break;
            case BC_INCREFS:
            case BC_ACQUIRE:
            case BC_RELEASE:
            case BC_DECREFS:
            case BC_ENTER_LOOPER:
            case BC_EXIT_LOOPER:
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

    /* Now read the data from the socket */
    err = ioctl(binder->public_fd, FIONREAD, &bytes_available);
    if (err >= 0) {
        int bytes_read = 0;
        if (bytes_available > 0) {
            bytes_read = read(binder->public_fd,
                (void*)(gsize)(wr->read_buffer + wr->read_consumed),
                wr->read_size - wr->read_consumed);
        }

        if (bytes_read >= 0) {
            wr->read_consumed += bytes_read;
            return 0;
        } else {
            err = bytes_read;
        }
    }
    return err;
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
        g_free(node->path);
        g_free(node);
    }
    if (!g_hash_table_size(test_node_map)) {
        g_hash_table_unref(test_node_map);
        test_node_map = NULL;
    }
}

static
gboolean
test_binder_push_data(
    int fd,
    const void* data)
{
    GASSERT(test_fd_map);
    if (test_fd_map) {
        gpointer key = GINT_TO_POINTER(fd);
        TestBinder* binder = g_hash_table_lookup(test_fd_map, key);

        GASSERT(binder);
        if (binder) {
            const guint32* cmd = data;
            const int len = sizeof(*cmd) + _IOC_SIZE(*cmd);

            return write(binder->private_fd, data, len) == len;
        }
    }
    return FALSE;
}

static
void
test_binder_fill_transaction_data(
    BinderTransactionData64* tr,
    guint64 handle,
    guint32 code,
    const GByteArray* bytes)
{
    g_assert(bytes);

    memset(tr, 0, sizeof(*tr));
    tr->handle = handle;
    tr->code = code;
    tr->data_size = bytes->len;
    tr->sender_pid = getpid();
    tr->sender_euid = geteuid();
    /* This memory should eventually get deallocated with BC_FREE_BUFFER_64 */
    tr->data_buffer = (gsize)g_memdup(bytes->data, bytes->len);
}

gboolean
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
    return test_binder_push_data(fd, buf);
}

gboolean
test_binder_br_noop(
    int fd)
{
    guint32 cmd = BR_NOOP;

    return test_binder_push_data(fd, &cmd);
}

gboolean
test_binder_br_increfs(
    int fd,
    void* ptr)
{
    return test_binder_push_ptr_cookie(fd, BR_INCREFS_64, ptr);
}

gboolean
test_binder_br_acquire(
    int fd,
    void* ptr)
{
    return test_binder_push_ptr_cookie(fd, BR_ACQUIRE_64, ptr);
}

gboolean
test_binder_br_release(
    int fd,
    void* ptr)
{
    return test_binder_push_ptr_cookie(fd, BR_RELEASE_64, ptr);
}

gboolean
test_binder_br_decrefs(
    int fd,
    void* ptr)
{
    return test_binder_push_ptr_cookie(fd, BR_DECREFS_64, ptr);
}

gboolean
test_binder_br_transaction_complete(
    int fd)
{
    guint32 cmd = BR_TRANSACTION_COMPLETE;

    return test_binder_push_data(fd, &cmd);
}

gboolean
test_binder_br_dead_binder(
    int fd,
    guint handle)
{
    const guint64 handle64 = handle;
    guint32 buf[3];

    buf[0] = BR_DEAD_BINDER_64;
    memcpy(buf + 1, &handle64, sizeof(handle64));

    return test_binder_push_data(fd, buf);
}

gboolean
test_binder_br_dead_reply(
    int fd)
{
    guint32 cmd = BR_DEAD_REPLY;

    return test_binder_push_data(fd, &cmd);
}

gboolean
test_binder_br_failed_reply(
    int fd)
{
    guint32 cmd = BR_FAILED_REPLY;

    return test_binder_push_data(fd, &cmd);
}

gboolean
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

    return test_binder_push_data(fd, buf);
}

gboolean
test_binder_br_reply(
    int fd,
    guint32 handle,
    guint32 code,
    const GByteArray* bytes)
{
    guint32 cmd = BR_REPLY_64;
    guint8 buf[sizeof(guint32) + sizeof(BinderTransactionData64)];

    memcpy(buf, &cmd, sizeof(cmd));
    test_binder_fill_transaction_data((void*)(buf + sizeof(cmd)),
        handle, code, bytes);

    return test_binder_push_data(fd, buf);
}

gboolean
test_binder_br_reply_status(
    int fd,
    gint32 status)
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
    return test_binder_push_data(fd, buf);
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
    GASSERT(test_fd_map);
    if (test_fd_map) {
        gpointer key = GINT_TO_POINTER(fd);
        TestBinder* binder = g_hash_table_lookup(test_fd_map, key);

        GASSERT(binder);
        if (binder) {
            g_hash_table_remove(test_fd_map, key);
            if (!g_hash_table_size(test_fd_map)) {
                g_hash_table_unref(test_fd_map);
                test_fd_map = NULL;
            }
            test_binder_node_unref(binder->node);
            close(binder->public_fd);
            close(binder->private_fd);
            g_free(binder);
            return 0;
        }
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
    GASSERT(test_fd_map);
    if (test_fd_map) {
        gpointer key = GINT_TO_POINTER(fd);
        TestBinder* binder = g_hash_table_lookup(test_fd_map, key);

        GASSERT(binder);
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
