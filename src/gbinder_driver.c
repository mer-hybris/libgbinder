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

#include "gbinder_driver.h"
#include "gbinder_buffer_p.h"
#include "gbinder_cleanup.h"
#include "gbinder_handler.h"
#include "gbinder_io.h"
#include "gbinder_local_object_p.h"
#include "gbinder_local_reply_p.h"
#include "gbinder_local_request_p.h"
#include "gbinder_object_registry.h"
#include "gbinder_output_data.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_remote_reply_p.h"
#include "gbinder_remote_request_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_system.h"
#include "gbinder_writer.h"
#include "gbinder_log.h"

#include <gutil_intarray.h>
#include <gutil_macros.h>
#include <gutil_misc.h>

#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/ioctl.h>

/* BINDER_VM_SIZE copied from native/libs/binder/ProcessState.cpp */
#define BINDER_VM_SIZE ((1024*1024) - sysconf(_SC_PAGE_SIZE)*2)

#define BINDER_MAX_REPLY_SIZE (256)

/* ioctl code (the only one we really need here) */
#define BINDER_VERSION _IOWR('b', 9, gint32)

/* OK, one more */
#define BINDER_SET_MAX_THREADS _IOW('b', 5, guint32)

#define DEFAULT_MAX_BINDER_THREADS (0)

struct gbinder_driver {
    gint refcount;
    int fd;
    void* vm;
    gsize vmsize;
    char* dev;
    const GBinderIo* io;
    const GBinderRpcProtocol* protocol;
};

typedef struct gbinder_driver_read_buf {
    GBinderIoBuf io;
    gsize offset;
} GBinderDriverReadBuf;

typedef struct gbinder_driver_read_data {
    GBinderDriverReadBuf buf;
    guint8 data[GBINDER_IO_READ_BUFFER_SIZE];
} GBinderDriverReadData;

typedef struct gbinder_driver_context {
    GBinderDriverReadBuf* rbuf;
    GBinderObjectRegistry* reg;
    GBinderHandler* handler;
    GBinderCleanup* unrefs;
    GBinderBufferContentsList* bufs;
} GBinderDriverContext;

static
int
gbinder_driver_txstatus(
    GBinderDriver* self,
    GBinderDriverContext* context,
    GBinderRemoteReply* reply);

/*==========================================================================*
 * Implementation
 *==========================================================================*/

#if GUTIL_LOG_VERBOSE
static
void
gbinder_driver_verbose_dump(
    const char mark,
    uintptr_t ptr,
    gsize len)
{
    /* The caller should make sure that verbose log is enabled */
    if (len > 0 && GLOG_ENABLED(GLOG_LEVEL_VERBOSE)) {
        char prefix[3];
        char line[GUTIL_HEXDUMP_BUFSIZE];

        prefix[0] = mark;
        prefix[1] = ' ';
        prefix[2] = 0;
        while (len > 0) {
            const guint dumped = gutil_hexdump(line, (void*)ptr, len);

            GVERBOSE("%s%s", prefix, line);
            len -= dumped;
            ptr += dumped;
            prefix[0] = ' ';
        }
    }
}

GBINDER_INLINE_FUNC
void
gbinder_driver_verbose_dump_bytes(
    const char mark,
    const GByteArray* bytes)
{
    gbinder_driver_verbose_dump(mark, (uintptr_t)bytes->data, bytes->len);
}

static
void
gbinder_driver_verbose_transaction_data(
    const char* name,
    const GBinderIoTxData* tx)
{
    if (GLOG_ENABLED(GLOG_LEVEL_VERBOSE)) {
        if (tx->objects) {
            guint n = 0;
            while (tx->objects[n]) n++;
            if (tx->status) {
                if (tx->target) {
                    GVERBOSE("> %s %p %d (%u bytes, %u objects)", name,
                             tx->target, tx->status, (guint)tx->size, n);
                } else {
                    GVERBOSE("> %s %d (%u bytes, %u objects)", name,
                        tx->status, (guint)tx->size, n);
                }
            } else {
                if (tx->target) {
                    GVERBOSE("> %s %p (%u bytes, %u objects)", name,
                        tx->target, (guint)tx->size, n);
                } else {
                    GVERBOSE("> %s (%u bytes, %u objects)", name, (guint)
                        tx->size, n);
                }
            }
        } else {
            if (tx->status) {
                if (tx->target) {
                    GVERBOSE("> %s %p %d (%u bytes)", name, tx->target,
                        tx->status, (guint)tx->size);
                } else {
                    GVERBOSE("> %s %d (%u bytes)", name, tx->status, (guint)
                        tx->size);
                }
            } else {
                if (tx->target) {
                    GVERBOSE("> %s %p (%u bytes)", name, tx->target, (guint)
                        tx->size);
                } else {
                    GVERBOSE("> %s (%u bytes)", name, (guint)tx->size);
                }
            }
        }
    }
}

#else
#  define gbinder_driver_verbose_dump(x,y,z) GLOG_NOTHING
#  define gbinder_driver_verbose_dump_bytes(x,y) GLOG_NOTHING
#  define gbinder_driver_verbose_transaction_data(x,y) GLOG_NOTHING
#endif /* GUTIL_LOG_VERBOSE */

