/*
 * Copyright (C) 2018-2026 Slava Monich <slava@monich.com>
 * Copyright (C) 2025 Jolla Mobile Ltd.
 * Copyright (C) 2018-2022 Jolla Ltd.
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

#include "gbinder_fmq_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_reader.h"
#include "gbinder_writer_p.h"
#include "gbinder_config.h"
#include "gbinder_log.h"
#include "gbinder_local_object_p.h"
#include "gbinder_remote_object_p.h"

#include <gutil_misc.h>

#include <string.h>

#define STRICT_MODE_PENALTY_GATHER (0x40 << 16)
#define BINDER_RPC_FLAGS (STRICT_MODE_PENALTY_GATHER)
#define UNSET_WORK_SOURCE (-1)

#define BINDER_VND_HEADER GBINDER_FOURCC('V', 'N', 'D', 'R')
#define BINDER_SYS_HEADER GBINDER_FOURCC('S', 'Y', 'S', 'T')

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
 * Common AIDL protocol
 *==========================================================================*/

static
void
gbinder_rpc_protocol_aidl_write_fmq_grantor_descriptor(
    GBinderWriter* writer,
    const GBinderFmqGrantorDescriptor* grantor)
{
    GBinderWriter parcelable;

    /*
     * package android.hardware.common.fmq;
     * parcelable GrantorDescriptor {
     *   int fdIndex;
     *   int offset;
     *   long extent;
     * }
     */
    gbinder_writer_start_parcelable(writer, &parcelable);
    gbinder_writer_append_int32(&parcelable, grantor->fd_index);
    gbinder_writer_append_int32(&parcelable, grantor->offset);
    gbinder_writer_append_int64(&parcelable, grantor->extent);
    gbinder_writer_finish_parcelable(&parcelable);
}

static
void
gbinder_rpc_protocol_aidl_write_fds(
    GBinderWriter* writer,
    const GBinderFds* fds)
{
    GBinderWriter parcelable;
    guint i;

    /*
     * package android.hardware.common;
     * parcelable NativeHandle {
     *   ParcelFileDescriptor[] fds;
     *   int[] ints;
     * }
     */
    gbinder_writer_start_parcelable(writer, &parcelable);

    /* fds */
    gbinder_writer_append_int32(&parcelable, fds->num_fds);
    for (i = 0; i < fds->num_fds; i++) {
        gbinder_writer_append_int32(&parcelable, 1);
        gbinder_writer_append_int32(&parcelable, 0);
        gbinder_writer_append_fd(&parcelable, gbinder_fds_get_fd(fds, i));
    }

    /* ints */
    gbinder_writer_append_int32(&parcelable, fds->num_ints);
    for (i = 0; i < fds->num_ints; i++) {
        gbinder_writer_append_int32(&parcelable,
            gbinder_fds_get_fd(fds, fds->num_fds + i));
    }
    gbinder_writer_finish_parcelable(&parcelable);
}

static
void
gbinder_rpc_protocol_aidl_write_fmq_descriptor(
    GBinderWriter* writer,
    const GBinderFmq* queue)
{
    if (queue) {
        const GBinderMQDescriptor* desc = gbinder_fmq_get_descriptor(queue);
        const GBinderFmqGrantorDescriptor* grantors = desc->grantors.data.ptr;
        GBinderWriter parcelable;
        guint i;

        /*
         * package android.hardware.common.fmq;
         * parcelable MQDescriptor<T, Flavor> {
         *   android.hardware.common.fmq.GrantorDescriptor[] grantors;
         *   android.hardware.common.NativeHandle handle;
         *   int quantum;
         *   int flags;
         * }
         */
        gbinder_writer_start_parcelable(writer, &parcelable);

        /* grantors */
        gbinder_writer_append_int32(&parcelable, desc->grantors.count);
        for (i = 0; i < desc->grantors.count; i++) {
            gbinder_rpc_protocol_aidl_write_fmq_grantor_descriptor
                (&parcelable, grantors + i);
        }

        gbinder_rpc_protocol_aidl_write_fds(&parcelable, desc->data.fds);
        gbinder_writer_append_int32(&parcelable, desc->quantum);
        gbinder_writer_append_int32(&parcelable, desc->flags);
        gbinder_writer_finish_parcelable(&parcelable);
    } else {
        gbinder_writer_append_null_parcelable(writer);
    }
}

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
    .read_rpc_header = gbinder_rpc_protocol_aidl_read_rpc_header,
    .write_fmq_descriptor = gbinder_rpc_protocol_aidl_write_fmq_descriptor,
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
    .read_rpc_header = gbinder_rpc_protocol_aidl2_read_rpc_header,
    .write_fmq_descriptor = gbinder_rpc_protocol_aidl_write_fmq_descriptor,
};

