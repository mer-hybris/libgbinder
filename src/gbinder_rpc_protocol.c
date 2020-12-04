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

#include "gbinder_rpc_protocol.h"
#include "gbinder_reader.h"
#include "gbinder_writer.h"
#include "gbinder_config.h"
#include "gbinder_log.h"

#define STRICT_MODE_PENALTY_GATHER (0x40 << 16)
#define BINDER_RPC_FLAGS (STRICT_MODE_PENALTY_GATHER)
#define UNSET_WORK_SOURCE (-1)

/*==========================================================================*
 * GBinderIpcProtocol callbacks (see Parcel::writeInterfaceToken in Android)
 * Note that there are two slightly different kinds of Parcels:
 *
 *   platform/system/libhwbinder/Parcel.cpp
 *   platform/frameworks/native/libs/binder/Parcel.cpp
 *
 * which mutate from version to version. Specific device => protocol
 * mapping can be optionally configured in /etc/gbinder.conf file.
 * The default protocol configuration looks like this:
 *
 *   [Protocol]
 *   Default = aidl
 *   /dev/binder = aidl
 *   /dev/hwbinder = hidl
 *
 *==========================================================================*/

#define CONF_GROUP GBINDER_CONFIG_GROUP_PROTOCOL
#define CONF_DEFAULT GBINDER_CONFIG_VALUE_DEFAULT

static GHashTable* gbinder_rpc_protocol_map = NULL;

/*
 * Default protocol for those binder devices which which haven't been
 * explicitely mapped.
 */
#define DEFAULT_PROTOCOL gbinder_rpc_protocol_aidl
static const GBinderRpcProtocol DEFAULT_PROTOCOL;
static const GBinderRpcProtocol* gbinder_rpc_protocol_default =
    &DEFAULT_PROTOCOL;

/*==========================================================================*
 * The original AIDL protocol.
 *==========================================================================*/

static
void
gbinder_rpc_protocol_aidl_write_ping(
    GBinderWriter* writer)
{
    /* No payload */
}