static
int
gbinder_driver_write(
    GBinderDriver* self,
    GBinderIoBuf* buf)
{
    int err = (-EAGAIN);

    while (err == (-EAGAIN)) {
        gbinder_driver_verbose_dump('<',
            buf->ptr +  buf->consumed,
            buf->size - buf->consumed);
        GVERBOSE("gbinder_driver_write(%d) %u/%u", self->fd,
            (guint)buf->consumed, (guint)buf->size);
        err = self->io->write_read(self->fd, buf, NULL);
        GVERBOSE("gbinder_driver_write(%d) %u/%u err %d", self->fd,
            (guint)buf->consumed, (guint)buf->size, err);
    }
    return err;
}

static
int
gbinder_driver_write_read(
    GBinderDriver* self,
    GBinderIoBuf* write,
    GBinderDriverReadBuf* rbuf)
{
    int err = (-EAGAIN);
    GBinderIoBuf rio;
    GBinderIoBuf* read;

    /* rbuf is never NULL */
    if (rbuf->offset) {
        rio.ptr = rbuf->io.ptr + rbuf->offset;
        rio.size = rbuf->io.size - rbuf->offset;
        rio.consumed = rbuf->io.consumed - rbuf->offset;
        read = &rio;
    } else {
        read = &rbuf->io;
    }

    while (err == (-EAGAIN)) {

#if GUTIL_LOG_VERBOSE
        const gsize were_consumed = read ? read->consumed : 0;
        if (GLOG_ENABLED(GLOG_LEVEL_VERBOSE)) {
            if (write) {
                gbinder_driver_verbose_dump('<',
                    write->ptr +  write->consumed,
                    write->size - write->consumed);
            }
            GVERBOSE("gbinder_driver_write_read(%d) "
                "write %u/%u read %u/%u", self->fd,
                (guint)(write ? write->consumed : 0),
                (guint)(write ? write->size : 0),
                (guint)(read ? read->consumed : 0),
                (guint)(read ? read->size : 0));
        }
#endif /* GUTIL_LOG_VERBOSE */
        err = self->io->write_read(self->fd, write, read);
#if GUTIL_LOG_VERBOSE
        if (GLOG_ENABLED(GLOG_LEVEL_VERBOSE)) {
            GVERBOSE("gbinder_driver_write_read(%d) "
                "write %u/%u read %u/%u err %d", self->fd,
                (guint)(write ? write->consumed : 0),
                (guint)(write ? write->size : 0),
                (guint)(read ? read->consumed : 0),
                (guint)(read ? read->size : 0), err);
            if (read) {
                gbinder_driver_verbose_dump('>',
                    read->ptr + were_consumed,
                    read->consumed - were_consumed);
            }
        }
#endif /* GUTIL_LOG_VERBOSE */
    }

    if (rbuf->offset) {
        rbuf->io.consumed = rio.consumed + rbuf->offset;
    }
    return err;
}

static
gboolean
gbinder_driver_cmd(
    GBinderDriver* self,
    guint32 cmd)
{
    GBinderIoBuf write;

    memset(&write, 0, sizeof(write));
    write.ptr = (uintptr_t)&cmd;
    write.size = sizeof(cmd);
    return gbinder_driver_write(self, &write) >= 0;
}

static
gboolean
gbinder_driver_cmd_int32(
    GBinderDriver* self,
    guint32 cmd,
    guint32 param)
{
    GBinderIoBuf write;
    guint32 data[2];

    data[0] = cmd;
    data[1] = param;
    memset(&write, 0, sizeof(write));
    write.ptr = (uintptr_t)data;
    write.size = sizeof(data);
    return gbinder_driver_write(self, &write) >= 0;
}

static
gboolean
gbinder_driver_cmd_data(
    GBinderDriver* self,
    guint32 cmd,
    const void* payload,
    void* buf)
{
    GBinderIoBuf write;
    guint32* data = buf;

    data[0] = cmd;
    memcpy(data + 1, payload, _IOC_SIZE(cmd));
    memset(&write, 0, sizeof(write));
    write.ptr = (uintptr_t)buf;
    write.size = 4 + _IOC_SIZE(cmd);

    return gbinder_driver_write(self, &write) >= 0;
}

static
gboolean
gbinder_driver_handle_cookie(
    GBinderDriver* self,
    guint32 cmd,
    GBinderRemoteObject* obj)
{
    GBinderIoBuf write;
    guint8 buf[4 + GBINDER_MAX_HANDLE_COOKIE_SIZE];
    guint32* data = (guint32*)buf;

    data[0] = cmd;
    memset(&write, 0, sizeof(write));
    write.ptr = (uintptr_t)buf;
    write.size = 4 + self->io->encode_handle_cookie(data + 1, obj);
    return gbinder_driver_write(self, &write) >= 0;
}

static
void
gbinder_driver_read_init(
    GBinderDriverReadData* read)
{
    /*
     * It shouldn't be necessary to zero-initialize the whole buffer
     * but valgrind complains about access to uninitialised data if
     * we don't do so. Oh well...
     */
    memset(read, 0, sizeof(*read));
    read->buf.io.ptr = GPOINTER_TO_SIZE(read->data);
    read->buf.io.size = sizeof(read->data);
}

