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

#include "gbinder_fmq_p.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/memfd.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Private API */

/* Grantor data positions */
enum {
    READ_PTR_POS = 0,
    WRITE_PTR_POS,
    DATA_PTR_POS,
    EVENT_FLAG_PTR_POS
};

typedef struct gbinder_fmq {
    GBinderMQDescriptor* desc;
    guint8* ring;
    _Atomic guint64* read_ptr;
    _Atomic guint64* write_ptr;
    _Atomic guint32* event_flag_ptr;
    guint32 refcount;
} GBinderFmq;

GBinderMQDescriptor*
gbinder_fmq_get_descriptor(
    const GBinderFmq* self)
{
    return self->desc;
}

GBINDER_INLINE_FUNC
GBinderFmqGrantorDescriptor*
gbinder_fmq_get_grantor_descriptor(
    GBinderFmq* self,
    gint index)
{
    return &((GBinderFmqGrantorDescriptor *)(
        self->desc->grantors.data.ptr))[index];
}

static
gsize
gbinder_fmq_available_to_read_bytes(
    GBinderFmq* self,
    gboolean contiguous)
{
    gsize available;

    guint64 read_ptr =
        atomic_load_explicit(self->read_ptr, memory_order_acquire);
    gsize available_to_read =
        atomic_load_explicit(self->write_ptr, memory_order_acquire) - read_ptr;

    if (contiguous) {
        gsize size =
            gbinder_fmq_get_grantor_descriptor(self, DATA_PTR_POS)->extent;
        gsize read_offset = read_ptr % size;
        /* The number of bytes that can be read contiguously from
         * read_offset without wrapping around the ring buffer */
        gsize available_to_read_contiguous = size - read_offset;
        available = available_to_read_contiguous < available_to_read ?
            available_to_read_contiguous : available_to_read;
    } else {
        available = available_to_read;
    }

    return available;
}

static
gsize
gbinder_fmq_available_to_write_bytes(
    GBinderFmq* self,
    gboolean contiguous)
{
    gsize available;

    gsize available_to_write =
        gbinder_fmq_get_grantor_descriptor(self, DATA_PTR_POS)->extent -
        gbinder_fmq_available_to_read_bytes(self, FALSE);

    if (contiguous) {
        guint64 write_ptr = atomic_load_explicit(self->write_ptr,
            memory_order_relaxed);
        gsize size =
            gbinder_fmq_get_grantor_descriptor(self, DATA_PTR_POS)->extent;
        gsize write_offset = write_ptr % size;
        /* The number of bytes that can be written contiguously starting from
         * write_offset without wrapping around the ring buffer */
        gsize available_to_write_contiguous = size - write_offset;
        available = available_to_write_contiguous < available_to_write ?
            available_to_write_contiguous : available_to_write;
    } else {
        available = available_to_write;
    }

    return available;
}

static
GBinderFmqGrantorDescriptor*
gbinder_fmq_create_grantors(
    gsize queue_size_bytes,
    gsize num_fds,
    gboolean configure_event_flag)
{
    gsize num_grantors = configure_event_flag ? EVENT_FLAG_PTR_POS + 1
                                              : DATA_PTR_POS + 1;

    GBinderFmqGrantorDescriptor* grantors =
        g_new0(GBinderFmqGrantorDescriptor, num_grantors);

    gsize mem_sizes[] = {
        sizeof(guint64),  /* read pointer counter */
        sizeof(guint64),  /* write pointer counter */
        queue_size_bytes, /* data buffer */
        sizeof(guint32)   /* event flag pointer */
    };
    gsize grantor_pos, offset;

    for (grantor_pos = 0, offset = 0; grantor_pos < num_grantors;
            grantor_pos++) {
        GBinderFmqGrantorDescriptor *grantor = &grantors[grantor_pos];
        guint32 grantor_fd_index;
        gsize grantor_offset;
        if (grantor_pos == DATA_PTR_POS && num_fds == 2) {
            grantor_fd_index = 1;
            grantor_offset = 0;
        } else {
            grantor_fd_index = 0;
            grantor_offset = offset;
            offset += mem_sizes[grantor_pos];
        }
        grantor->fd_index = grantor_fd_index;
        grantor->offset = (guint32)(G_ALIGN8(grantor_offset));
        grantor->extent = mem_sizes[grantor_pos];
    }
    return grantors;
}

