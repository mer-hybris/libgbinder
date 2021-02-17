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

#include "gbinder_io.h"
#include "gbinder_buffer_p.h"
#include "gbinder_local_object_p.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_object_registry.h"
#include "gbinder_writer.h"
#include "gbinder_system.h"
#include "gbinder_log.h"

#include "binder.h"

#include <gutil_intarray.h>
#include <gutil_macros.h>

#include <errno.h>

/*
 * This file is included from gbinder_io_32.c and gbinder_io_64.c to
 * generate the code for different ioctl codes and structure sizes.
 */

#define GBINDER_POINTER_SIZE sizeof(binder_uintptr_t)

#define GBINDER_IO_FN__(prefix,suffix) prefix##_##suffix
#define GBINDER_IO_FN_(prefix,suffix) GBINDER_IO_FN__(prefix,suffix)
#define GBINDER_IO_FN(fn) GBINDER_IO_FN_(GBINDER_IO_PREFIX,fn)

static
int
GBINDER_IO_FN(write_read)(
    int fd,
    GBinderIoBuf* write,
    GBinderIoBuf* read)
{
    int ret;
    struct binder_write_read bwr;

    memset(&bwr, 0, sizeof(bwr));
    if (write) {
        bwr.write_buffer = write->ptr + write->consumed;
        bwr.write_size =  write->size - write->consumed;
    }
    if (read) {
        bwr.read_buffer = read->ptr + read->consumed;
        bwr.read_size = read->size - read->consumed;
    }
    ret = gbinder_system_ioctl(fd, BINDER_WRITE_READ, &bwr);
    if (ret >= 0) {
        if (write) {
            write->consumed += bwr.write_consumed;
        }
        if (read) {
            read->consumed += bwr.read_consumed;
        }
    } else {
        GERR("binder_write_read: %s", strerror(errno));
    }
    return ret;
}

/* Returns size of the object */
static
gsize
GBINDER_IO_FN(object_size)(
    const void* obj)
{
    if (obj) {
        const struct binder_object_header* hdr = obj;

        switch (hdr->type) {
        case BINDER_TYPE_BINDER:
        case BINDER_TYPE_WEAK_BINDER:
        case BINDER_TYPE_HANDLE:
        case BINDER_TYPE_WEAK_HANDLE:
            return sizeof(struct flat_binder_object);
        case BINDER_TYPE_FD:
            return sizeof(struct binder_fd_object);
        case BINDER_TYPE_FDA:
            return sizeof(struct binder_fd_array_object);
        case BINDER_TYPE_PTR:
            return sizeof(struct binder_buffer_object);
        }
    }
    return 0;
}

/* Returns size of the object's extra data */
static
gsize
GBINDER_IO_FN(object_data_size)(
    const void* obj)
{
    if (obj) {
        const struct binder_object_header* hdr = obj;

        switch (hdr->type) {
        case BINDER_TYPE_PTR:
            return ((struct binder_buffer_object*)obj)->length;
        case BINDER_TYPE_FDA:
            return ((struct binder_fd_array_object*)obj)->num_fds * 4;
       }
    }
    return 0;
}

/* Writes pointer to the buffer */
static
guint
GBINDER_IO_FN(encode_pointer)(
    void* out,
    const void* pointer)
{
    binder_uintptr_t* dest = out;

    *dest = (uintptr_t)pointer;
    return sizeof(*dest);
}

/* Writes cookie to the buffer */
static
guint
GBINDER_IO_FN(encode_cookie)(
    void* out,
    guint64 cookie)
{
    binder_uintptr_t* dest = out;

    *dest = (uintptr_t)cookie;
    return sizeof(*dest);
}

/* Encodes flat_binder_object */
static
guint
GBINDER_IO_FN(encode_local_object)(
    void* out,
    GBinderLocalObject* obj)
{
    struct flat_binder_object* dest = out;

    memset(dest, 0, sizeof(*dest));
    if (obj) {
        dest->hdr.type = BINDER_TYPE_BINDER;
        dest->flags = 0x7f | FLAT_BINDER_FLAG_ACCEPTS_FDS;
        dest->binder = (uintptr_t)obj;
    } else {
        dest->hdr.type = BINDER_TYPE_WEAK_BINDER;
    }
    return sizeof(*dest);
}

