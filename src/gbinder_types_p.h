/*
 * Copyright (C) 2018-2022 Jolla Ltd.
 * Copyright (C) 2018-2022 Slava Monich <slava.monich@jolla.com>
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

#ifndef GBINDER_TYPES_PRIVATE_H
#define GBINDER_TYPES_PRIVATE_H

#include <gbinder_types.h>

typedef struct gbinder_buffer_contents GBinderBufferContents;
typedef struct gbinder_buffer_contents_list GBinderBufferContentsList;
typedef struct gbinder_cleanup GBinderCleanup;
typedef struct gbinder_driver GBinderDriver;
typedef struct gbinder_handler GBinderHandler;
typedef struct gbinder_io GBinderIo;
typedef struct gbinder_object_converter GBinderObjectConverter;
typedef struct gbinder_object_registry GBinderObjectRegistry;
typedef struct gbinder_output_data GBinderOutputData;
typedef struct gbinder_proxy_object GBinderProxyObject;
typedef struct gbinder_rpc_protocol GBinderRpcProtocol;
typedef struct gbinder_servicepoll GBinderServicePoll;
typedef struct gbinder_ipc_looper_tx GBinderIpcLooperTx;
typedef struct gbinder_ipc_sync_api GBinderIpcSyncApi;

#define GBINDER_INLINE_FUNC static inline
#define GBINDER_INTERNAL G_GNUC_INTERNAL
#define GBINDER_DESTRUCTOR __attribute__((destructor))

/* Internal transactions from frameworks/native/libs/binder/include/binder/IBinder.h */
#define GBINDER_PING_TRANSACTION            GBINDER_AIDL_TRANSACTION('P','N','G')
#define GBINDER_DUMP_TRANSACTION            GBINDER_AIDL_TRANSACTION('D','M','P')
#define GBINDER_SHELL_COMMAND_TRANSACTION   GBINDER_AIDL_TRANSACTION('C','M','D')
#define GBINDER_INTERFACE_TRANSACTION       GBINDER_AIDL_TRANSACTION('N','T','F')
#define GBINDER_SYSPROPS_TRANSACTION        GBINDER_AIDL_TRANSACTION('S','P','R')

/* platform/system/tools/hidl/Interface.cpp */
#define HIDL_PING_TRANSACTION                     GBINDER_HIDL_TRANSACTION('P','N','G')
#define HIDL_DESCRIPTOR_CHAIN_TRANSACTION         GBINDER_HIDL_TRANSACTION('C','H','N')
#define HIDL_GET_DESCRIPTOR_TRANSACTION           GBINDER_HIDL_TRANSACTION('D','S','C')
#define HIDL_SYSPROPS_CHANGED_TRANSACTION         GBINDER_HIDL_TRANSACTION('S','Y','S')
#define HIDL_LINK_TO_DEATH_TRANSACTION            GBINDER_HIDL_TRANSACTION('L','T','D')
#define HIDL_UNLINK_TO_DEATH_TRANSACTION          GBINDER_HIDL_TRANSACTION('U','T','D')
#define HIDL_SET_HAL_INSTRUMENTATION_TRANSACTION  GBINDER_HIDL_TRANSACTION('I','N','T')
#define HIDL_GET_REF_INFO_TRANSACTION             GBINDER_HIDL_TRANSACTION('R','E','F')
#define HIDL_DEBUG_TRANSACTION                    GBINDER_HIDL_TRANSACTION('D','B','G')
#define HIDL_HASH_CHAIN_TRANSACTION               GBINDER_HIDL_TRANSACTION('H','S','H')

/* As a special case, ServiceManager's handle is zero */
#define GBINDER_SERVICEMANAGER_HANDLE (0)

typedef enum gbinder_stability_level {
    GBINDER_STABILITY_UNDECLARED = 0,
    GBINDER_STABILITY_VENDOR = 0x03,
    GBINDER_STABILITY_SYSTEM = 0x0c,
    GBINDER_STABILITY_VINTF = 0x3f
} GBINDER_STABILITY_LEVEL;

#endif /* GBINDER_TYPES_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