static
void
gbinder_driver_context_init(
    GBinderDriverContext* context,
    GBinderDriverReadBuf* rbuf,
    GBinderObjectRegistry* reg,
    GBinderHandler* handler)
{
    context->rbuf = rbuf;
    context->reg = reg;
    context->handler = handler;
    context->unrefs = NULL;
    context->bufs = NULL;
}

static
void
gbinder_driver_context_cleanup(
    GBinderDriverContext* context)
{
    gbinder_cleanup_free(context->unrefs);
    gbinder_buffer_contents_list_free(context->bufs);
}

static
guint32
gbinder_driver_next_command(
    GBinderDriver* self,
    const GBinderDriverReadBuf* rbuf)
{
    if (rbuf->io.consumed > rbuf->offset) {
        const gsize remaining = rbuf->io.consumed - rbuf->offset;

        if (remaining >= 4) {
            /* The size of the data to follow is encoded in the command code */
            const guint32 cmd = *(guint32*)(rbuf->io.ptr + rbuf->offset);
            const guint datalen = _IOC_SIZE(cmd);

            if (remaining >= 4 + datalen) {
                return cmd;
            }
        }
    }
    return 0;
}

static
gboolean
gbinder_driver_reply_status(
    GBinderDriver* self,
    gint32 status)
{
    const GBinderIo* io = self->io;
    GBinderIoBuf write;
    guint8 buf[sizeof(guint32) + GBINDER_MAX_BC_TRANSACTION_SIZE];
    guint8* ptr = buf;
    const guint32* code = &io->bc.reply;

    /* Command (this has to be slightly convoluted to avoid breaking
     * strict-aliasing rules.. oh well) */
    memcpy(buf, code, sizeof(*code));
    ptr += sizeof(*code);

    /* Data */
    ptr += io->encode_status_reply(ptr, &status);

    GVERBOSE("< BC_REPLY (%d)", status);
    memset(&write, 0, sizeof(write));
    write.ptr = (uintptr_t)buf;
    write.size = ptr - buf;
    return gbinder_driver_write(self, &write) >= 0;
}

static
gboolean
gbinder_driver_reply_data(
    GBinderDriver* self,
    GBinderOutputData* data)
{
    GBinderIoBuf write;
    const GBinderIo* io = self->io;
    const gsize extra_buffers = gbinder_output_data_buffers_size(data);
    guint8 buf[GBINDER_MAX_BC_TRANSACTION_SG_SIZE + sizeof(guint32)];
    guint32* cmd = (guint32*)buf;
    guint len = sizeof(*cmd);
    int status;
    GUtilIntArray* offsets = gbinder_output_data_offsets(data);
    void* offsets_buf = NULL;

    /* Build BC_REPLY */
    if (extra_buffers) {
        GVERBOSE("< BC_REPLY_SG %u bytes", (guint)extra_buffers);
        gbinder_driver_verbose_dump_bytes(' ', data->bytes);
        *cmd = io->bc.reply_sg;
        len += io->encode_reply_sg(buf + len, 0, 0, data->bytes,
            offsets, &offsets_buf, extra_buffers);
    } else {
        GVERBOSE("< BC_REPLY");
        gbinder_driver_verbose_dump_bytes(' ', data->bytes);
        *cmd = io->bc.reply;
        len += io->encode_reply(buf + len, 0, 0, data->bytes,
            offsets, &offsets_buf);
    }

#if 0 /* GUTIL_LOG_VERBOSE */
    if (offsets && offsets->count) {
        gbinder_driver_verbose_dump('<', (uintptr_t)offsets_buf,
            offsets->count * io->pointer_size);
    }
#endif /* GUTIL_LOG_VERBOSE */

    /* Write it */
    write.ptr = (uintptr_t)buf;
    write.size = len;
    write.consumed = 0;
    status = gbinder_driver_write(self, &write) >= 0;

    g_free(offsets_buf);
    return status >= 0;
}