static
void*
gbinder_fmq_map_grantor_descriptor(
    GBinderFmq* self,
    guint32 index)
{
    const GBinderFds* fds = self->desc->data.fds;
    gint fd_index;
    gint map_offset;
    gint map_length;
    void* address;
    void* ptr = NULL;

    if (index >= self->desc->grantors.count) {
        GWARN("grantor index must be less than %d", self->desc->grantors.count);
    } else {
        fd_index = gbinder_fmq_get_grantor_descriptor(self, index)->fd_index;
        /* Offset for mmap must be a multiple of PAGE_SIZE */
        map_offset = (gbinder_fmq_get_grantor_descriptor(self, index)->offset /
            getpagesize()) * getpagesize();
        map_length = gbinder_fmq_get_grantor_descriptor(self, index)->offset -
            map_offset +
            gbinder_fmq_get_grantor_descriptor(self, index)->extent;

        address = mmap(0, map_length, PROT_READ | PROT_WRITE, MAP_SHARED,
            gbinder_fds_get_fd(fds, fd_index), map_offset);
        if (address == MAP_FAILED) {
            GWARN("mmap failed: %d", errno);
        } else {
            ptr = (guint8*)(address) +
                (gbinder_fmq_get_grantor_descriptor(self, index)->offset -
                map_offset);
        }
    }
    return ptr;
}

static
void
gbinder_fmq_unmap_grantor_descriptor(
    GBinderFmq* self,
    void* address,
    guint32 index)
{
    gint map_offset;
    gint map_length;
    void* base_address;

    if (index >= self->desc->grantors.count) {
        GWARN("grantor index must be less than %d", self->desc->grantors.count);
    } else if (address) {
        map_offset = (gbinder_fmq_get_grantor_descriptor(self, index)->offset /
            getpagesize()) * getpagesize();
        map_length =
            gbinder_fmq_get_grantor_descriptor(self, index)->offset - map_offset
            + gbinder_fmq_get_grantor_descriptor(self, index)->extent;

        base_address = (guint8*)(address) -
            (gbinder_fmq_get_grantor_descriptor(self, index)->offset -
            map_offset);
        if (base_address) {
            munmap(base_address, map_length);
        }
    }
}

static
void
gbinder_fmq_free(
    GBinderFmq* self)
{
    if (self->desc) {
        if (self->desc->flags == GBINDER_FMQ_TYPE_UNSYNC_WRITE) {
            g_free(self->read_ptr);
        } else {
            gbinder_fmq_unmap_grantor_descriptor(self, self->read_ptr,
                READ_PTR_POS);
        }
        gbinder_fmq_unmap_grantor_descriptor(self, self->write_ptr,
            WRITE_PTR_POS);
        gbinder_fmq_unmap_grantor_descriptor(self, self->ring,
            DATA_PTR_POS);
        gbinder_fmq_unmap_grantor_descriptor(self, self->event_flag_ptr,
            EVENT_FLAG_PTR_POS);

        g_free((GBinderFmqGrantorDescriptor*)self->desc->grantors.data.ptr);
        g_free((GBinderFds*)self->desc->data.fds);

        g_free(self->desc);
    }
    g_slice_free(GBinderFmq, self);
}

/* Public API */

