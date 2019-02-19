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

#ifndef GBINDER_REMOTE_OBJECT_H
#define GBINDER_REMOTE_OBJECT_H

#include "gbinder_types.h"

G_BEGIN_DECLS

typedef
void
(*GBinderRemoteObjectNotifyFunc)(
    GBinderRemoteObject* obj,
    void* user_data);

GBinderRemoteObject*
gbinder_remote_object_ref(
    GBinderRemoteObject* obj);

void
gbinder_remote_object_unref(
    GBinderRemoteObject* obj);

GBinderIpc*
gbinder_remote_object_ipc(
    GBinderRemoteObject* obj); /* Since 1.0.30 */

gboolean
gbinder_remote_object_is_dead(
    GBinderRemoteObject* obj);

gulong
gbinder_remote_object_add_death_handler(
    GBinderRemoteObject* obj,
    GBinderRemoteObjectNotifyFunc func,
    void* user_data);

void
gbinder_remote_object_remove_handler(
    GBinderRemoteObject* obj,
    gulong id);

G_END_DECLS

#endif /* GBINDER_REMOTE_OBJECT_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
