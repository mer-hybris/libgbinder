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

#ifndef GBINDER_IO_H
#define GBINDER_IO_H

#include "gbinder_types_p.h"

#include <stdint.h>

typedef struct gbinder_io_buf {
    uintptr_t ptr;
    gsize size;
    gsize consumed;
} GBinderIoBuf;

typedef struct gbinder_io_buffer_object {
    void* data;
    gsize size;
    gsize parent_offset;
    gboolean has_parent;
} GBinderIoBufferObject;

typedef struct gbinder_io_tx_data {
    int status;
    guint32 code;
    guint32 flags;   /* GBINDER_TX_FLAG_xxx */
    pid_t pid;
    uid_t euid;
    void* target;
    void* data;
    gsize size;
    void** objects;
} GBinderIoTxData;

/* Read buffer size (allocated on stack, shouldn't be too large) */
#define GBINDER_IO_READ_BUFFER_SIZE (128)

/*
 * There are (at least) 2 versions of the binder ioctl API, implemented by
 * 32-bit and 64-bit kernels. The ioctl codes, transaction commands - many
 * of those are derived from the sizes of the structures being passed
 * between the driver and the user space client. All these differences
 * are abstracted away by GBinderIo interfaces.
 *
 * The API version is returned by BINDER_VERSION ioctl which itself doesn't
 * depend on the API version (it would be very strange if it did).
 */

struct gbinder_io {
    int version;
    guint pointer_size;

    /* Driver command protocol */
    struct gbinder_io_command_codes {
        guint transaction;
        guint reply;
        guint acquire_result;
        guint free_buffer;
        guint increfs;
        guint acquire;
        guint release;
        guint decrefs;
        guint increfs_done;
        guint acquire_done;
        guint attempt_acquire;
        guint register_looper;
        guint enter_looper;
        guint exit_looper;
        guint request_death_notification;
        guint clear_death_notification;
        guint dead_binder_done;
        guint transaction_sg;
        guint reply_sg;
    } bc;

    /* Driver return protocol */
    struct gbinder_io_return_codes {
        guint error;
        guint ok;
        guint transaction;
        guint reply;
        guint acquire_result;
        guint dead_reply;
        guint transaction_complete;
        guint increfs;
        guint acquire;
        guint release;
        guint decrefs;
        guint attempt_acquire;
        guint noop;
        guint spawn_looper;
        guint finished;
        guint dead_binder;
        guint clear_death_notification_done;
        guint failed_reply;
    } br;

    /* Size of the object and its extra data */
    gsize (*object_size)(const void* obj);
    gsize (*object_data_size)(const void* obj);

    /* Writes pointer to the buffer. The destination buffer must have
     * at least GBINDER_IO_MAX_POINTER_SIZE bytes available. The
     * actual size is returned. */
#define GBINDER_MAX_POINTER_SIZE (8)
    guint (*encode_pointer)(void* out, const void* pointer);

    /* Writes cookie to the buffer. The destination buffer must have
     * at least GBINDER_IO_MAX_COOKIE_SIZE bytes available. The
     * actual size is returned. */
#define GBINDER_MAX_COOKIE_SIZE GBINDER_MAX_POINTER_SIZE
    guint (*encode_cookie)(void* out, guint64 cookie);

    /* Encode flat_buffer_object */
#define GBINDER_MAX_BINDER_OBJECT_SIZE (24)
    guint (*encode_local_object)(void* out, GBinderLocalObject* obj);
    guint (*encode_remote_object)(void* out, GBinderRemoteObject* obj);
    guint (*encode_fd_object)(void* out, int fd);

    /* Encode binder_buffer_object */
#define GBINDER_MAX_BUFFER_OBJECT_SIZE (40)
    guint (*encode_buffer_object)(void* out, const void* data, gsize size,
        const GBinderParent* parent);

    /* Encode binder_handle_cookie */
#define GBINDER_MAX_HANDLE_COOKIE_SIZE (12)
    guint (*encode_handle_cookie)(void* out, GBinderRemoteObject* obj);

    /* Encode binder_ptr_cookie */
#define GBINDER_MAX_PTR_COOKIE_SIZE (16)
    guint (*encode_ptr_cookie)(void* out, GBinderLocalObject* obj);

    /* Encode BC_TRANSACTION/BC_TRANSACTION_SG data */
#define GBINDER_MAX_BC_TRANSACTION_SIZE (64)
    guint (*encode_transaction)(void* out, guint32 handle, guint32 code,
        const GByteArray* data, guint flags /* See below */,
        GUtilIntArray* offsets, void** offsets_buf);
#define GBINDER_MAX_BC_TRANSACTION_SG_SIZE (72)
    guint (*encode_transaction_sg)(void* out, guint32 handle, guint32 code,
        const GByteArray* data, guint flags /* GBINDER_TX_FLAG_xxx */,
        GUtilIntArray* offsets, void** offsets_buf,
        gsize buffers_size);

    /* Encode BC_REPLY/REPLY_SG data */
#define GBINDER_MAX_BC_REPLY_SIZE GBINDER_MAX_BC_TRANSACTION_SIZE
    guint (*encode_reply)(void* out, guint32 handle, guint32 code,
        const GByteArray* data, GUtilIntArray* offsets, void** offsets_buf);
#define GBINDER_MAX_BC_REPLY_SG_SIZE GBINDER_MAX_BC_TRANSACTION_SG_SIZE
    guint (*encode_reply_sg)(void* out, guint32 handle, guint32 code,
        const GByteArray* data, GUtilIntArray* offsets, void** offsets_buf,
        gsize buffers_size);

    /* Encode BC_REPLY */
    guint (*encode_status_reply)(void* out, gint32* status);

    /* Decoders */
    void (*decode_transaction_data)(const void* data, GBinderIoTxData* tx);
    void* (*decode_ptr_cookie)(const void* data);
    guint (*decode_cookie)(const void* data, guint64* cookie);
    guint (*decode_binder_handle)(const void* obj, guint32* handle);
    guint (*decode_binder_object)(const void* data, gsize size,
        GBinderObjectRegistry* reg, GBinderRemoteObject** obj);
    guint (*decode_buffer_object)(GBinderBuffer* buf, gsize offset,
        GBinderIoBufferObject* out);
    guint (*decode_fd_object)(const void* data, gsize size, int* fd);

    /* ioctl wrappers */
    int (*write_read)(int fd, GBinderIoBuf* write, GBinderIoBuf* read);
};

extern const GBinderIo gbinder_io_32 GBINDER_INTERNAL;
extern const GBinderIo gbinder_io_64 GBINDER_INTERNAL;

#endif /* GBINDER_IO_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