static
void
gbinder_driver_handle_transaction(
    GBinderDriver* self,
    GBinderDriverContext* context,
    const void* data)
{
    GBinderLocalReply* reply = NULL;
    GBinderObjectRegistry* reg = context->reg;
    GBinderRemoteRequest* req;
    GBinderIoTxData tx;
    GBinderLocalObject* obj;
    const char* iface;
    int txstatus = -EBADMSG;

    self->io->decode_transaction_data(data, &tx);
    gbinder_driver_verbose_transaction_data("BR_TRANSACTION", &tx);
    req = gbinder_remote_request_new(reg, self->protocol, tx.pid, tx.euid);
    obj = gbinder_object_registry_get_local(reg, tx.target);

    /* Transfer data ownership to the request */
    if (tx.data && tx.size) {
        GBinderBuffer* buf = gbinder_buffer_new(self,
            tx.data, tx.size, tx.objects);

        gbinder_driver_verbose_dump(' ', (uintptr_t)tx.data, tx.size);
        gbinder_remote_request_set_data(req, tx.code, buf);
        context->bufs = gbinder_buffer_contents_list_add(context->bufs,
            gbinder_buffer_contents(buf));
    } else {
        GASSERT(!tx.objects);
        gbinder_driver_free_buffer(self, tx.data);
    }

    /* Process the transaction (NULL is properly handled) */
    iface = gbinder_remote_request_interface(req);
    switch (gbinder_local_object_can_handle_transaction(obj, iface, tx.code)) {
    case GBINDER_LOCAL_TRANSACTION_LOOPER:
        reply = gbinder_local_object_handle_looper_transaction(obj, req,
            tx.code, tx.flags, &txstatus);
        break;
    case GBINDER_LOCAL_TRANSACTION_SUPPORTED:
        /*
         * NULL GBinderHandler means that this is a synchronous call
         * executed on the main thread, meaning that we can call the
         * local object directly.
         */
        reply = context->handler ?
            gbinder_handler_transact(context->handler, obj, req, tx.code,
                tx.flags, &txstatus) :
            gbinder_local_object_handle_transaction(obj, req, tx.code,
                tx.flags, &txstatus);
        break;
    default:
        GWARN("Unhandled transaction %s 0x%08x", iface, tx.code);
        break;
    }

    /* No reply for one-way transactions */
    if (!(tx.flags & GBINDER_TX_FLAG_ONEWAY)) {
        if (reply) {
            context->bufs = gbinder_buffer_contents_list_add(context->bufs,
                gbinder_local_reply_contents(reply));
            gbinder_driver_reply_data(self, gbinder_local_reply_data(reply));
        } else {
            gbinder_driver_reply_status(self, txstatus);
        }

        /* Wait until the reply is handled */
        do {
            txstatus = gbinder_driver_write_read(self, NULL, context->rbuf);
            if (txstatus >= 0) {
                txstatus = gbinder_driver_txstatus(self, context, NULL);
            }
        } while (txstatus == (-EAGAIN));
    }

    /* Free the data allocated for the transaction */
    gbinder_remote_request_unref(req);
    gbinder_local_reply_unref(reply);
    gbinder_local_object_unref(obj);
}

static
void
gbinder_driver_cleanup_decrefs(
    gpointer pointer)
{
    GBinderLocalObject* obj = GBINDER_LOCAL_OBJECT(pointer);

    gbinder_local_object_handle_decrefs(obj);
    gbinder_local_object_unref(obj);
}

static
void
gbinder_driver_cleanup_release(
    gpointer pointer)
{
    GBinderLocalObject* obj = GBINDER_LOCAL_OBJECT(pointer);

    gbinder_local_object_handle_release(obj);
    gbinder_local_object_unref(obj);
}

static
void
gbinder_driver_handle_command(
    GBinderDriver* self,
    GBinderDriverContext* context,
    guint32 cmd,
    const void* data)
{
    const GBinderIo* io = self->io;
    GBinderObjectRegistry* reg = context->reg;

    if (cmd == io->br.noop) {
        GVERBOSE("> BR_NOOP");
    } else if (cmd == io->br.ok) {
        GVERBOSE("> BR_OK");
    } else if (cmd == io->br.transaction_complete) {
        GVERBOSE("> BR_TRANSACTION_COMPLETE (?)");
    } else if (cmd == io->br.spawn_looper) {
        GVERBOSE("> BR_SPAWN_LOOPER");
    } else if (cmd == io->br.finished) {
        GVERBOSE("> BR_FINISHED");
    } else if (cmd == io->br.increfs) {
        guint8 buf[4 + GBINDER_MAX_PTR_COOKIE_SIZE];
        GBinderLocalObject* obj = gbinder_object_registry_get_local
            (reg, io->decode_ptr_cookie(data));

        GVERBOSE("> BR_INCREFS %p", obj);
        gbinder_local_object_handle_increfs(obj);
        gbinder_local_object_unref(obj);
        GVERBOSE("< BC_INCREFS_DONE %p", obj);
        gbinder_driver_cmd_data(self, io->bc.increfs_done, data, buf);
    } else if (cmd == io->br.decrefs) {
        GBinderLocalObject* obj = gbinder_object_registry_get_local
            (reg, io->decode_ptr_cookie(data));

        GVERBOSE("> BR_DECREFS %p", obj);
        if (obj) {
            /*
             * Unrefs must be processed only after clearing the incoming
             * command queue.
             */
            context->unrefs = gbinder_cleanup_add(context->unrefs,
                gbinder_driver_cleanup_decrefs, obj);
        }
    } else if (cmd == io->br.acquire) {
        guint8 buf[4 + GBINDER_MAX_PTR_COOKIE_SIZE];
        GBinderLocalObject* obj = gbinder_object_registry_get_local
            (reg, io->decode_ptr_cookie(data));

        GVERBOSE("> BR_ACQUIRE %p", obj);
        if (obj) {
            /* BC_ACQUIRE_DONE will be sent after the request is handled */
            gbinder_local_object_handle_acquire(obj, context->bufs);
            gbinder_local_object_unref(obj);
        } else {
            /* This shouldn't normally happen. Just send the same data back. */
            GVERBOSE("< BC_ACQUIRE_DONE");
            gbinder_driver_cmd_data(self, io->bc.acquire_done, data, buf);
        }
    } else if (cmd == io->br.release) {
        GBinderLocalObject* obj = gbinder_object_registry_get_local
            (reg, io->decode_ptr_cookie(data));

        GVERBOSE("> BR_RELEASE %p", obj);
        if (obj) {
            /*
             * Unrefs must be processed only after clearing the incoming
             * command queue.
             */
            context->unrefs = gbinder_cleanup_add(context->unrefs,
                gbinder_driver_cleanup_release, obj);
        }
    } else if (cmd == io->br.transaction) {
        gbinder_driver_handle_transaction(self, context, data);
    } else if (cmd == io->br.dead_binder) {
        guint64 handle = 0;
        GBinderRemoteObject* obj;

        io->decode_cookie(data, &handle);
        GVERBOSE("> BR_DEAD_BINDER 0x%08llx", (long long unsigned int) handle);
        obj = gbinder_object_registry_get_remote(reg, (guint32)handle,
            REMOTE_REGISTRY_DONT_CREATE);
        if (obj) {
            /* BC_DEAD_BINDER_DONE will be sent after the request is handled */
            gbinder_remote_object_handle_death_notification(obj);
            gbinder_remote_object_unref(obj);
        } else {
            guint8 buf[4 + GBINDER_MAX_COOKIE_SIZE];

            /* This shouldn't normally happen. Just send the same data back. */
            GVERBOSE("< BC_DEAD_BINDER_DONE 0x%08llx", (long long unsigned int)
                handle);
            gbinder_driver_cmd_data(self, io->bc.dead_binder_done, data, buf);
        }
    } else if (cmd == io->br.clear_death_notification_done) {
#if GUTIL_LOG_VERBOSE
        if (GLOG_ENABLED(GLOG_LEVEL_VERBOSE)) {
            guint64 handle = 0;

            io->decode_cookie(data, &handle);
            GVERBOSE("> BR_CLEAR_DEATH_NOTIFICATION_DONE 0x%08llx",
                (long long unsigned int) handle);
        }
#endif /* GUTIL_LOG_VERBOSE */
    } else {
#pragma message("TODO: handle more commands from the driver")
        GWARN("Unexpected command 0x%08x", cmd);
    }
}

