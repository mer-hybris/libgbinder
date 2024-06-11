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

#ifndef GBINDER_TYPES_H
#define GBINDER_TYPES_H

#include <gutil_types.h>

G_BEGIN_DECLS

#define GBINDER_LOG_MODULE gbinder_log

/*
 * Terminology:
 *
 * 1. RemoteObject is the one we sent requests to.
 * 2. LocalObjects may receive incoming transactions (and reply to them)
 * 3. We must have a RemoteObject to initiate a transaction.
 *    We send LocalRequest and receive RemoteReply:
 *
 *    LocalObject --- (LocalRequest) --> Client(RemoteObject)
 *    LocalObject <-- (RemoteReply) -- RemoteObject
 *
 * 4. LocalObject knows caller's pid (and therefore credentials)
 *
 *    LocalObject <-- (RemoteRequest) --- (pid)
 *    LocalObject --- (LocalReply)    --> (pid)
 *
 * 5. Writer prepares the data for LocalRequest and LocalReply
 * 6. Reader parses the data coming with RemoteRequest and RemoteReply
 */

typedef struct gbinder_bridge GBinderBridge; /* Since 1.1.5 */
typedef struct gbinder_buffer GBinderBuffer;
typedef struct gbinder_client GBinderClient;
typedef struct gbinder_fmq GBinderFmq;  /* Since 1.1.14 */
typedef struct gbinder_ipc GBinderIpc;
typedef struct gbinder_local_object GBinderLocalObject;
typedef struct gbinder_local_reply GBinderLocalReply;
typedef struct gbinder_local_request GBinderLocalRequest;
typedef struct gbinder_reader GBinderReader;
typedef struct gbinder_remote_object GBinderRemoteObject;
typedef struct gbinder_remote_reply GBinderRemoteReply;
typedef struct gbinder_remote_request GBinderRemoteRequest;
typedef struct gbinder_servicename GBinderServiceName;
typedef struct gbinder_servicemanager GBinderServiceManager;
typedef struct gbinder_writer GBinderWriter;
typedef struct gbinder_parent GBinderParent;

/* Basic HIDL types */

#define GBINDER_ALIGNED(x) __attribute__ ((aligned(x)))

typedef struct gbinder_hidl_vec {
    union {
        guint64 value;
        const void* ptr;
    } data;
    guint32 count;
    guint8 owns_buffer;
    guint8 pad[3];
} GBinderHidlVec;

#define GBINDER_HIDL_VEC_BUFFER_OFFSET (0)
G_STATIC_ASSERT(sizeof(GBinderHidlVec) == 16);

typedef struct gbinder_hidl_string {
    union {
        guint64 value;
        const char* str;
    } data;
    guint32 len;
    guint8 owns_buffer;
    guint8 pad[3];
} GBinderHidlString;

#define GBINDER_HIDL_STRING_BUFFER_OFFSET (0)
G_STATIC_ASSERT(sizeof(GBinderHidlString) == 16);

typedef struct gbinder_fds {
    guint32 version GBINDER_ALIGNED(4);
    guint32 num_fds GBINDER_ALIGNED(4);
    guint32 num_ints GBINDER_ALIGNED(4);
} GBINDER_ALIGNED(4) GBinderFds;  /* Since 1.1.4 */

/* Actual fds immediately follow GBinderFds: */
#define gbinder_fds_get_fd(fds,i) (((const int*)((fds) + 1))[i])

#define GBINDER_HIDL_FDS_VERSION (12)
G_STATIC_ASSERT(sizeof(GBinderFds) == GBINDER_HIDL_FDS_VERSION);

typedef struct gbinder_hidl_handle {
    union {
        guint64 value;
        const GBinderFds* fds;
    } data;
    guint8 owns_handle;
    guint8 pad[7];
} GBinderHidlHandle; /* Since 1.1.4 */

#define GBINDER_HIDL_HANDLE_VALUE_OFFSET (0)
G_STATIC_ASSERT(sizeof(GBinderHidlHandle) == 16);

typedef struct gbinder_hidl_memory {
    union {
        guint64 value;
        const GBinderFds* fds;
    } data;
    guint8 owns_buffer;
    guint8 pad[7];
    guint64 size;
    GBinderHidlString name;
} GBinderHidlMemory; /* Since 1.1.4 */

