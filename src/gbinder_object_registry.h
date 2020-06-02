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

#ifndef GBINDER_OBJECT_REGISTRY_H
#define GBINDER_OBJECT_REGISTRY_H

#include "gbinder_types_p.h"

typedef struct gbinder_object_registry_functions {
    void (*ref)(GBinderObjectRegistry* reg);
    void (*unref)(GBinderObjectRegistry* reg);
    GBinderLocalObject* (*get_local)(GBinderObjectRegistry* reg,
        void* pointer);
    GBinderRemoteObject* (*get_remote)(GBinderObjectRegistry* reg,
        guint32 handle);
} GBinderObjectRegistryFunctions;

struct gbinder_object_registry {
    const GBinderObjectRegistryFunctions* f;
    const GBinderIo* io;
};

/* Inline wrappers */

GBINDER_INLINE_FUNC
GBinderObjectRegistry*
gbinder_object_registry_ref(
    GBinderObjectRegistry* reg)
{
    if (reg) reg->f->ref(reg);
    return reg;
}

GBINDER_INLINE_FUNC
void
gbinder_object_registry_unref(
    GBinderObjectRegistry* reg)
{
    if (reg) reg->f->unref(reg);
}

GBINDER_INLINE_FUNC
GBinderLocalObject*
gbinder_object_registry_get_local(
    GBinderObjectRegistry* reg,
    void* pointer)
{
    return reg ? reg->f->get_local(reg, pointer) : NULL;
}

GBINDER_INLINE_FUNC
GBinderRemoteObject*
gbinder_object_registry_get_remote(
    GBinderObjectRegistry* reg,
    guint32 handle)
{
    return reg ? reg->f->get_remote(reg, handle) : NULL;
}

#endif /* GBINDER_OBJECT_REGISTRY_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