static
guint
GBINDER_IO_FN(encode_remote_object)(
    void* out,
    GBinderRemoteObject* obj)
{
    struct flat_binder_object* dest = out;

    memset(dest, 0, sizeof(*dest));
    if (obj) {
        dest->hdr.type = BINDER_TYPE_HANDLE;
        dest->flags = FLAT_BINDER_FLAG_ACCEPTS_FDS;
        dest->handle = obj->handle;
    } else {
        dest->hdr.type = BINDER_TYPE_BINDER;
    }
    return sizeof(*dest);
}

static
guint
GBINDER_IO_FN(encode_fd_object)(
    void* out,
    int fd)
{
    struct binder_fd_object* dest = out;

    memset(dest, 0, sizeof(*dest));
    dest->hdr.type = BINDER_TYPE_FD;
    dest->pad_flags = 0x7f | FLAT_BINDER_FLAG_ACCEPTS_FDS;
    dest->fd = fd;
    return sizeof(*dest);
}

/* Encodes binder_buffer_object */
static
guint
GBINDER_IO_FN(encode_buffer_object)(
    void* out,
    const void* data,
    gsize size,
    const GBinderParent* parent)
{
    struct binder_buffer_object* dest = out;

    memset(dest, 0, sizeof(*dest));
    dest->hdr.type = BINDER_TYPE_PTR;
    dest->buffer = (uintptr_t)data;
    dest->length = size;
    if (parent) {
        dest->flags |= BINDER_BUFFER_FLAG_HAS_PARENT;
        dest->parent = parent->index;
        dest->parent_offset = parent->offset;
    }
    return sizeof(*dest);
}

static
guint
GBINDER_IO_FN(encode_handle_cookie)(
    void* out,
    GBinderRemoteObject* obj)
{
    struct binder_handle_cookie* dest = out;

    /* We find the object by handle, so we use handle as a cookie */
    dest->handle = obj->handle;
    dest->cookie = obj->handle;
    return sizeof(*dest);
}

static
guint
GBINDER_IO_FN(encode_ptr_cookie)(
    void* out,
    GBinderLocalObject* obj)
{
    struct binder_ptr_cookie* dest = out;

    /* We never send these cookies and don't expect them back */
    dest->ptr = (uintptr_t)obj;
    dest->cookie = 0;
    return sizeof(dest);
}

/* Fills binder_transaction_data for BC_TRANSACTION/REPLY */
static
void
GBINDER_IO_FN(fill_transaction_data)(
    struct binder_transaction_data* tr,
    guint32 handle,
    guint32 code,
    const GByteArray* payload,
    guint tx_flags,
    GUtilIntArray* offsets,
    void** offsets_buf)
{
    memset(tr, 0, sizeof(*tr));
    tr->target.handle = handle;
    tr->code = code;
    tr->data_size = payload->len;
    tr->data.ptr.buffer = (uintptr_t)payload->data;
    tr->flags = tx_flags;
    if (offsets && offsets->count) {
        guint i;
        binder_size_t* tx_offsets = g_new(binder_size_t, offsets->count);

        tr->offsets_size = offsets->count * sizeof(binder_size_t);
        tr->data.ptr.offsets = (uintptr_t)tx_offsets;
        for (i = 0; i < offsets->count; i++) {
            tx_offsets[i] = offsets->data[i];
        }
        *offsets_buf = tx_offsets;
    } else {
        *offsets_buf = NULL;
    }
}

/* Encodes BC_TRANSACTION data */
static
guint
GBINDER_IO_FN(encode_transaction)(
    void* out,
    guint32 handle,
    guint32 code,
    const GByteArray* payload,
    guint flags,
    GUtilIntArray* offsets,
    void** offsets_buf)
{
    struct binder_transaction_data* tr = out;

    GBINDER_IO_FN(fill_transaction_data)(tr, handle, code, payload,
        (flags & GBINDER_TX_FLAG_ONEWAY) ? TF_ONE_WAY : TF_ACCEPT_FDS,
        offsets, offsets_buf);
    return sizeof(*tr);
}