/*==========================================================================*
 * AIDL protocol appeared in Android 11 (API level 30)
 *==========================================================================*/

static
void
gbinder_rpc_protocol_aidl3_write_rpc_header(
    GBinderWriter* writer,
    const char* iface)
{
    gbinder_writer_append_int32(writer, BINDER_RPC_FLAGS);
    gbinder_writer_append_int32(writer, UNSET_WORK_SOURCE);
    gbinder_writer_append_int32(writer, BINDER_SYS_HEADER);
    gbinder_writer_append_string16(writer, iface);
}

static
const char*
gbinder_rpc_protocol_aidl3_read_rpc_header(
    GBinderReader* reader,
    guint32 txcode,
    char** iface)
{
    if (txcode > GBINDER_TRANSACTION(0,0,0)) {
        *iface = NULL;
    } else if (gbinder_reader_read_int32(reader, NULL) /* flags */ &&
        gbinder_reader_read_int32(reader, NULL) /* work source */ &&
        gbinder_reader_read_int32(reader, NULL) /* sys header */) {
        *iface = gbinder_reader_read_string16(reader);
    } else {
        *iface = NULL;
    }

    return *iface;
}

static
void
gbinder_rpc_protocol_aidl3_finish_flatten_binder(
    void* out,
    GBinderLocalObject* obj)
{
    if (G_LIKELY(obj)) {
        *(guint32*)out = obj->stability;
    } else {
        *(guint32*)out = GBINDER_STABILITY_UNDECLARED;
    }
}

static
void
gbinder_rpc_protocol_aidl3_finish_unflatten_binder(
    const void* in,
    GBinderRemoteObject* obj)
{
    guint32 stability;

    memcpy(&stability, in, sizeof(stability));
    obj->stability = stability;
}

static const GBinderRpcProtocol gbinder_rpc_protocol_aidl3 = {
    .name = "aidl3",
    .ping_tx = GBINDER_PING_TRANSACTION,
    .write_ping = gbinder_rpc_protocol_aidl_write_ping, /* no payload */
    .write_rpc_header = gbinder_rpc_protocol_aidl3_write_rpc_header,
    .read_rpc_header = gbinder_rpc_protocol_aidl3_read_rpc_header,
    .flat_binder_object_extra = 4,
    .finish_flatten_binder = gbinder_rpc_protocol_aidl3_finish_flatten_binder,
    .finish_unflatten_binder =
        gbinder_rpc_protocol_aidl3_finish_unflatten_binder,
    .write_fmq_descriptor = gbinder_rpc_protocol_aidl_write_fmq_descriptor,
};

/*==========================================================================*
 * AIDL protocol appeared in Android 12 (API level 31), but reverted in
 * Android 13 (API level 33).
 *==========================================================================*/

#define BINDER_WIRE_FORMAT_VERSION_AIDL4 1
struct stability_category {
    guint8 binder_wire_format_version;
    guint8 reserved[2];
    guint8 stability_level;
};
G_STATIC_ASSERT(sizeof(struct stability_category) == sizeof(guint32));

static
void
gbinder_rpc_protocol_aidl4_finish_flatten_binder(
    void* out,
    GBinderLocalObject* obj)
{
    struct stability_category cat = {
        .binder_wire_format_version = BINDER_WIRE_FORMAT_VERSION_AIDL4,
        .reserved = { 0, 0, },
        .stability_level = obj ? obj->stability : GBINDER_STABILITY_UNDECLARED,
    };

    memcpy(out, &cat, sizeof(cat));
}