static
void
gbinder_driver_compact_read_buf(
    GBinderDriverReadBuf* buf)
{
    /*
     * Move the data to the beginning of the buffer to make room for the
     * next portion of data (in case if we need one)
     */
    if (buf->io.consumed > buf->offset) {
        const gsize unprocessed = buf->io.consumed - buf->offset;
        guint8* data = GSIZE_TO_POINTER(buf->io.ptr);

        memmove(data, data + buf->offset, unprocessed);
        buf->io.consumed = unprocessed;
    } else {
        buf->io.consumed = 0;
    }
    buf->offset = 0;
}

static
void
gbinder_driver_handle_commands(
    GBinderDriver* self,
    GBinderDriverContext* context)
{
    GBinderDriverReadBuf* rbuf = context->rbuf;
    guint32 cmd;

    while ((cmd = gbinder_driver_next_command(self, rbuf)) != 0) {
        const gsize datalen = _IOC_SIZE(cmd);
        const gsize total = datalen + sizeof(cmd);
        const void* data = GSIZE_TO_POINTER(rbuf->io.ptr + rbuf->offset + 4);

        /* Handle this command */
        rbuf->offset += total;
        gbinder_driver_handle_command(self, context, cmd, data);
    }

    gbinder_driver_compact_read_buf(rbuf);
}

static
int
gbinder_driver_txstatus(
    GBinderDriver* self,
    GBinderDriverContext* context,
    GBinderRemoteReply* reply)
{
    guint32 cmd;
    int txstatus = (-EAGAIN);
    GBinderDriverReadBuf* rbuf = context->rbuf;
    const guint8* buf = GSIZE_TO_POINTER(rbuf->io.ptr);
    const GBinderIo* io = self->io;

    while (txstatus == (-EAGAIN) && (cmd =
        gbinder_driver_next_command(self, context->rbuf)) != 0) {
        /* The size of the data is encoded in the command code */
        const gsize datalen = _IOC_SIZE(cmd);
        const gsize total = datalen + sizeof(cmd);
        const void* data = buf + rbuf->offset + sizeof(cmd);

        /* Swallow this packet */
        rbuf->offset += total;

        /* Handle the command */
        if (cmd == io->br.transaction_complete) {
            GVERBOSE("> BR_TRANSACTION_COMPLETE");
            if (!reply) {
                txstatus = GBINDER_STATUS_OK;
            }
        } else if (cmd == io->br.dead_reply) {
            GVERBOSE("> BR_DEAD_REPLY");
            txstatus = GBINDER_STATUS_DEAD_OBJECT;
        } else if (cmd == io->br.failed_reply) {
            GVERBOSE("> BR_FAILED_REPLY");
            txstatus = GBINDER_STATUS_FAILED;
        } else if (cmd == io->br.reply) {
            GBinderIoTxData tx;

            io->decode_transaction_data(data, &tx);
            gbinder_driver_verbose_transaction_data("BR_REPLY", &tx);

            /* Transfer data ownership to the reply */
            if (tx.data && tx.size) {
                GBinderBuffer* buf = gbinder_buffer_new(self,
                    tx.data, tx.size, tx.objects);

                gbinder_driver_verbose_dump(' ', (uintptr_t)tx.data, tx.size);
                gbinder_remote_reply_set_data(reply, buf);
                context->bufs = gbinder_buffer_contents_list_add(context->bufs,
                    gbinder_buffer_contents(buf));
            } else {
                GASSERT(!tx.objects);
                gbinder_driver_free_buffer(self, tx.data);
            }

            /*
             * Filter out special cases. It's a bit unfortunate that
             * libgbinder API historically mixed TF_STATUS_CODE payload
             * with special delivery errors. It's not a bit deal though,
             * because in real life TF_STATUS_CODE transactions are not
             * being used that often, if at all.
             */
            switch (tx.status) {
            case (-EAGAIN):
            case GBINDER_STATUS_FAILED:
            case GBINDER_STATUS_DEAD_OBJECT:
                txstatus = (-EFAULT);
                GWARN("Replacing tx status %d with %d", tx.status, txstatus);
                break;
            default:
                txstatus = tx.status;
                break;
            }
        } else {
            gbinder_driver_handle_command(self, context, cmd, data);
        }
    }

    gbinder_driver_compact_read_buf(rbuf);
    return txstatus;
}