#define GBINDER_HIDL_MEMORY_PTR_OFFSET (0)
#define GBINDER_HIDL_MEMORY_NAME_OFFSET (24)
G_STATIC_ASSERT(sizeof(GBinderHidlMemory) == 40);

/*
 * Each RPC call is identified by the interface name returned
 * by gbinder_remote_request_interface() the transaction code.
 * Transaction code itself is not unique.
 *
 * The value returned from GBinderLocalTransactFunc callback will
 * be ignored for one-way transactions. If GBINDER_TX_FLAG_ONEWAY
 * is passed in, the callback may and should return NULL and that
 * won't be interpreted as an error.
 */
typedef
GBinderLocalReply*
(*GBinderLocalTransactFunc)(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data);

#define GBINDER_TX_FLAG_ONEWAY (0x01)

/* Other possible errors added from system/core/libutils/include/utils/Errors.h
 * Legacy GBINDER_STATUS values preserved, but translated over the wire.
 */
typedef enum gbinder_status {
    GBINDER_STATUS_OK = 0,
    GBINDER_STATUS_FAILED, /* maps to UNKNOWN_ERROR */
    GBINDER_STATUS_DEAD_OBJECT,
    GBINDER_STATUS_UNKNOWN_ERROR       = (-2147483647-1), // INT32_MIN
    GBINDER_STATUS_NO_MEMORY           = -ENOMEM,
    GBINDER_STATUS_INVALID_OPERATION   = -ENOSYS,
    GBINDER_STATUS_BAD_VALUE           = -EINVAL,
    GBINDER_STATUS_BAD_TYPE            = (GBINDER_STATUS_UNKNOWN_ERROR + 1),
    GBINDER_STATUS_NAME_NOT_FOUND      = -ENOENT,
    GBINDER_STATUS_PERMISSION_DENIED   = -EPERM,
    GBINDER_STATUS_NO_INIT             = -ENODEV,
    GBINDER_STATUS_ALREADY_EXISTS      = -EEXIST,
    GBINDER_STATUS_FAILED_TRANSACTION  = (GBINDER_STATUS_UNKNOWN_ERROR + 2),
#if !defined(_WIN32)
    GBINDER_STATUS_BAD_INDEX           = -EOVERFLOW,
    GBINDER_STATUS_NOT_ENOUGH_DATA     = -ENODATA,
    GBINDER_STATUS_WOULD_BLOCK         = -EWOULDBLOCK, 
    GBINDER_STATUS_TIMED_OUT           = -ETIMEDOUT,
    GBINDER_STATUS_UNKNOWN_TRANSACTION = -EBADMSG,
#else
    GBINDER_STATUS_BAD_INDEX           = -E2BIG,
    GBINDER_STATUS_NOT_ENOUGH_DATA     = (GBINDER_STATUS_UNKNOWN_ERROR + 3),
    GBINDER_STATUS_WOULD_BLOCK         = (GBINDER_STATUS_UNKNOWN_ERROR + 4),
    GBINDER_STATUS_TIMED_OUT           = (GBINDER_STATUS_UNKNOWN_ERROR + 5),
    GBINDER_STATUS_UNKNOWN_TRANSACTION = (GBINDER_STATUS_UNKNOWN_ERROR + 6),
#endif    
    GBINDER_STATUS_FDS_NOT_ALLOWED     = (GBINDER_STATUS_UNKNOWN_ERROR + 7),
    GBINDER_STATUS_UNEXPECTED_NULL     = (GBINDER_STATUS_UNKNOWN_ERROR + 8),
} GBINDER_STATUS;

/* Utility macros for generating internal binder transaction codes 
 * such as DUMP and INTERFACE
 */
#define GBINDER_FOURCC(c1,c2,c3,c4) \
    (((c1) << 24) | ((c2) << 16) | ((c3) << 8) | (c4))
#define GBINDER_AIDL_TRANSACTION(c2,c3,c4)  GBINDER_FOURCC('_',c2,c3,c4)
#define GBINDER_HIDL_TRANSACTION(c2,c3,c4)  GBINDER_FOURCC(0x0f,c2,c3,c4)

#define GBINDER_FIRST_CALL_TRANSACTION    (0x00000001)

/* Default binder devices */
#define GBINDER_DEFAULT_BINDER            "/dev/binder"
#define GBINDER_DEFAULT_HWBINDER          "/dev/hwbinder"

extern GLogModule GBINDER_LOG_MODULE;

G_END_DECLS

#endif /* GBINDER_TYPES_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