/* Encodes BC_TRANSACTION_SG data */
static
guint
GBINDER_IO_FN(encode_transaction_sg)(
    void* out,
    guint32 handle,
    guint32 code,
    const GByteArray* payload,
    guint flags,
    GUtilIntArray* offsets,
    void** offsets_buf,
    gsize buffers_size)
{
    struct binder_transaction_data_sg* sg = out;

    GBINDER_IO_FN(fill_transaction_data)(&sg->transaction_data, handle, code,
        payload, (flags & GBINDER_TX_FLAG_ONEWAY) ? TF_ONE_WAY : TF_ACCEPT_FDS,
        offsets, offsets_buf);
    /* The driver seems to require buffers to be 8-byte aligned */
    sg->buffers_size = G_ALIGN8(buffers_size);
    return sizeof(*sg);
}

/* Encodes BC_REPLY data */
static
guint
GBINDER_IO_FN(encode_reply)(
    void* out,
    guint32 handle,
    guint32 code,
    const GByteArray* payload,
    GUtilIntArray* offsets,
    void** offsets_buf)
{
    struct binder_transaction_data* tr = out;

    GBINDER_IO_FN(fill_transaction_data)(tr, handle, code, payload, 0,
        offsets, offsets_buf);
    return sizeof(*tr);
}

/* Encodes BC_REPLY_SG data */
static
guint
GBINDER_IO_FN(encode_reply_sg)(
    void* out,
    guint32 handle,
    guint32 code,
    const GByteArray* payload,
    GUtilIntArray* offsets,
    void** offsets_buf,
    gsize buffers_size)
{
    struct binder_transaction_data_sg* sg = out;

    GBINDER_IO_FN(fill_transaction_data)(&sg->transaction_data, handle, code,
        payload, 0, offsets, offsets_buf);
    /* The driver seems to require buffers to be 8-byte aligned */
    sg->buffers_size = G_ALIGN8(buffers_size);
    return sizeof(*sg);
}

/* Encode BC_REPLY with just status */
static
guint
GBINDER_IO_FN(encode_status_reply)(
    void* out,
    gint32* status)
{
    struct binder_transaction_data* tr = out;

    memset(tr, 0, sizeof(*tr));
    tr->flags = TF_STATUS_CODE;
    tr->data_size = sizeof(*status);
    tr->data.ptr.buffer = (uintptr_t)status;
    return sizeof(*tr);
}

/* Decode BR_REPLY and BR_TRANSACTION */
static
void
GBINDER_IO_FN(decode_transaction_data)(
    const void* data,
    GBinderIoTxData* tx)
{
    const struct binder_transaction_data* tr = data;

    tx->objects = NULL;
    tx->code = tr->code;
    tx->flags = 0;
    tx->pid = tr->sender_pid;
    tx->euid = tr->sender_euid;
    tx->target = (void*)(uintptr_t)tr->target.ptr;
    tx->data = (void*)(uintptr_t)tr->data.ptr.buffer;
    if (tr->flags & TF_STATUS_CODE) {
        GASSERT(tr->data_size == 4);
        tx->status = *((gint32*)tx->data);
        tx->size = 0;
    } else {
        guint objcount = tr->offsets_size/sizeof(binder_size_t);
        const binder_size_t* offsets = (void*)(uintptr_t)tr->data.ptr.offsets;

        tx->status = GBINDER_STATUS_OK;
        tx->size = tr->data_size;
        if (tr->flags & TF_ONE_WAY) {
            tx->flags |= GBINDER_TX_FLAG_ONEWAY;
        }

        if (objcount > 0) {
            binder_size_t min_offset = 0;
            guint i;

            /* Validate the offsets */
            for (i = 0; i < objcount; i++) {
                if (offsets[i] < min_offset || (offsets[i] +
                    sizeof(struct flat_binder_object)) > tx->size) {
                    GWARN("Invalid offset");
                    objcount = 0;
                    break;
                }
                min_offset = offsets[i] + sizeof(struct flat_binder_object);
            }

            if (objcount > 0) {
                tx->objects = g_new(void*, objcount + 1);
                for (i = 0; i < objcount; i++) {
                    tx->objects[i] = (guint8*)tx->data + offsets[i];
                }
                tx->objects[objcount] = NULL;
            }
        }
    }
}