GBinderFmq*
gbinder_fmq_new(
    gsize item_size,
    gsize num_items,
    GBINDER_FMQ_TYPE type,
    GBINDER_FMQ_FLAGS flags,
    gint fd,
    gsize buffer_size)
{
    GBinderFmq* self = NULL;

    if (item_size <= 0) {
        GWARN("Incorrect item size");
    } else if (num_items <= 0) {
        GWARN("Empty queue requested");
    } else if (num_items > SIZE_MAX / item_size) {
        GWARN("Requested message queue size too large");
    } else if (fd != -1 && num_items * item_size > buffer_size) {
        GWARN("The size needed for items (%"G_GSIZE_FORMAT") is larger\
            than the supplied buffer size (%"G_GSIZE_FORMAT")",
            num_items * item_size, buffer_size);
    } else {
        gboolean configure_event_flag =
            (flags & GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG) != 0;
        gsize queue_size_bytes = num_items * item_size;
        gsize meta_data_size;
        gsize shmem_size;
        int shmem_fd;

        self = g_slice_new0(GBinderFmq);

        meta_data_size = 2 * sizeof(guint64);
        if (configure_event_flag) {
            meta_data_size += sizeof(guint32);
        }

        /* Allocate shared memory */
        if (fd != -1) {
            /* User-supplied ringbuffer memory provided,
             * allocating memory only for meta data */
            shmem_size = (meta_data_size + getpagesize() - 1) &
                ~(getpagesize() - 1);
        } else {
            /* Allocate ringbuffer, read counter and write counter */
            shmem_size = (G_ALIGN8(queue_size_bytes) +
                meta_data_size + getpagesize() - 1) & ~(getpagesize() - 1);
        }

        shmem_fd = syscall(__NR_memfd_create, "MessageQueue", MFD_CLOEXEC);

        if (shmem_fd < 0 || ftruncate(shmem_fd, shmem_size) < 0) {
            GWARN("Failed to create shared memory file");
            gbinder_fmq_free(self);
            self = NULL;
        } else {
            GBinderFmqGrantorDescriptor* grantors;
            gsize num_fds = (fd != -1) ? 2 : 1;
            gsize fds_size = sizeof(GBinderFds) + sizeof(int) * num_fds;
            GBinderFds* fds = (GBinderFds*)g_malloc0(fds_size);

            fds->version = fds_size;
            fds->num_fds = num_fds;

            (((int*)((fds) + 1))[0]) = shmem_fd;

            if (fd != -1) {
                /* Use user-supplied file descriptor for fd_index 1 */
                (((int*)((fds) + 1))[1]) = fd;
            }
            grantors = gbinder_fmq_create_grantors(
                queue_size_bytes, num_fds, configure_event_flag);

            /* Fill FMQ descriptor */
            self->desc = g_new0(GBinderMQDescriptor, 1);
            self->desc->data.fds = fds;
            self->desc->quantum = item_size;
            self->desc->flags = type;
            self->desc->grantors.data.ptr = grantors;
            self->desc->grantors.count =
                configure_event_flag ? EVENT_FLAG_PTR_POS + 1
                                     : DATA_PTR_POS + 1;
            self->desc->grantors.owns_buffer = TRUE;

            /* Initialize memory pointers */
            if (type == GBINDER_FMQ_TYPE_SYNC_READ_WRITE) {
                self->read_ptr = (_Atomic guint64*)(
                    gbinder_fmq_map_grantor_descriptor(self, READ_PTR_POS));
            } else {
                /* Unsynchronized write FMQs may have multiple readers and
                 * each reader would have their own read pointer counter */
                self->read_ptr = g_new0(_Atomic guint64, 1);
            }

            if (self->read_ptr == NULL) {
                GWARN("Read pointer is null");
            }

            self->write_ptr = (_Atomic guint64*)(
                gbinder_fmq_map_grantor_descriptor(self, WRITE_PTR_POS));
            if (self->write_ptr == NULL) {
                GWARN("Write pointer is null");
            }

            if ((flags & GBINDER_FMQ_FLAG_NO_RESET_POINTERS) == 0) {
                atomic_store_explicit(self->read_ptr, 0, memory_order_release);
                atomic_store_explicit(self->write_ptr, 0, memory_order_release);
            } else if (type != GBINDER_FMQ_TYPE_SYNC_READ_WRITE) {
                /* Always reset the read pointer */
                atomic_store_explicit(self->read_ptr, 0, memory_order_release);
            }

            self->ring = (guint8 *)(gbinder_fmq_map_grantor_descriptor(self,
                DATA_PTR_POS));
            if (self->ring == NULL) {
                GWARN("Ring buffer pointer is null");
            }

            if (self->desc->grantors.count > EVENT_FLAG_PTR_POS) {
                self->event_flag_ptr = (_Atomic guint32*)(
                    gbinder_fmq_map_grantor_descriptor(self,
                    EVENT_FLAG_PTR_POS));
                if (self->event_flag_ptr == NULL) {
                    GWARN("Event flag pointer is null");
                }
            }

            g_atomic_int_set(&self->refcount, 1);
        }
    }

    return self;
}

GBinderFmq*
gbinder_fmq_ref(
    GBinderFmq* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
gbinder_fmq_unref(
    GBinderFmq* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            gbinder_fmq_free(self);
        }
    }
}

gsize
gbinder_fmq_available_to_read(
    GBinderFmq* self)
{
    gsize ret = 0;
    if (G_LIKELY(self)) {
        ret = gbinder_fmq_available_to_read_bytes(self, FALSE) /
            self->desc->quantum;
    }
    return ret;
}

gsize
gbinder_fmq_available_to_write(
    GBinderFmq* self)
{
    gsize ret = 0;
    if (G_LIKELY(self)) {
        ret = gbinder_fmq_available_to_write_bytes(self, FALSE) /
            self->desc->quantum;
    }
    return ret;
}

