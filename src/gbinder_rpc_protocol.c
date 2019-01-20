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

#include "gbinder_rpc_protocol.h"
#include "gbinder_reader.h"
#include "gbinder_writer.h"

/*==========================================================================*
 * GBinderIpcProtocol callbacks (see Parcel::writeInterfaceToken in Android)
 * Note that there are two slightly different kinds of Parcels:
 *
 *   platform/system/libhwbinder/Parcel.cpp
 *   platform/frameworks/native/libs/binder/Parcel.cpp
 *==========================================================================*/

/*==========================================================================*
 * /dev/binder
 *==========================================================================*/

/* No idea what that is... */
#define STRICT_MODE_PENALTY_GATHER (0x40 << 16)
#define BINDER_RPC_FLAGS (STRICT_MODE_PENALTY_GATHER)

static
void
gbinder_rpc_protocol_binder_write_ping(
    GBinderWriter* writer)
{
    /* No payload */
}

static
void
gbinder_rpc_protocol_binder_write_rpc_header(
    GBinderWriter* writer,
    const char* iface)
{
    /*
     * writeInt32(IPCThreadState::self()->getStrictModePolicy() |
     *               STRICT_MODE_PENALTY_GATHER);
     * writeString16(interface);
     */
    gbinder_writer_append_int32(writer, BINDER_RPC_FLAGS);
    gbinder_writer_append_string16(writer, iface);
}

static
const char*
gbinder_rpc_protocol_binder_read_rpc_header(
    GBinderReader* reader,
    guint32 txcode,
    char** iface)
{
    if (txcode > GBINDER_TRANSACTION(0,0,0)) {
        /* Internal transaction e.g. GBINDER_DUMP_TRANSACTION etc. */
        *iface = NULL;
    } else if (gbinder_reader_read_int32(reader, NULL)) {
        *iface = gbinder_reader_read_string16(reader);
    } else {
        *iface = NULL;
    }
    return *iface;
}

/*==========================================================================*
 * /dev/hwbinder
 *==========================================================================*/

static
void
gbinder_rpc_protocol_hwbinder_write_rpc_header(
    GBinderWriter* writer,
    const char* iface)
{
    /*
     * writeCString(interface);
     */
    gbinder_writer_append_string8(writer, iface);
}

static
void
gbinder_rpc_protocol_hwbinder_write_ping(
    GBinderWriter* writer)
{
    gbinder_rpc_protocol_hwbinder_write_rpc_header(writer,
        "android.hidl.base@1.0::IBase");
}

static
const char*
gbinder_rpc_protocol_hwbinder_read_rpc_header(
    GBinderReader* reader,
    guint32 txcode,
    char** iface)
{
    *iface = NULL;
    return gbinder_reader_read_string8(reader);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

const GBinderRpcProtocol gbinder_rpc_protocol_binder = {
    .ping_tx = GBINDER_PING_TRANSACTION,
    .write_ping = gbinder_rpc_protocol_binder_write_ping,
    .write_rpc_header = gbinder_rpc_protocol_binder_write_rpc_header,
    .read_rpc_header = gbinder_rpc_protocol_binder_read_rpc_header
};

const GBinderRpcProtocol gbinder_rpc_protocol_hwbinder = {
    .ping_tx = HIDL_PING_TRANSACTION,
    .write_ping = gbinder_rpc_protocol_hwbinder_write_ping,
    .write_rpc_header = gbinder_rpc_protocol_hwbinder_write_rpc_header,
    .read_rpc_header = gbinder_rpc_protocol_hwbinder_read_rpc_header
};

const GBinderRpcProtocol*
gbinder_rpc_protocol_for_device(
    const char* dev)
{
    return (dev && !strcmp(dev, GBINDER_DEFAULT_HWBINDER)) ?
        &gbinder_rpc_protocol_hwbinder : &gbinder_rpc_protocol_binder;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