static
void
gbinder_rpc_protocol_aidl_write_rpc_header(
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
gbinder_rpc_protocol_aidl_read_rpc_header(
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

static const GBinderRpcProtocol gbinder_rpc_protocol_aidl = {
    .name = "aidl",
    .ping_tx = GBINDER_PING_TRANSACTION,
    .write_ping = gbinder_rpc_protocol_aidl_write_ping,
    .write_rpc_header = gbinder_rpc_protocol_aidl_write_rpc_header,
    .read_rpc_header = gbinder_rpc_protocol_aidl_read_rpc_header
};

/*==========================================================================*
 * AIDL protocol appeared in Android 10 (API level 29)
 *==========================================================================*/

static
void
gbinder_rpc_protocol_aidl2_write_rpc_header(
    GBinderWriter* writer,
    const char* iface)
{
    /*
     * writeInt32(IPCThreadState::self()->getStrictModePolicy() |
     *               STRICT_MODE_PENALTY_GATHER);
     * writeInt32(IPCThreadState::kUnsetWorkSource);
     * writeString16(interface);
     */
    gbinder_writer_append_int32(writer, BINDER_RPC_FLAGS);
    gbinder_writer_append_int32(writer, UNSET_WORK_SOURCE);
    gbinder_writer_append_string16(writer, iface);
}

static
const char*
gbinder_rpc_protocol_aidl2_read_rpc_header(
    GBinderReader* reader,
    guint32 txcode,
    char** iface)
{
    if (txcode > GBINDER_TRANSACTION(0,0,0)) {
        /* Internal transaction e.g. GBINDER_DUMP_TRANSACTION etc. */
        *iface = NULL;
    } else if (gbinder_reader_read_int32(reader, NULL) /* flags */ &&
        gbinder_reader_read_int32(reader, NULL) /* work source */) {
        *iface = gbinder_reader_read_string16(reader);
    } else {
        *iface = NULL;
    }
    return *iface;
}

static const GBinderRpcProtocol gbinder_rpc_protocol_aidl2 = {
    .name = "aidl2",
    .ping_tx = GBINDER_PING_TRANSACTION,
    .write_ping = gbinder_rpc_protocol_aidl_write_ping, /* no payload */
    .write_rpc_header = gbinder_rpc_protocol_aidl2_write_rpc_header,
    .read_rpc_header = gbinder_rpc_protocol_aidl2_read_rpc_header
};

/*==========================================================================*
 * The original /dev/hwbinder protocol.
 *==========================================================================*/

static
void
gbinder_rpc_protocol_hidl_write_rpc_header(
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
gbinder_rpc_protocol_hidl_write_ping(
    GBinderWriter* writer)
{
    gbinder_rpc_protocol_hidl_write_rpc_header(writer,
        "android.hidl.base@1.0::IBase");
}

static
const char*
gbinder_rpc_protocol_hidl_read_rpc_header(
    GBinderReader* reader,
    guint32 txcode,
    char** iface)
{
    *iface = NULL;
    return gbinder_reader_read_string8(reader);
}

static const GBinderRpcProtocol gbinder_rpc_protocol_hidl = {
    .name = "hidl",
    .ping_tx = HIDL_PING_TRANSACTION,
    .write_ping = gbinder_rpc_protocol_hidl_write_ping,
    .write_rpc_header = gbinder_rpc_protocol_hidl_write_rpc_header,
    .read_rpc_header = gbinder_rpc_protocol_hidl_read_rpc_header
};

/*==========================================================================*
 * Implementation
 *==========================================================================*/

/* All known protocols */
static const GBinderRpcProtocol* gbinder_rpc_protocol_list[] = {
    &gbinder_rpc_protocol_aidl,
    &gbinder_rpc_protocol_aidl2,
    &gbinder_rpc_protocol_hidl
};

static
const GBinderRpcProtocol*
gbinder_rpc_protocol_find(
    const char* name)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(gbinder_rpc_protocol_list); i++) {
        if (!g_ascii_strcasecmp(gbinder_rpc_protocol_list[i]->name, name)) {
            return gbinder_rpc_protocol_list[i];
        }
    }
    return NULL;
}

static
void
gbinder_rpc_protocol_map_add_default(
    GHashTable* map,
    const char* dev,
    const GBinderRpcProtocol* protocol)
{
    if (!g_hash_table_contains(map, dev)) {
        g_hash_table_insert(map, g_strdup(dev), (gpointer) protocol);
    }
}

static
gconstpointer
gbinder_rpc_protocol_value_map(
    const char* name)
{
    return gbinder_rpc_protocol_find(name);
}

static
GHashTable*
gbinder_rpc_protocol_load_config()
{
    GHashTable* map = gbinder_config_load(CONF_GROUP,
        gbinder_rpc_protocol_value_map);

    /* Add default configuration if it's not overridden */
    gbinder_rpc_protocol_map_add_default(map,
        GBINDER_DEFAULT_BINDER, &gbinder_rpc_protocol_aidl);
    gbinder_rpc_protocol_map_add_default(map,
        GBINDER_DEFAULT_HWBINDER, &gbinder_rpc_protocol_hidl);

    return map;
}

/* Runs at exit */
void
gbinder_rpc_protocol_exit()
{
    if (gbinder_rpc_protocol_map) {
        g_hash_table_destroy(gbinder_rpc_protocol_map);
        gbinder_rpc_protocol_map = NULL;
    }
    /* Reset the default too, mostly for unit testing */
    gbinder_rpc_protocol_default = &DEFAULT_PROTOCOL;
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

const GBinderRpcProtocol*
gbinder_rpc_protocol_for_device(
    const char* dev)
{
    if (dev) {
        const GBinderRpcProtocol* protocol;

        if (!gbinder_rpc_protocol_map) {
            const GBinderRpcProtocol* p;

            /* One-time initialization */
            gbinder_rpc_protocol_map = gbinder_rpc_protocol_load_config();

            /* "Default" is a special value stored in a special variable */
            p = g_hash_table_lookup(gbinder_rpc_protocol_map, CONF_DEFAULT);
            if (p) {
                g_hash_table_remove(gbinder_rpc_protocol_map, CONF_DEFAULT);
                gbinder_rpc_protocol_default = p;
            } else {
                gbinder_rpc_protocol_default = &DEFAULT_PROTOCOL;
            }
        }
        protocol = g_hash_table_lookup(gbinder_rpc_protocol_map, dev);
        if (protocol) {
            GDEBUG("Using %s protocol for %s", protocol->name, dev);
            return protocol;
        }
        GDEBUG("Using default protocol %s for %s",
            gbinder_rpc_protocol_default->name, dev);
    } else {
        GDEBUG("Using default protocol %s",
            gbinder_rpc_protocol_default->name);
    }
    return gbinder_rpc_protocol_default;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
