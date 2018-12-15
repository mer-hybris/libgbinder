/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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

#ifndef GBINDER_REMOTE_REQUEST_H
#define GBINDER_REMOTE_REQUEST_H

#include <gbinder_reader.h>

G_BEGIN_DECLS

const char*
gbinder_remote_request_interface(
    GBinderRemoteRequest* req);

GBinderRemoteRequest*
gbinder_remote_request_ref(
    GBinderRemoteRequest* req);

void
gbinder_remote_request_unref(
    GBinderRemoteRequest* req);

void
gbinder_remote_request_init_reader(
    GBinderRemoteRequest* req,
    GBinderReader* reader);

pid_t
gbinder_remote_request_sender_pid(
    GBinderRemoteRequest* req); /* since 1.0.2 */

uid_t
gbinder_remote_request_sender_euid(
    GBinderRemoteRequest* req); /* since 1.0.2 */

GBinderLocalRequest*
gbinder_remote_request_copy_to_local(
    GBinderRemoteRequest* req) /* since 1.0.6 */
    G_GNUC_WARN_UNUSED_RESULT;

void
gbinder_remote_request_block(
    GBinderRemoteRequest* req); /* Since 1.0.20 */

void
gbinder_remote_request_complete(
    GBinderRemoteRequest* req,
    GBinderLocalReply* reply,
    int status); /* Since 1.0.20 */

/* Convenience function to decode requests with just one data item */

gboolean
gbinder_remote_request_read_int32(
    GBinderRemoteRequest* req,
    gint32* value);

gboolean
gbinder_remote_request_read_uint32(
    GBinderRemoteRequest* req,
    guint32* value);

gboolean
gbinder_remote_request_read_int64(
    GBinderRemoteRequest* req,
    gint64* value);

gboolean
gbinder_remote_request_read_uint64(
    GBinderRemoteRequest* req,
    guint64* value);

const char*
gbinder_remote_request_read_string8(
    GBinderRemoteRequest* req);

char*
gbinder_remote_request_read_string16(
    GBinderRemoteRequest* req)
    G_GNUC_WARN_UNUSED_RESULT;

GBinderRemoteObject*
gbinder_remote_request_read_object(
    GBinderRemoteRequest* self)
    G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* GBINDER_REMOTE_REQUEST_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
