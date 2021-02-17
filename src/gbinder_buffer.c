/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_buffer_p.h"
#include "gbinder_driver.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

struct gbinder_buffer_contents {
    gint refcount;
    void* buffer;
    gsize size;
    void** objects;
    GBinderDriver* driver;
};

typedef struct gbinder_buffer_priv {
    GBinderBuffer pub;
    GBinderBufferContents* contents;
} GBinderBufferPriv;

static inline GBinderBufferPriv* gbinder_buffer_cast(GBinderBuffer* buf)
    { return G_CAST(buf, GBinderBufferPriv, pub); }

/*==========================================================================*
 * GBinderBufferContents
 *==========================================================================*/

static
GBinderBufferContents*
gbinder_buffer_contents_new(
    GBinderDriver* driver,
    void* buffer,
    gsize size,
    void** objects)
{
    GBinderBufferContents* self = g_slice_new0(GBinderBufferContents);

    g_atomic_int_set(&self->refcount, 1);
    self->buffer = buffer;
    self->size = size;
    self->objects = objects;
    self->driver = gbinder_driver_ref(driver);
    return self;
}

static
void
gbinder_buffer_contents_free(
    GBinderBufferContents* self)
{
    if (self->objects) {
        gbinder_driver_close_fds(self->driver, self->objects,
            ((guint8*)self->buffer) + self->size);
        g_free(self->objects);
    }
    gbinder_driver_free_buffer(self->driver, self->buffer);
    gbinder_driver_unref(self->driver);
    g_slice_free(GBinderBufferContents, self);
}

GBinderBufferContents*
gbinder_buffer_contents_ref(
    GBinderBufferContents* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
gbinder_buffer_contents_unref(
    GBinderBufferContents* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            gbinder_buffer_contents_free(self);
        }
    }
}

/*==========================================================================*
 * GBinderBufferContentsList
 * It's actually a GSList containing GBinderBufferContents refs.
 *==========================================================================*/

GBinderBufferContentsList*
gbinder_buffer_contents_list_add(
    GBinderBufferContentsList* list,
    GBinderBufferContents* contents)
{
    /* Prepend rather than append for better efficiency */
    return contents ? (GBinderBufferContentsList*) g_slist_prepend((GSList*)
            list, gbinder_buffer_contents_ref(contents)) : list;
}

GBinderBufferContentsList*
gbinder_buffer_contents_list_dup(
    GBinderBufferContentsList* list)
{
    GSList* out = NULL;

    if (list) {
        GSList* l = (GSList*) list;

        /* The order gets reversed but it doesn't matter */
        while (l) {
            out = g_slist_prepend(out, gbinder_buffer_contents_ref(l->data));
            l = l->next;
        }
    }
    return (GBinderBufferContentsList*) out;
}

void
gbinder_buffer_contents_list_free(
    GBinderBufferContentsList* list)
{
    g_slist_free_full((GSList*) list, (GDestroyNotify)
        gbinder_buffer_contents_unref);
}

/*==========================================================================*
 * GBinderBuffer
 *==========================================================================*/

static
GBinderBuffer*
gbinder_buffer_alloc(
    GBinderBufferContents* contents,
    void* data,
    gsize size)
{
    GBinderBufferPriv* priv = g_slice_new0(GBinderBufferPriv);
    GBinderBuffer* self = &priv->pub;

    priv->contents = contents;
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

        gbinder_buffer_contents_unref(priv->contents);
        g_slice_free(GBinderBufferPriv, priv);
    }
}

GBinderBuffer*
gbinder_buffer_new(
    GBinderDriver* driver,
    void* data,
    gsize size,
    void** objects)
{
    return gbinder_buffer_alloc((driver && data) ?
        gbinder_buffer_contents_new(driver, data, size, objects) : NULL,
        data, size);
}

GBinderBuffer*
gbinder_buffer_new_with_parent(
    GBinderBuffer* parent,
    void* data,
    gsize size)
{
    return gbinder_buffer_alloc(parent ?
        gbinder_buffer_contents_ref(gbinder_buffer_contents(parent)) : NULL,
        data, size);
}

gconstpointer
gbinder_buffer_data(
    GBinderBuffer* self,
    gsize* size)
{
    GBinderBufferContents* contents = gbinder_buffer_contents(self);

    if (G_LIKELY(contents)) {
        if (size) {
            *size = contents->size;
        }
        return contents->buffer;
    } else {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
}

GBinderDriver*
gbinder_buffer_driver(
    GBinderBuffer* self)
{
    if (G_LIKELY(self)) {
        GBinderBufferPriv* priv = gbinder_buffer_cast(self);

        if (priv->contents) {
            return priv->contents->driver;
        }
    }
    return NULL;
}

const GBinderIo*
gbinder_buffer_io(
    GBinderBuffer* buf)
{
    GBinderDriver* driver = gbinder_buffer_driver(buf);

    return driver ? gbinder_driver_io(driver) : NULL;
}

void**
gbinder_buffer_objects(
    GBinderBuffer* self)
{
    if (G_LIKELY(self)) {
        GBinderBufferPriv* priv = gbinder_buffer_cast(self);

        if (priv->contents) {
            return priv->contents->objects;
        }
    }
    return NULL;
}

GBinderBufferContents*
gbinder_buffer_contents(
    GBinderBuffer* self)
{
    return G_LIKELY(self) ? gbinder_buffer_cast(self)->contents : NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
