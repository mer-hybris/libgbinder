/*
 * Copyright (C) 2021 Jolla Ltd.
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

#ifndef GBINDER_FMQ_PRIVATE_H
#define GBINDER_FMQ_PRIVATE_H

#include <gbinder_fmq.h>

#include "gbinder_types_p.h"

/* FMQ functionality requires __NR_memfd_create syscall */
#include <sys/syscall.h>

#ifdef __NR_memfd_create
#  define GBINDER_FMQ_SUPPORTED 1
#else
#  define GBINDER_FMQ_SUPPORTED 0
#endif

/*
 * From linux/memfd.h
 */
#ifndef MFD_CLOEXEC
#  define MFD_CLOEXEC 0x0001U
#endif

/*
 * FMQ types
 */
typedef struct gbinder_fmq_grantor_descriptor {
    guint32 flags GBINDER_ALIGNED(4);
    guint32 fd_index GBINDER_ALIGNED(4);
    guint32 offset GBINDER_ALIGNED(4);
    guint64 extent GBINDER_ALIGNED(8);
} GBinderFmqGrantorDescriptor;

G_STATIC_ASSERT(G_STRUCT_OFFSET(GBinderFmqGrantorDescriptor, flags) == 0);
G_STATIC_ASSERT(G_STRUCT_OFFSET(GBinderFmqGrantorDescriptor, fd_index) == 4);
G_STATIC_ASSERT(G_STRUCT_OFFSET(GBinderFmqGrantorDescriptor, offset) == 8);
G_STATIC_ASSERT(G_STRUCT_OFFSET(GBinderFmqGrantorDescriptor, extent) == 16);
G_STATIC_ASSERT(sizeof(GBinderFmqGrantorDescriptor) == 24);

typedef struct gbinder_mq_descriptor {
    GBinderHidlVec grantors;
    union {
        guint64 value;
        const GBinderFds* fds;
    } data;
    guint32 quantum;
    guint32 flags;
} GBinderMQDescriptor;

#define GBINDER_MQ_DESCRIPTOR_GRANTORS_OFFSET (0)
#define GBINDER_MQ_DESCRIPTOR_FDS_OFFSET (16)
G_STATIC_ASSERT(G_STRUCT_OFFSET(GBinderMQDescriptor, grantors) ==
    GBINDER_MQ_DESCRIPTOR_GRANTORS_OFFSET);
G_STATIC_ASSERT(G_STRUCT_OFFSET(GBinderMQDescriptor, data) ==
    GBINDER_MQ_DESCRIPTOR_FDS_OFFSET);
G_STATIC_ASSERT(sizeof(GBinderMQDescriptor) == 32);

GBinderMQDescriptor*
gbinder_fmq_get_descriptor(
    const GBinderFmq* self)
    GBINDER_INTERNAL;

#endif /* GBINDER_FMQ_PRIVATE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
