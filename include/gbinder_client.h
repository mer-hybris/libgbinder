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

#ifndef GBINDER_CLIENT_H
#define GBINDER_CLIENT_H

#include <gbinder_types.h>

G_BEGIN_DECLS

typedef struct gbinder_client_iface_info {
    const char* iface;
    guint32 last_code;
} GBinderClientIfaceInfo;

typedef
void
(*GBinderClientReplyFunc)(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data);

GBinderClient*
gbinder_client_new(
    GBinderRemoteObject* object,
    const char* iface);

GBinderClient*
gbinder_client_new2(
    GBinderRemoteObject* object,
    const GBinderClientIfaceInfo* ifaces,
    gsize count); /* since 1.0.42 */

GBinderClient*
gbinder_client_ref(
    GBinderClient* client);

void
gbinder_client_unref(
    GBinderClient* client);

const char*
gbinder_client_interface(
    GBinderClient* client); /* since 1.0.22 */

const char*
gbinder_client_interface2(
    GBinderClient* client,
    guint32 code); /* since 1.0.42 */

GBinderLocalRequest*
gbinder_client_new_request(
    GBinderClient* client);

GBinderLocalRequest*
gbinder_client_new_request2(
    GBinderClient* client,
    guint32 code); /* since 1.0.42 */

GBinderRemoteReply*
gbinder_client_transact_sync_reply(
    GBinderClient* client,
    guint32 code,
    GBinderLocalRequest* req,
    int* status);

int
gbinder_client_transact_sync_oneway(
    GBinderClient* client,
    guint32 code,
    GBinderLocalRequest* req);

gulong
gbinder_client_transact(
    GBinderClient* client,
    guint32 code,
    guint32 flags,
    GBinderLocalRequest* req,
    GBinderClientReplyFunc reply,
    GDestroyNotify destroy,
    void* user_data);

void
gbinder_client_cancel(
    GBinderClient* client,
    gulong id);

G_END_DECLS

#endif /* GBINDER_CLIENT_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