gsize
gbinder_fmq_available_to_read_contiguous(
    GBinderFmq* self)
{
    gsize ret = 0;
    if (G_LIKELY(self)) {
        ret = gbinder_fmq_available_to_read_bytes(self, TRUE) /
            self->desc->quantum;
    }
    return ret;
}

gsize
gbinder_fmq_available_to_write_contiguous(
    GBinderFmq* self)
{
    gsize ret = 0;
    if (G_LIKELY(self)) {
        ret = gbinder_fmq_available_to_write_bytes(self, TRUE) /
            self->desc->quantum;
    }
    return ret;
}

const void*
gbinder_fmq_begin_read(
    GBinderFmq* self,
    gsize items)
{
    void* ptr = NULL;
    if (G_LIKELY(self) && G_LIKELY(items > 0)) {
        gsize size = gbinder_fmq_get_grantor_descriptor(self,
            DATA_PTR_POS)->extent;
        gsize item_size = self->desc->quantum;
        gsize bytes_desired = items * item_size;
        gsize read_offset;

        guint64 write_ptr = atomic_load_explicit(self->write_ptr,
            memory_order_acquire);
        guint64 read_ptr = atomic_load_explicit(self->read_ptr,
            memory_order_relaxed);

        if (write_ptr % item_size != 0 || read_ptr % item_size != 0) {
            GWARN("Unable to write data because of misaligned pointer");
        } else if (write_ptr - read_ptr > size) {
            atomic_store_explicit(self->read_ptr, write_ptr,
                memory_order_release);
        } else if (write_ptr - read_ptr < bytes_desired) {
            /* Not enough data to read in FMQ. */
        } else {
            read_offset = read_ptr % size;
            ptr = self->ring + read_offset;
        }
    }

    return ptr;
}

void*
gbinder_fmq_begin_write(
    GBinderFmq* self,
    gsize items)
{
    void* ptr = NULL;
    if (G_LIKELY(self) && G_LIKELY(items > 0)) {
        gsize size = gbinder_fmq_get_grantor_descriptor(self,
            DATA_PTR_POS)->extent;
        gsize item_size = self->desc->quantum;

        if ((self->desc->flags == GBINDER_FMQ_TYPE_SYNC_READ_WRITE &&
            (gbinder_fmq_available_to_write(self) < items)) ||
            items > gbinder_fmq_get_grantor_descriptor(self,
                DATA_PTR_POS)->extent / item_size) {
            /* Incorrect parameters */
        } else {
            guint64 write_ptr = atomic_load_explicit(self->write_ptr,
                memory_order_relaxed);
            if (write_ptr % item_size != 0) {
                GWARN("The write pointer has become misaligned.");
            } else {
                ptr = self->ring + (write_ptr % size);
            }
        }
    }
    return ptr;
}

void
gbinder_fmq_end_read(
    GBinderFmq* self,
    gsize items)
{
    if (G_LIKELY(self) && G_LIKELY(items > 0)) {
        gsize size = gbinder_fmq_get_grantor_descriptor(self,
            DATA_PTR_POS)->extent;

        guint64 read_ptr = atomic_load_explicit(self->read_ptr,
            memory_order_relaxed);
        guint64 write_ptr = atomic_load_explicit(self->write_ptr,
            memory_order_acquire);

        /* If queue type is unsynchronized, it is possible that a write overflow
         * may have occurred */
        if (write_ptr - read_ptr > size) {
            atomic_store_explicit(self->read_ptr, write_ptr,
                memory_order_release);
        } else {
            read_ptr += items * self->desc->quantum;
            atomic_store_explicit(self->read_ptr, read_ptr,
                memory_order_release);
        }
    }
}

void
gbinder_fmq_end_write(
    GBinderFmq* self,
    gsize items)
{
    if (G_LIKELY(self) && G_LIKELY(items > 0)) {
        guint64 write_ptr = atomic_load_explicit(self->write_ptr,
            memory_order_relaxed);

        write_ptr += items * self->desc->quantum;
        atomic_store_explicit(self->write_ptr, write_ptr, memory_order_release);
    }
}