/* Decode binder_uintptr_t */
static
guint
GBINDER_IO_FN(decode_cookie)(
    const void* data,
    guint64* cookie)
{
    const binder_uintptr_t* ptr = data;

    if (cookie) *cookie = *ptr;
    return sizeof(*ptr);
}

/* Decode struct binder_ptr_cookie */
static
void*
GBINDER_IO_FN(decode_ptr_cookie)(
    const void* data)
{
    const struct binder_ptr_cookie* ptr = data;

    /* We never send cookie and don't expect it back */
    GASSERT(!ptr->cookie);
    return (void*)(uintptr_t)ptr->ptr;
}

static
guint
GBINDER_IO_FN(decode_binder_handle)(
    const void* data,
    guint32* handle)
{
    const struct flat_binder_object* obj = data;

    /* Caller guarantees that data points to an object */
    if (obj->hdr.type == BINDER_TYPE_HANDLE) {
        if (handle) {
            *handle = obj->handle;
        }
        return sizeof(*obj);
    }
    return 0;
}

static
guint
GBINDER_IO_FN(decode_binder_object)(
    const void* data,
    gsize size,
    GBinderObjectRegistry* reg,
    GBinderRemoteObject** out)
{
    const struct flat_binder_object* obj = data;

    if (size >= sizeof(*obj)) {
        switch (obj->hdr.type) {
        case BINDER_TYPE_HANDLE:
            if (out) {
                *out = gbinder_object_registry_get_remote(reg, obj->handle,
                    REMOTE_REGISTRY_CAN_CREATE_AND_ACQUIRE);
            }
            return sizeof(*obj);
        case BINDER_TYPE_BINDER:
            if (!obj->binder) {
                /* That's a NULL reference */
                if (out) {
                    *out = NULL;
                }
                return sizeof(*obj);
            }
            /* fallthrough */
        default:
            GERR("Unsupported binder object type 0x%08x", obj->hdr.type);
            break;
        }
    }
    if (out) *out = NULL;
    return 0;
}

static
guint
GBINDER_IO_FN(decode_buffer_object)(
    GBinderBuffer* buf,
    gsize offset,
    GBinderIoBufferObject* out)
{
    const void* data = (guint8*)buf->data + offset;
    const gsize size = (offset < buf->size) ? (buf->size - offset) : 0;
    const struct binder_buffer_object* flat = data;

    if (size >= sizeof(*flat) && flat->hdr.type == BINDER_TYPE_PTR) {
        if (out) {
            out->data = (void*)(uintptr_t)flat->buffer;
            out->size = (gsize)flat->length;
            out->parent_offset = (gsize)flat->parent_offset;
            out->has_parent = (flat->flags & BINDER_BUFFER_FLAG_HAS_PARENT) ?
                TRUE : FALSE;
        }
        return sizeof(*flat);
    }
    return 0;
}

static
guint
GBINDER_IO_FN(decode_fd_object)(
    const void* data,
    gsize size,
    int* fd)
{
    const struct flat_binder_object* obj = data;

    if (size >= sizeof(*obj)) {
        switch (obj->hdr.type) {
        case BINDER_TYPE_FD:
            if (fd) *fd = obj->handle;
            return sizeof(*obj);
        default:
            break;
        }
    }
    if (fd) *fd = -1;
    return 0;
}

