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

#ifndef GBINDER_TYPES_PRIVATE_H
#define GBINDER_TYPES_PRIVATE_H

#include <gbinder_types.h>

typedef struct gbinder_buffer_contents GBinderBufferContents;
typedef struct gbinder_cleanup GBinderCleanup;
typedef struct gbinder_driver GBinderDriver;
typedef struct gbinder_handler GBinderHandler;
typedef struct gbinder_io GBinderIo;
typedef struct gbinder_object_registry GBinderObjectRegistry;
typedef struct gbinder_output_data GBinderOutputData;
typedef struct gbinder_rpc_protocol GBinderRpcProtocol;
typedef struct gbinder_servicepoll GBinderServicePoll;
typedef struct gbinder_ipc_looper_tx GBinderIpcLooperTx;

#define GBINDER_INLINE_FUNC static inline
#define GBINDER_INTERNAL G_GNUC_INTERNAL
#define GBINDER_DESTRUCTOR __attribute__((destructor))

#define GBINDER_TRANSACTION(c2,c3,c4)     GBINDER_FOURCC('_',c2,c3,c4)
#define GBINDER_PING_TRANSACTION          GBINDER_TRANSACTION('P','N','G')
#define GBINDER_DUMP_TRANSACTION          GBINDER_TRANSACTION('D','M','P')
#define GBINDER_SHELL_COMMAND_TRANSACTION GBINDER_TRANSACTION('C','M','D')
#define GBINDER_INTERFACE_TRANSACTION     GBINDER_TRANSACTION('N','T','F')
#define GBINDER_SYSPROPS_TRANSACTION      GBINDER_TRANSACTION('S','P','R')

/* platform/system/tools/hidl/Interface.cpp */
#define HIDL_FOURCC(c2,c3,c4)                     GBINDER_FOURCC(0x0f,c2,c3,c4)
#define HIDL_PING_TRANSACTION                     HIDL_FOURCC('P','N','G')
#define HIDL_DESCRIPTOR_CHAIN_TRANSACTION         HIDL_FOURCC('C','H','N')
#define HIDL_GET_DESCRIPTOR_TRANSACTION           HIDL_FOURCC('D','S','C')
#define HIDL_SYSPROPS_CHANGED_TRANSACTION         HIDL_FOURCC('S','Y','S')
#define HIDL_LINK_TO_DEATH_TRANSACTION            HIDL_FOURCC('L','T','D')
#define HIDL_UNLINK_TO_DEATH_TRANSACTION          HIDL_FOURCC('U','T','D')
#define HIDL_SET_HAL_INSTRUMENTATION_TRANSACTION  HIDL_FOURCC('I','N','T')
#define HIDL_GET_REF_INFO_TRANSACTION             HIDL_FOURCC('R','E','F')
#define HIDL_DEBUG_TRANSACTION                    HIDL_FOURCC('D','B','G')
#define HIDL_HASH_CHAIN_TRANSACTION               HIDL_FOURCC('H','S','H')

#endif /* GBINDER_TYPES_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
