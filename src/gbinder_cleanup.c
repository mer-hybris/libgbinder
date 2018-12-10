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

#include "gbinder_cleanup.h"

typedef struct gbinder_cleanup_item {
    GDestroyNotify destroy;
    gpointer pointer;
} GBinderCleanupItem;

/*
 * This is basically a GArray providing better type safety at compile time.
 */
struct gbinder_cleanup {
    GBinderCleanupItem* items;
    guint count;
};

G_STATIC_ASSERT(sizeof(GBinderCleanup) == sizeof(GArray));
#define ELEMENT_SIZE (sizeof(GBinderCleanupItem))

static
void
gbinder_cleanup_destroy_func(
    gpointer data)
{
    GBinderCleanupItem* item = data;

    item->destroy(item->pointer);
}

static
GBinderCleanup*
gbinder_cleanup_new()
{
    GArray* array = g_array_sized_new(FALSE, FALSE, ELEMENT_SIZE, 0);

    g_array_set_clear_func(array, gbinder_cleanup_destroy_func);
    return (GBinderCleanup*)array;
}

void
gbinder_cleanup_reset(
    GBinderCleanup* self)
{
    if (G_LIKELY(self)) {
        g_array_set_size((GArray*)self, 0);
    }
}

void
gbinder_cleanup_free(
    GBinderCleanup* self)
{
    if (G_LIKELY(self)) {
        g_array_free((GArray*)self, TRUE);
    }
}

GBinderCleanup*
gbinder_cleanup_add(
    GBinderCleanup* self,
    GDestroyNotify destroy,
    gpointer pointer)
{
    if (G_LIKELY(destroy)) {
        GBinderCleanupItem item;

        item.destroy = destroy;
        item.pointer = pointer;
        if (!self) {
            self = gbinder_cleanup_new();
        }
        g_array_append_vals((GArray*)self, (void*)&item, 1);
    }
    return self;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