/*==========================================================================*
 * Interface
 *
 * This is an internal module, we can assume that GBinderDriver pointer
 * is never NULL, GBinderIpc makes sure of that.
 *==========================================================================*/

GBinderDriver*
gbinder_driver_new(
    const char* dev,
    const GBinderRpcProtocol* protocol)
{
    const int fd = gbinder_system_open(dev, O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        gint32 version = 0;

        if (gbinder_system_ioctl(fd, BINDER_VERSION, &version) >= 0) {
            const GBinderIo* io = NULL;

            /* Decide which kernel we are dealing with */
            GDEBUG("Opened %s version %d", dev, version);
            if (version == gbinder_io_32.version) {
                io = &gbinder_io_32;
            } else if (version == gbinder_io_64.version) {
                io = &gbinder_io_64;
            } else {
                GERR("%s unexpected version %d", dev, version);
            }
            if (io) {
                /* mmap the binder, providing a chunk of virtual address
                 * space to receive transactions. */
                const gsize vmsize = BINDER_VM_SIZE;
                void* vm = gbinder_system_mmap(vmsize, PROT_READ,
                    MAP_PRIVATE | MAP_NORESERVE, fd);
                if (vm != MAP_FAILED) {
                    guint32 max_threads = DEFAULT_MAX_BINDER_THREADS;
                    GBinderDriver* self = g_slice_new0(GBinderDriver);

                    g_atomic_int_set(&self->refcount, 1);
                    self->fd = fd;
                    self->io = io;
                    self->vm = vm;
                    self->vmsize = vmsize;
                    self->dev = g_strdup(dev);
                    if (gbinder_system_ioctl(fd, BINDER_SET_MAX_THREADS,
                        &max_threads) < 0) {
                        GERR("%s failed to set max threads (%u): %s", dev,
                            max_threads, strerror(errno));
                    }
                    /* Choose the protocol based on the device name
                     * if none is explicitly specified */
                    self->protocol = protocol ? protocol :
                        gbinder_rpc_protocol_for_device(dev);
                    return self;
                } else {
                    GERR("%s failed to mmap: %s", dev, strerror(errno));
                }
            }
        } else {
            GERR("Can't get binder version from %s: %s", dev, strerror(errno));
        }
        gbinder_system_close(fd);
    } else {
        GERR("Can't open %s: %s", dev, strerror(errno));
    }
    return NULL;
}

GBinderDriver*
gbinder_driver_ref(
    GBinderDriver* self)
{
    GASSERT(self->refcount > 0);
    g_atomic_int_inc(&self->refcount);
    return self;
}

void
gbinder_driver_unref(
    GBinderDriver* self)
{
    GASSERT(self->refcount > 0);
    if (g_atomic_int_dec_and_test(&self->refcount)) {
        gbinder_driver_close(self);
        g_free(self->dev);
        g_slice_free(GBinderDriver, self);
    }
}

void
gbinder_driver_close(
    GBinderDriver* self)
{
    if (self->vm) {
        GDEBUG("Closing %s", self->dev);
        gbinder_system_munmap(self->vm, self->vmsize);
        gbinder_system_close(self->fd);
        self->fd = -1;
        self->vm = NULL;
        self->vmsize = 0;
    }
}

int
gbinder_driver_fd(
    GBinderDriver* self)
{
    /* Only used by unit tests */
    return self->fd;
}

int
gbinder_driver_poll(
    GBinderDriver* self,
    struct pollfd* pipefd)
{
    struct pollfd fds[2];
    nfds_t n = 1;
    int err;

    memset(fds, 0, sizeof(fds));
    fds[0].fd = self->fd;
    fds[0].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;

    if (pipefd) {
        fds[n].fd = pipefd->fd;
        fds[n].events = pipefd->events;
        n++;
    }

    err = poll(fds, n, -1);
    if (err >= 0) {
        if (pipefd) {
            pipefd->revents = fds[1].revents;
        }
        return fds[0].revents;
    }

    if (pipefd) {
        pipefd->revents = 0;
    }
    return err;
}