gboolean
gbinder_fmq_read(
    GBinderFmq* self,
    void* data,
    gsize items)
{
    gboolean ret = FALSE;
    if (G_LIKELY(self) && G_LIKELY(data) && G_LIKELY(items > 0)) {
        const void *in_data = gbinder_fmq_begin_read(self, items);

        if (in_data) {
            gsize item_size = self->desc->quantum;

            /* The number of messages that can be read contiguously without
             * wrapping around the ring buffer */
            gsize contiguous_messages =
                gbinder_fmq_available_to_read_contiguous(self);

            if (contiguous_messages < items) {
                /* A wrap around is required */
                memcpy(data, in_data, contiguous_messages * item_size);
                memcpy((char *)data + contiguous_messages * item_size /
                    sizeof(char), self->ring,
                    (items - contiguous_messages) * item_size);
            } else {
                /* A wrap around is not required */
                memcpy(data, in_data, items * item_size);
            }

            gbinder_fmq_end_read(self, items);
            ret = TRUE;
        }
    }

    return ret;
}

gboolean
gbinder_fmq_write(
    GBinderFmq* self,
    const void* data,
    gsize items)
{
    gboolean ret = FALSE;
    if (G_LIKELY(self) && G_LIKELY(data) && G_LIKELY(items > 0)) {
        void *out_data = gbinder_fmq_begin_write(self, items);

        if (out_data) {
            gsize item_size = self->desc->quantum;

            /* The number of messages that can be written contiguously without
             * wrapping around the ring buffer */
            gsize contiguous_messages =
                gbinder_fmq_available_to_write_contiguous(self);

            if (contiguous_messages < items) {
                /* A wrap around is required. */
                memcpy(out_data, data, contiguous_messages * item_size);
                memcpy(self->ring, (char *)data + contiguous_messages *
                    item_size / sizeof(char),
                    (items - contiguous_messages) * item_size);
            } else {
                /* A wrap around is not required to write items */
                memcpy(out_data, data, items * item_size);
            }

            gbinder_fmq_end_write(self, items);
            ret = TRUE;
        }
    }

    return ret;
}

int
gbinder_fmq_wait_timeout(
    GBinderFmq* self,
    guint32 bit_mask,
    guint32* state,
    guint timeout_ms)
{
    int ret = 0;
    if (G_LIKELY(self) && G_LIKELY(state)) {
        /* Event flag is not configured */
        if (self->event_flag_ptr == NULL) {
            ret = -ENOSYS;
        } else if (bit_mask == 0 || state == NULL) {
            ret = -EINVAL;
        } else {
            guint32 old_value = atomic_fetch_and(self->event_flag_ptr,
                ~bit_mask);
            guint32 set_bits = old_value & bit_mask;

            /* Check if any of the bits was already set */
            if (set_bits != 0) {
                *state = set_bits;
            } else {
                if (timeout_ms > 0) {
                    struct timespec wait_time;
                    clock_gettime(CLOCK_MONOTONIC, &wait_time);
                    guint64 ns_in_sec = 1000000000;

                    wait_time.tv_sec += timeout_ms / 1000;
                    wait_time.tv_nsec += (timeout_ms % 1000) * 1000000;

                    if (wait_time.tv_nsec >= ns_in_sec) {
                        wait_time.tv_sec++;
                        wait_time.tv_nsec -= ns_in_sec;
                    }

                    ret = syscall(__NR_futex, self->event_flag_ptr,
                        FUTEX_WAIT_BITSET, old_value, &wait_time, NULL,
                            bit_mask);
                } else {
                    ret = syscall(__NR_futex, self->event_flag_ptr,
                        FUTEX_WAIT_BITSET, old_value, NULL, NULL, bit_mask);
                }

                if (ret != -1) {
                    old_value = atomic_fetch_and(self->event_flag_ptr,
                        ~bit_mask);
                    *state = old_value & bit_mask;

                    if (*state == 0) {
                        ret = -EINTR;
                    }
                    ret = 0;
                } else {
                    /* Report error code */
                    *state = 0;
                    ret = -errno;
                }
            }
        }
    } else {
        ret = -EINVAL;
    }
    return ret;
}

int
gbinder_fmq_wake(
    GBinderFmq* self,
    guint32 bit_mask)
{
    int ret = 0;
    if (G_LIKELY(self)) {
        if (self->event_flag_ptr == NULL) {
            /* Event flag is not configured */
            ret = -ENOSYS;
        } else if (bit_mask == 0) {
            /* Ignore zero bit mask */
        } else {
            /* Set bit mask only if needed */
            guint32 old_value = atomic_fetch_or(self->event_flag_ptr, bit_mask);
            if ((~old_value & bit_mask) != 0) {
                ret = syscall(__NR_futex, self->event_flag_ptr,
                    FUTEX_WAKE_BITSET, G_MAXUINT32, NULL, NULL, bit_mask);
            }

            if (ret == -1) {
                /* Report error code */
                ret = -errno;
            }
        }
    } else {
        ret = -EINVAL;
    }
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
