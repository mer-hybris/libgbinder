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
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
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

#ifndef GBINDER_LOCAL_REQUEST_H
#define GBINDER_LOCAL_REQUEST_H

#include "gbinder_types.h"

G_BEGIN_DECLS

GBinderLocalRequest*
gbinder_local_request_ref(
    GBinderLocalRequest* request);

void
gbinder_local_request_unref(
    GBinderLocalRequest* request);

void
gbinder_local_request_init_writer(
    GBinderLocalRequest* request,
    GBinderWriter* writer);

void
gbinder_local_request_cleanup(
    GBinderLocalRequest* request,
    GDestroyNotify destroy,
    gpointer pointer);

GBinderLocalRequest*
gbinder_local_request_append_bool(
    GBinderLocalRequest* request,
    gboolean value); /* since 1.0.3 */

GBinderLocalRequest*
gbinder_local_request_append_int32(
    GBinderLocalRequest* request,
    guint32 value);

GBinderLocalRequest*
gbinder_local_request_append_int64(
    GBinderLocalRequest* request,
    guint64 value);

GBinderLocalRequest*
gbinder_local_request_append_float(
    GBinderLocalRequest* request,
    gfloat value);

GBinderLocalRequest*
gbinder_local_request_append_double(
    GBinderLocalRequest* request,
    gdouble value);

GBinderLocalRequest*
gbinder_local_request_append_string8(
    GBinderLocalRequest* request,
    const char* str);

GBinderLocalRequest*
gbinder_local_request_append_string16(
    GBinderLocalRequest* request,
    const char* utf8);

GBinderLocalRequest*
gbinder_local_request_append_hidl_string(
    GBinderLocalRequest* request,
    const char* str);

GBinderLocalRequest*
gbinder_local_request_append_hidl_string_vec(
    GBinderLocalRequest* request,
    const char* strv[],
    gssize count);

GBinderLocalRequest*
gbinder_local_request_append_local_object(
    GBinderLocalRequest* request,
    GBinderLocalObject* obj);

GBinderLocalRequest*
gbinder_local_request_append_remote_object(
    GBinderLocalRequest* request,
    GBinderRemoteObject* obj);

G_END_DECLS

#endif /* GBINDER_LOCAL_OBJECT_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
