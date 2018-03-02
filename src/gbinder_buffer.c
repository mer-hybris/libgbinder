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

#include "gbinder_buffer_p.h"
#include "gbinder_driver.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

typedef struct gbinder_buffer_memory {
    gint refcount;
    void* buffer;
    gsize size;
    GBinderDriver* driver;
} GBinderBufferMemory;

typedef struct gbinder_buffer_priv {
    GBinderBuffer pub;
    GBinderBufferMemory* memory;
} GBinderBufferPriv;

static inline GBinderBufferPriv* gbinder_buffer_cast(GBinderBuffer* buf)
    { return G_CAST(buf, GBinderBufferPriv, pub); }

/*==========================================================================*
 * GBinderBufferMemory
 *==========================================================================*/

static
GBinderBufferMemory*
gbinder_buffer_memory_new(
    GBinderDriver* driver,
    void* buffer,
    gsize size)
{
    GBinderBufferMemory* self = g_slice_new0(GBinderBufferMemory);

    g_atomic_int_set(&self->refcount, 1);
    self->buffer = buffer;
    self->size = size;
    self->driver = gbinder_driver_ref(driver);
    return self;
}

static
void
gbinder_buffer_memory_free(
    GBinderBufferMemory* self)
{
    gbinder_driver_free_buffer(self->driver, self->buffer);
    gbinder_driver_unref(self->driver);
    g_slice_free(GBinderBufferMemory, self);
}

static
GBinderBufferMemory*
gbinder_buffer_memory_ref(
    GBinderBufferMemory* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

static
void
gbinder_buffer_memory_unref(
    GBinderBufferMemory* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            gbinder_buffer_memory_free(self);
        }
    }
}

/*==========================================================================*
 * GBinderBuffer
 *==========================================================================*/

static
GBinderBuffer*
gbinder_buffer_alloc(
    GBinderBufferMemory* memory,
    void* data,
    gsize size)
{
    GBinderBufferPriv* priv = g_slice_new0(GBinderBufferPriv);
    GBinderBuffer* self = &priv->pub;

    priv->memory = memory;
    self->data = data;
    self->size = size;
    return self;
}

void
gbinder_buffer_free(
    GBinderBuffer* self)
{
    if (G_LIKELY(self)) {
        GBinderBufferPriv* priv = gbinder_buffer_cast(self);

        gbinder_buffer_memory_unref(priv->memory);
        g_slice_free(GBinderBufferPriv, priv);
    }
}

GBinderBuffer*
gbinder_buffer_new(
    GBinderDriver* driver,
    void* data,
    gsize size)
{
    return gbinder_buffer_alloc((driver && data) ?
        gbinder_buffer_memory_new(driver, data, size) : NULL, data, size);
}

GBinderBuffer*
gbinder_buffer_new_with_parent(
    GBinderBuffer* parent,
    void* data,
    gsize size)
{
    return gbinder_buffer_alloc(parent ?
        gbinder_buffer_memory_ref(gbinder_buffer_cast(parent)->memory) : NULL,
        data, size);
}

GBinderDriver*
gbinder_buffer_driver(
    GBinderBuffer* self)
{
    if (G_LIKELY(self)) {
        GBinderBufferPriv* priv = gbinder_buffer_cast(self);

        if (priv->memory) {
            return priv->memory->driver;
        }
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
