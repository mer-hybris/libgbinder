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

#ifndef GBINDER_RPC_PROTOCOL_H
#define GBINDER_RPC_PROTOCOL_H

#include "gbinder_types_p.h"

/*
 * There are several versions of binder RPC protocol with diffferent
 * transaction headers and transaction codes.
 */

struct gbinder_rpc_protocol {
    const char* name;
    guint32 ping_tx;
    void (*write_ping)(GBinderWriter* writer);
    void (*write_rpc_header)(GBinderWriter* writer, const char* iface);
    const char* (*read_rpc_header)(GBinderReader* reader, guint32 txcode,
        char** iface);

    /*
     * For the sake of simplicity, let's assume that the trailer has a
     * fixed size and that size is the same on both 32 and 64 bit platforms.
     * Also note that finish_unflatten_binder() is only invoked for the
     * remote objects that are not NULL, otherwise flat_binder_object_extra
     * bytes are just skipped.
     */
    gsize flat_binder_object_extra;
    void (*finish_flatten_binder)(void* out, GBinderLocalObject* obj);
    void (*finish_unflatten_binder)(const void* in, GBinderRemoteObject* obj);
};

const GBinderRpcProtocol*
gbinder_rpc_protocol_by_name(
    const char* protocol_name)
    GBINDER_INTERNAL;

/* Returns one of the above based on the device name */
const GBinderRpcProtocol*
gbinder_rpc_protocol_for_device(
    const char* dev)
    GBINDER_INTERNAL;

/* Runs at exit, declared here strictly for unit tests */
void
gbinder_rpc_protocol_exit(
    void)
    GBINDER_DESTRUCTOR
    GBINDER_INTERNAL;

#endif /* GBINDER_RPC_PROTOCOL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