const char*
gbinder_driver_dev(
    GBinderDriver* self)
{
    return self->dev;
}

const GBinderIo*
gbinder_driver_io(
    GBinderDriver* self)
{
    return self->io;
}

const GBinderRpcProtocol*
gbinder_driver_protocol(
    GBinderDriver* self)
{
    return self->protocol;
}

gboolean
gbinder_driver_acquire_done(
    GBinderDriver* self,
    GBinderLocalObject* obj)
{
    GBinderIoBuf write;
    guint8 buf[4 + GBINDER_MAX_PTR_COOKIE_SIZE];
    guint32* data = (guint32*)buf;
    const GBinderIo* io = self->io;

    data[0] = io->bc.acquire_done;
    memset(&write, 0, sizeof(write));
    write.ptr = (uintptr_t)buf;
    write.size = 4 + io->encode_ptr_cookie(data + 1, obj);

    GVERBOSE("< BC_ACQUIRE_DONE %p", obj);
    return gbinder_driver_write(self, &write) >= 0;
}

gboolean
gbinder_driver_dead_binder_done(
    GBinderDriver* self,
    GBinderRemoteObject* obj)
{
    if (G_LIKELY(obj)) {
        GBinderIoBuf write;
        guint8 buf[4 + GBINDER_MAX_COOKIE_SIZE];
        guint32* data = (guint32*)buf;
        const GBinderIo* io = self->io;

        data[0] = io->bc.dead_binder_done;
        memset(&write, 0, sizeof(write));
        write.ptr = (uintptr_t)buf;
        write.size = 4 + io->encode_cookie(data + 1, obj->handle);

        GVERBOSE("< BC_DEAD_BINDER_DONE 0x%08x", obj->handle);
        return gbinder_driver_write(self, &write) >= 0;
    } else {
        return FALSE;
    }
}

gboolean
gbinder_driver_request_death_notification(
    GBinderDriver* self,
    GBinderRemoteObject* obj)
{
    if (G_LIKELY(obj)) {
        GVERBOSE("< BC_REQUEST_DEATH_NOTIFICATION 0x%08x", obj->handle);
        return gbinder_driver_handle_cookie(self,
            self->io->bc.request_death_notification, obj);
    } else {
        return FALSE;
    }
}

gboolean
gbinder_driver_clear_death_notification(
    GBinderDriver* self,
    GBinderRemoteObject* obj)
{
    if (G_LIKELY(obj)) {
        GVERBOSE("< BC_CLEAR_DEATH_NOTIFICATION 0x%08x", obj->handle);
        return gbinder_driver_handle_cookie(self,
            self->io->bc.clear_death_notification, obj);
    } else {
        return FALSE;
    }
}

gboolean
gbinder_driver_increfs(
    GBinderDriver* self,
    guint32 handle)
{
    GVERBOSE("< BC_INCREFS 0x%08x", handle);
    return gbinder_driver_cmd_int32(self, self->io->bc.increfs, handle);
}

gboolean
gbinder_driver_decrefs(
    GBinderDriver* self,
    guint32 handle)
{
    GVERBOSE("< BC_DECREFS 0x%08x", handle);
    return gbinder_driver_cmd_int32(self, self->io->bc.decrefs, handle);
}

gboolean
gbinder_driver_acquire(
    GBinderDriver* self,
    guint32 handle)
{
    GVERBOSE("< BC_ACQUIRE 0x%08x", handle);
    return gbinder_driver_cmd_int32(self, self->io->bc.acquire, handle);
}

gboolean
gbinder_driver_release(
    GBinderDriver* self,
    guint32 handle)
{
    GVERBOSE("< BC_RELEASE 0x%08x", handle);
    return gbinder_driver_cmd_int32(self, self->io->bc.release, handle);
}

void
gbinder_driver_close_fds(
    GBinderDriver* self,
    void** objects,
    const void* end)
{
    const GBinderIo* io = self->io;
    void** ptr;

    /* Caller checks objects for NULL */
    for (ptr = objects; *ptr; ptr++) {
        void* obj = *ptr;

        GASSERT(obj < end);
        if (obj < end) {
            int fd;

            if (io->decode_fd_object(obj, (guint8*)end - (guint8*)obj, &fd)) {
                if (close(fd) < 0) {
                    GWARN("Error closing fd %d: %s", fd, strerror(errno));
                }
            }
        }
    }
}

void
gbinder_driver_free_buffer(
    GBinderDriver* self,
    void* buffer)
{
    if (buffer) {
        GBinderIoBuf write;
        const GBinderIo* io = self->io;
        guint8 wbuf[GBINDER_MAX_POINTER_SIZE + sizeof(guint32)];
        guint32* cmd = (guint32*)wbuf;
        guint len = sizeof(*cmd);

        GVERBOSE("< BC_FREE_BUFFER %p", buffer);
        *cmd = io->bc.free_buffer;
        len += io->encode_pointer(wbuf + len, buffer);

        /* Write it */
        write.ptr = (uintptr_t)wbuf;
        write.size = len;
        write.consumed = 0;
        gbinder_driver_write(self, &write);
    }
}