const GBinderIo GBINDER_IO_PREFIX = {
    .version = BINDER_CURRENT_PROTOCOL_VERSION,
    .pointer_size = GBINDER_POINTER_SIZE,

    /* Driver command protocol */
    .bc = {
        .transaction = BC_TRANSACTION,
        .reply = BC_REPLY,
        .acquire_result = BC_ACQUIRE_RESULT,
        .free_buffer = BC_FREE_BUFFER,
        .increfs = BC_INCREFS,
        .acquire = BC_ACQUIRE,
        .release = BC_RELEASE,
        .decrefs = BC_DECREFS,
        .increfs_done = BC_INCREFS_DONE,
        .acquire_done = BC_ACQUIRE_DONE,
        .attempt_acquire = BC_ATTEMPT_ACQUIRE,
        .register_looper = BC_REGISTER_LOOPER,
        .enter_looper = BC_ENTER_LOOPER,
        .exit_looper = BC_EXIT_LOOPER,
        .request_death_notification = BC_REQUEST_DEATH_NOTIFICATION,
        .clear_death_notification = BC_CLEAR_DEATH_NOTIFICATION,
        .dead_binder_done = BC_DEAD_BINDER_DONE,
        .transaction_sg = BC_TRANSACTION_SG,
        .reply_sg = BC_REPLY_SG
    },

    /* Driver return protocol */
    .br = {
        .error = BR_ERROR,
        .ok = BR_OK,
        .transaction = BR_TRANSACTION,
        .reply = BR_REPLY,
        .acquire_result = BR_ACQUIRE_RESULT,
        .dead_reply = BR_DEAD_REPLY,
        .transaction_complete = BR_TRANSACTION_COMPLETE,
        .increfs = BR_INCREFS,
        .acquire = BR_ACQUIRE,
        .release = BR_RELEASE,
        .decrefs = BR_DECREFS,
        .attempt_acquire = BR_ATTEMPT_ACQUIRE,
        .noop = BR_NOOP,
        .spawn_looper = BR_SPAWN_LOOPER,
        .finished = BR_FINISHED,
        .dead_binder = BR_DEAD_BINDER,
        .clear_death_notification_done = BR_CLEAR_DEATH_NOTIFICATION_DONE,
        .failed_reply = BR_FAILED_REPLY
    },

    .object_size = GBINDER_IO_FN(object_size),
    .object_data_size = GBINDER_IO_FN(object_data_size),

    /* Encoders */
    .encode_pointer = GBINDER_IO_FN(encode_pointer),
    .encode_cookie = GBINDER_IO_FN(encode_cookie),
    .encode_local_object = GBINDER_IO_FN(encode_local_object),
    .encode_remote_object = GBINDER_IO_FN(encode_remote_object),
    .encode_fd_object = GBINDER_IO_FN(encode_fd_object),
    .encode_buffer_object = GBINDER_IO_FN(encode_buffer_object),
    .encode_handle_cookie = GBINDER_IO_FN(encode_handle_cookie),
    .encode_ptr_cookie = GBINDER_IO_FN(encode_ptr_cookie),
    .encode_transaction = GBINDER_IO_FN(encode_transaction),
    .encode_transaction_sg = GBINDER_IO_FN(encode_transaction_sg),
    .encode_reply = GBINDER_IO_FN(encode_reply),
    .encode_reply_sg = GBINDER_IO_FN(encode_reply_sg),
    .encode_status_reply = GBINDER_IO_FN(encode_status_reply),

    /* Decoders */
    .decode_transaction_data = GBINDER_IO_FN(decode_transaction_data),
    .decode_cookie = GBINDER_IO_FN(decode_cookie),
    .decode_ptr_cookie = GBINDER_IO_FN(decode_ptr_cookie),
    .decode_binder_handle = GBINDER_IO_FN(decode_binder_handle),
    .decode_binder_object = GBINDER_IO_FN(decode_binder_object),
    .decode_buffer_object = GBINDER_IO_FN(decode_buffer_object),
    .decode_fd_object = GBINDER_IO_FN(decode_fd_object),

    /* ioctl wrappers */
    .write_read = GBINDER_IO_FN(write_read)
};

/* Compile time constraints */
G_STATIC_ASSERT(GBINDER_POINTER_SIZE <= GBINDER_MAX_POINTER_SIZE);
G_STATIC_ASSERT(sizeof(struct flat_binder_object) <=
    GBINDER_MAX_BINDER_OBJECT_SIZE);
G_STATIC_ASSERT(sizeof(struct binder_buffer_object) <=
    GBINDER_MAX_BUFFER_OBJECT_SIZE);
G_STATIC_ASSERT(sizeof(struct binder_handle_cookie) <=
    GBINDER_MAX_HANDLE_COOKIE_SIZE);
G_STATIC_ASSERT(sizeof(struct binder_transaction_data) <=
    GBINDER_MAX_BC_TRANSACTION_SIZE);
G_STATIC_ASSERT(sizeof(struct binder_transaction_data_sg) <=
    GBINDER_MAX_BC_TRANSACTION_SG_SIZE);

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
