/*
 * Copyright (C) 2021 Jolla Ltd.
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

#ifndef GBINDER_FMQ_H
#define GBINDER_FMQ_H

#include <gbinder_types.h>

G_BEGIN_DECLS

/* Since 1.1.14 */

typedef enum gbinder_fmq_type {
    GBINDER_FMQ_TYPE_SYNC_READ_WRITE = 1,
    GBINDER_FMQ_TYPE_UNSYNC_WRITE
} GBINDER_FMQ_TYPE;

typedef enum gbinder_fmq_flags {
    GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG = 0x1,
    GBINDER_FMQ_FLAG_NO_RESET_POINTERS    = 0x2
} GBINDER_FMQ_FLAGS;

GBinderFmq*
gbinder_fmq_new(
    gsize item_size,
    gsize max_num_items,
    GBINDER_FMQ_TYPE type,
    GBINDER_FMQ_FLAGS flags,
    gint fd,
    gsize buffer_size);

GBinderFmq*
gbinder_fmq_ref(
    GBinderFmq* fmq);

void
gbinder_fmq_unref(
    GBinderFmq* fmq);

/* Functions for checking how many items are available in queue */
gsize
gbinder_fmq_available_to_read(
    GBinderFmq* fmq);

gsize
gbinder_fmq_available_to_write(
    GBinderFmq* fmq);

gsize
gbinder_fmq_available_to_read_contiguous(
    GBinderFmq* fmq);

gsize
gbinder_fmq_available_to_write_contiguous(
    GBinderFmq* fmq);

/* Functions for obtaining data pointer for zero copy read/write */
const void*
gbinder_fmq_begin_read(
    GBinderFmq* fmq,
    gsize items);

void*
gbinder_fmq_begin_write(
    GBinderFmq* fmq,
    gsize items);

/* Functions for ending zero copy read/write
 * The number of items must match the value provided to gbinder_fmq_begin_read
 * or gbinder_fmq_begin_write */
void
gbinder_fmq_end_read(
    GBinderFmq* fmq,
    gsize items);

void
gbinder_fmq_end_write(
    GBinderFmq* fmq,
    gsize items);

/* Regular read/write functions (non-zero-copy) */
gboolean
gbinder_fmq_read(
    GBinderFmq* fmq,
    void* data,
    gsize items);

gboolean
gbinder_fmq_write(
    GBinderFmq* fmq,
    const void* data,
    gsize items);

/*
 * Functions for waiting and waking message queue.
 * Requires configured event flag in message queue.
 */
int
gbinder_fmq_wait_timeout(
    GBinderFmq* fmq,
    guint32 bit_mask,
    guint32* state,
    int timeout_ms);

#define gbinder_fmq_try_wait(fmq, mask, state) \
    gbinder_fmq_wait_timeout(fmq, mask, state, 0)

#define gbinder_fmq_wait(fmq, mask, state) \
    gbinder_fmq_wait_timeout(fmq, mask, state, -1)

int
gbinder_fmq_wake(
    GBinderFmq* fmq,
    guint32 bit_mask);

G_END_DECLS

#endif /* GBINDER_FMQ_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