gboolean
gbinder_driver_enter_looper(
    GBinderDriver* self)
{
    GVERBOSE("< BC_ENTER_LOOPER");
    return gbinder_driver_cmd(self, self->io->bc.enter_looper);
}

gboolean
gbinder_driver_exit_looper(
    GBinderDriver* self)
{
    GVERBOSE("< BC_EXIT_LOOPER");
    return gbinder_driver_cmd(self, self->io->bc.exit_looper);
}

int
gbinder_driver_read(
    GBinderDriver* self,
    GBinderObjectRegistry* reg,
    GBinderHandler* handler)
{
    GBinderDriverReadData read;
    GBinderDriverContext context;
    int ret;

    gbinder_driver_read_init(&read);
    gbinder_driver_context_init(&context, &read.buf, reg, handler);
    ret = gbinder_driver_write_read(self, NULL, context.rbuf);
    if (ret >= 0) {
        /* Loop until we have handled all the incoming commands */
        gbinder_driver_handle_commands(self, &context);
        while (read.buf.io.consumed && gbinder_handler_can_loop(handler)) {
            ret = gbinder_driver_write_read(self, NULL, context.rbuf);
            if (ret >= 0) {
                gbinder_driver_handle_commands(self, &context);
            } else {
                break;
            }
        }
    }
    gbinder_driver_context_cleanup(&context);
    return ret;
}

int
gbinder_driver_transact(
    GBinderDriver* self,
    GBinderObjectRegistry* reg,
    GBinderHandler* handler,
    guint32 handle,
    guint32 code,
    GBinderLocalRequest* req,
    GBinderRemoteReply* reply)
{
    GBinderDriverReadData read;
    GBinderDriverContext context;
    GBinderIoBuf write;
    GBinderDriverReadBuf* rbuf = &read.buf;
    const GBinderIo* io = self->io;
    const guint flags = reply ? 0 : GBINDER_TX_FLAG_ONEWAY;
    GBinderOutputData* data = gbinder_local_request_data(req);
    const gsize extra_buffers = gbinder_output_data_buffers_size(data);
    GUtilIntArray* offsets = gbinder_output_data_offsets(data);
    void* offsets_buf = NULL;
    guint8 wbuf[GBINDER_MAX_BC_TRANSACTION_SG_SIZE + sizeof(guint32)];
    guint32* cmd = (guint32*)wbuf;
    guint len = sizeof(*cmd);
    int txstatus = (-EAGAIN);

    gbinder_driver_read_init(&read);
    gbinder_driver_context_init(&context, &read.buf, reg, handler);

    /* Build BC_TRANSACTION */
    if (extra_buffers) {
        GVERBOSE("< BC_TRANSACTION_SG 0x%08x 0x%08x %u bytes", handle, code,
            (guint)extra_buffers);
        gbinder_driver_verbose_dump_bytes(' ', data->bytes);
        *cmd = io->bc.transaction_sg;
        len += io->encode_transaction_sg(wbuf + len, handle, code,
            data->bytes, flags, offsets, &offsets_buf, extra_buffers);
    } else {
        GVERBOSE("< BC_TRANSACTION 0x%08x 0x%08x", handle, code);
        gbinder_driver_verbose_dump_bytes(' ', data->bytes);
        *cmd = io->bc.transaction;
        len += io->encode_transaction(wbuf + len, handle, code,
            data->bytes, flags, offsets, &offsets_buf);
    }

#if 0 /* GUTIL_LOG_VERBOSE */
    if (offsets && offsets->count) {
        gbinder_driver_verbose_dump('<', (uintptr_t)offsets_buf,
            offsets->count * io->pointer_size);
    }
#endif /* GUTIL_LOG_VERBOSE */

    /* Write it */
    write.ptr = (uintptr_t)wbuf;
    write.size = len;
    write.consumed = 0;

    /* And wait for reply. Positive txstatus is the transaction status,
     * negative is a driver error (except for -EAGAIN meaning that there's
     * no status yet) */
    while (txstatus == (-EAGAIN)) {
        int err = gbinder_driver_write_read(self, &write, rbuf);
        if (err < 0) {
            txstatus = err;
        } else {
            txstatus = gbinder_driver_txstatus(self, &context, reply);
        }
    }

    if (txstatus >= 0) {
        /* The whole thing should've been written in case of success */
        GASSERT(write.consumed == write.size || txstatus > 0);

        /* Loop until we have handled all the incoming commands */
        gbinder_driver_handle_commands(self, &context);
        while (rbuf->io.consumed) {
            int err = gbinder_driver_write_read(self, NULL, rbuf);
            if (err < 0) {
                txstatus = err;
                break;
            } else {
                gbinder_driver_handle_commands(self, &context);
            }
        }
    }

    gbinder_driver_context_cleanup(&context);
    g_free(offsets_buf);
    return txstatus;
}

GBinderLocalRequest*
gbinder_driver_local_request_new(
    GBinderDriver* self,
    const char* iface)
{
    return gbinder_local_request_new_iface(self->io, self->protocol, iface);
}

GBinderLocalRequest*
gbinder_driver_local_request_new_ping(
    GBinderDriver* self)
{
    GBinderLocalRequest* req = gbinder_local_request_new(self->io, NULL);
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    self->protocol->write_ping(&writer);
    return req;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