static
void
gbinder_rpc_protocol_aidl4_finish_unflatten_binder(
    const void* in,
    GBinderRemoteObject* obj)
{
    struct stability_category cat;

    memcpy(&cat, in, sizeof(cat));
    obj->stability = cat.stability_level;
}

static const GBinderRpcProtocol gbinder_rpc_protocol_aidl4 = {
    .name = "aidl4",
    .ping_tx = GBINDER_PING_TRANSACTION,
    .write_ping = gbinder_rpc_protocol_aidl_write_ping, /* no payload */
    .write_rpc_header = gbinder_rpc_protocol_aidl3_write_rpc_header,
    .read_rpc_header = gbinder_rpc_protocol_aidl3_read_rpc_header,
    .flat_binder_object_extra = 4,
    .finish_flatten_binder = gbinder_rpc_protocol_aidl4_finish_flatten_binder,
    .finish_unflatten_binder =
        gbinder_rpc_protocol_aidl4_finish_unflatten_binder,
    .write_fmq_descriptor = gbinder_rpc_protocol_aidl_write_fmq_descriptor,
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

void
gbinder_rpc_protocol_hidl_write_fmq_descriptor(
    GBinderWriter* writer,
    const GBinderFmq* queue)
{
    GBinderParent parent;
    GBinderMQDescriptor* desc = gbinder_fmq_get_descriptor(queue);
    GBinderMQDescriptor* mqdesc = gutil_memdup(desc,
        sizeof(GBinderMQDescriptor));

    const gsize vec_total =
        desc->grantors.count * sizeof(GBinderFmqGrantorDescriptor);
    void* vec_buf = gutil_memdup(desc->grantors.data.ptr, vec_total);

    const gsize fds_total = sizeof(GBinderFds) +
        sizeof(int) * (desc->data.fds->num_fds + desc->data.fds->num_ints);
    GBinderFds* fds = gutil_memdup(desc->data.fds, fds_total);

    mqdesc->data.fds = fds;
    gbinder_writer_add_cleanup(writer, g_free, mqdesc);
    gbinder_writer_add_cleanup(writer, g_free, fds);

    /* Fill in the grantor vector descriptor */
    if (vec_buf) {
        mqdesc->grantors.count = desc->grantors.count;
        mqdesc->grantors.data.ptr = vec_buf;
        mqdesc->grantors.owns_buffer = TRUE;
        gbinder_writer_add_cleanup(writer, g_free, vec_buf);
    }

    /* Write the FMQ descriptor object */
    parent.index = gbinder_writer_append_buffer_object(writer,
        mqdesc, sizeof(*mqdesc));

    /* Write the vector data buffer */
    parent.offset = GBINDER_MQ_DESCRIPTOR_GRANTORS_OFFSET;
    gbinder_writer_append_buffer_object_with_parent(writer, vec_buf, vec_total,
        &parent);

    /* Write the fds */
    parent.offset = GBINDER_MQ_DESCRIPTOR_FDS_OFFSET;
    gbinder_writer_append_fds(writer, mqdesc->data.fds, &parent);
}

static const GBinderRpcProtocol gbinder_rpc_protocol_hidl = {
    .name = "hidl",
    .ping_tx = HIDL_PING_TRANSACTION,
    .write_ping = gbinder_rpc_protocol_hidl_write_ping,
    .write_rpc_header = gbinder_rpc_protocol_hidl_write_rpc_header,
    .read_rpc_header = gbinder_rpc_protocol_hidl_read_rpc_header,
    .write_fmq_descriptor = gbinder_rpc_protocol_hidl_write_fmq_descriptor,
};

/*==========================================================================*
 * Implementation
 *==========================================================================*/

/* All known protocols */
static const GBinderRpcProtocol* gbinder_rpc_protocol_list[] = {
    &gbinder_rpc_protocol_aidl,
    &gbinder_rpc_protocol_aidl2,
    &gbinder_rpc_protocol_aidl3,
    &gbinder_rpc_protocol_aidl4,
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

const GBinderRpcProtocol*
gbinder_rpc_protocol_by_name(
    const char* protocol_name)
{
    return gbinder_rpc_protocol_find(protocol_name);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
