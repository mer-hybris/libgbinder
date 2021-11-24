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
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

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
    guint64* read_ptr;
    guint64* write_ptr;
    guint32* event_flag_ptr;
    guint32 refcount;
} GBinderFmq;

GBINDER_INLINE_FUNC
GBinderFmqGrantorDescriptor*
gbinder_fmq_get_grantor_descriptor(
    GBinderFmq* self,
    gint index)
{
    return (GBinderFmqGrantorDescriptor*)(self->desc->grantors.data.ptr) +
        index;
}

static
gsize
gbinder_fmq_available_to_read_bytes(
    GBinderFmq* self,
    gboolean contiguous)
{
    const guint64 read_ptr = __atomic_load_n(self->read_ptr, __ATOMIC_ACQUIRE);
    const gsize available_total = __atomic_load_n(self->write_ptr,
        __ATOMIC_ACQUIRE) - read_ptr;

    if (contiguous) {
        /*
         * The number of bytes that can be read contiguously from
         * read offset without wrapping around the ring buffer.
         */
        const gsize size = gbinder_fmq_get_grantor_descriptor(self,
            DATA_PTR_POS)->extent;
        const gsize available_contiguous = size - (read_ptr % size);

        return (available_contiguous < available_total) ?
            available_contiguous : available_total;
    } else {
        return available_total;
    }
}

static
gsize
gbinder_fmq_available_to_write_bytes(
    GBinderFmq* self,
    gboolean contiguous)
{
    const guint32 size = gbinder_fmq_get_grantor_descriptor(self,
        DATA_PTR_POS)->extent;
    const gsize available_total = size -
        gbinder_fmq_available_to_read_bytes(self, FALSE);

    if (contiguous) {
        /*
         * The number of bytes that can be written contiguously starting from
         * write_offset without wrapping around the ring buffer.
         */
        const guint64 write_ptr = __atomic_load_n(self->write_ptr,
            __ATOMIC_RELAXED);
        const gsize available_contiguous = size - (write_ptr % size);

        return (available_contiguous < available_total) ?
            available_contiguous : available_total;
    } else {
        return available_total;
    }
}

static
GBinderFmqGrantorDescriptor*
gbinder_fmq_create_grantors(
    gsize queue_size_bytes,
    gsize num_fds,
    gboolean configure_event_flag)
{
    const gsize num_grantors = configure_event_flag ?
        (EVENT_FLAG_PTR_POS + 1) : (DATA_PTR_POS + 1);
    GBinderFmqGrantorDescriptor* grantors =
        g_new0(GBinderFmqGrantorDescriptor, num_grantors);
    gsize pos, offset;
    gsize mem_sizes[] = {
        sizeof(guint64),  /* read pointer counter */
        sizeof(guint64),  /* write pointer counter */
        queue_size_bytes, /* data buffer */
        sizeof(guint32)   /* event flag pointer */
    };

    for (pos = 0, offset = 0; pos < num_grantors; pos++) {
        GBinderFmqGrantorDescriptor* grantor = grantors + pos;
        guint32 grantor_fd_index;
        gsize grantor_offset;

        if (pos == DATA_PTR_POS && num_fds == 2) {
            grantor_fd_index = 1;
            grantor_offset = 0;
        } else {
            grantor_fd_index = 0;
            grantor_offset = offset;
            offset += mem_sizes[pos];
        }
        grantor->fd_index = grantor_fd_index;
        grantor->offset = (guint32)G_ALIGN8(grantor_offset);
        grantor->extent = mem_sizes[pos];
    }
    return grantors;
}

static
void*
gbinder_fmq_map_grantor_descriptor(
    GBinderFmq* self,
    guint32 index)
{
    if (index < self->desc->grantors.count) {
        const GBinderFmqGrantorDescriptor* desc =
            gbinder_fmq_get_grantor_descriptor(self, index);
        /* Offset for mmap must be a multiple of PAGE_SIZE */
        const guint32 map_offset = (desc->offset & ~(getpagesize()-1));
        const guint32 map_length = desc->offset - map_offset + desc->extent;
        const GBinderFds* fds = self->desc->data.fds;
        void* address = mmap(0, map_length, PROT_READ | PROT_WRITE, MAP_SHARED,
            gbinder_fds_get_fd(fds, desc->fd_index), map_offset);

        if (address != MAP_FAILED) {
            return (guint8*)address + (desc->offset - map_offset);
        } else {
            GWARN("mmap failed: %d", errno);
        }
    }
    return NULL;
}

static
void
gbinder_fmq_unmap_grantor_descriptor(
    GBinderFmq* self,
    void* address,
    guint index)
{
    if (index < self->desc->grantors.count && address) {
        const GBinderFmqGrantorDescriptor* desc =
            gbinder_fmq_get_grantor_descriptor(self, index);
        const gsize remainder = desc->offset & (getpagesize() - 1);

        munmap((guint8*)address - remainder, remainder + desc->extent);
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

/* Private API */

GBinderMQDescriptor*
gbinder_fmq_get_descriptor(
    const GBinderFmq* self)
{
    return self->desc;
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
    if (item_size <= 0) {
        GWARN("Incorrect item size");
    } else if (num_items <= 0) {
        GWARN("Empty queue requested");
    } else if (num_items > SIZE_MAX / item_size) {
        GWARN("Requested message queue size too large");
    } else if (fd != -1 && num_items * item_size > buffer_size) {
        GWARN("The size needed for items (%"G_GSIZE_FORMAT") is larger "
            "than the supplied buffer size (%"G_GSIZE_FORMAT")",
            num_items * item_size, buffer_size);
    } else {
        GBinderFmq* self = g_slice_new0(GBinderFmq);
        gboolean configure_event_flag =
            (flags & GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG) != 0;
        gsize queue_size_bytes = num_items * item_size;
        gsize meta_data_size;
        gsize shmem_size;
        int shmem_fd;

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

        if (shmem_fd >= 0 && ftruncate(shmem_fd, shmem_size) == 0) {
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
            grantors = gbinder_fmq_create_grantors(queue_size_bytes,
                num_fds, configure_event_flag);

            /* Fill FMQ descriptor */
            self->desc = g_new0(GBinderMQDescriptor, 1);
            self->desc->data.fds = fds;
            self->desc->quantum = item_size;
            self->desc->flags = type;
            self->desc->grantors.data.ptr = grantors;
            self->desc->grantors.count = configure_event_flag ?
                (EVENT_FLAG_PTR_POS + 1) : (DATA_PTR_POS + 1);
            self->desc->grantors.owns_buffer = TRUE;

            /* Initialize memory pointers */
            if (type == GBINDER_FMQ_TYPE_SYNC_READ_WRITE) {
                self->read_ptr = gbinder_fmq_map_grantor_descriptor(self,
                    READ_PTR_POS);
            } else {
                /*
                 * Unsynchronized write FMQs may have multiple readers and
                 * each reader would have their own read pointer counter.
                 */
                self->read_ptr = g_new0(guint64, 1);
            }

            if (!self->read_ptr) {
                GWARN("Read pointer is null");
            }

            self->write_ptr = gbinder_fmq_map_grantor_descriptor(self,
                WRITE_PTR_POS);
            if (!self->write_ptr) {
                GWARN("Write pointer is null");
            }

            if (!(flags & GBINDER_FMQ_FLAG_NO_RESET_POINTERS)) {
                __atomic_store_n(self->read_ptr, 0, __ATOMIC_RELEASE);
                __atomic_store_n(self->write_ptr, 0, __ATOMIC_RELEASE);
            } else if (type != GBINDER_FMQ_TYPE_SYNC_READ_WRITE) {
                /* Always reset the read pointer */
                __atomic_store_n(self->read_ptr, 0, __ATOMIC_RELEASE);
            }

            self->ring = gbinder_fmq_map_grantor_descriptor(self,
                DATA_PTR_POS);
            if (!self->ring) {
                GWARN("Ring buffer pointer is null");
            }

            if (self->desc->grantors.count > EVENT_FLAG_PTR_POS) {
                self->event_flag_ptr = gbinder_fmq_map_grantor_descriptor(self,
                    EVENT_FLAG_PTR_POS);
                if (!self->event_flag_ptr) {
                    GWARN("Event flag pointer is null");
                }
            }

            g_atomic_int_set(&self->refcount, 1);
            return self;
        }

        GWARN("Failed to allocate shared memory: %s", strerror(errno));
        gbinder_fmq_free(self);
    }

    return NULL;
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
    return G_LIKELY(self) ? (gbinder_fmq_available_to_read_bytes(self, FALSE) /
        self->desc->quantum) : 0;
}

gsize
gbinder_fmq_available_to_write(
    GBinderFmq* self)
{
    return G_LIKELY(self) ? (gbinder_fmq_available_to_write_bytes(self, FALSE) /
        self->desc->quantum) : 0;
}

gsize
gbinder_fmq_available_to_read_contiguous(
    GBinderFmq* self)
{
    return G_LIKELY(self) ? (gbinder_fmq_available_to_read_bytes(self, TRUE) /
        self->desc->quantum) : 0;
}

gsize
gbinder_fmq_available_to_write_contiguous(
    GBinderFmq* self)
{
    return G_LIKELY(self) ? (gbinder_fmq_available_to_write_bytes(self, TRUE) /
        self->desc->quantum) : 0;
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
        guint64 write_ptr = __atomic_load_n(self->write_ptr, __ATOMIC_ACQUIRE);
        guint64 read_ptr = __atomic_load_n(self->read_ptr, __ATOMIC_RELAXED);

        if ((write_ptr % item_size) || (read_ptr % item_size)) {
            GWARN("Unable to write data because of misaligned pointer");
        } else if (write_ptr - read_ptr > size) {
            __atomic_store_n(self->read_ptr, write_ptr, __ATOMIC_RELEASE);
        } else if (write_ptr - read_ptr < bytes_desired) {
            /* Not enough data to read in FMQ. */
        } else {
            ptr = self->ring + (read_ptr % size);
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
        const gsize item_size = self->desc->quantum;
        const gsize size = gbinder_fmq_get_grantor_descriptor(self,
            DATA_PTR_POS)->extent;

        if ((self->desc->flags == GBINDER_FMQ_TYPE_SYNC_READ_WRITE &&
            (gbinder_fmq_available_to_write(self) < items)) ||
            items > size / item_size) {
            /* Incorrect parameters */
        } else {
            guint64 write_ptr = __atomic_load_n(self->write_ptr,
                __ATOMIC_RELAXED);

            if (write_ptr % item_size) {
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
        guint64 read_ptr = __atomic_load_n(self->read_ptr, __ATOMIC_RELAXED);
        guint64 write_ptr = __atomic_load_n(self->write_ptr, __ATOMIC_ACQUIRE);

        /*
         * If queue type is unsynchronized, it is possible that a write
         * overflow may have occurred.
         */
        if (write_ptr - read_ptr > size) {
            __atomic_store_n(self->read_ptr, write_ptr, __ATOMIC_RELEASE);
        } else {
            read_ptr += items * self->desc->quantum;
            __atomic_store_n(self->read_ptr, read_ptr, __ATOMIC_RELEASE);
        }
    }
}

void
gbinder_fmq_end_write(
    GBinderFmq* self,
    gsize items)
{
    if (G_LIKELY(self) && G_LIKELY(items > 0)) {
        guint64 write_ptr = __atomic_load_n(self->write_ptr, __ATOMIC_RELAXED);

        write_ptr += items * self->desc->quantum;
        __atomic_store_n(self->write_ptr, write_ptr, __ATOMIC_RELEASE);
    }
}

gboolean
gbinder_fmq_read(
    GBinderFmq* self,
    void* data,
    gsize items)
{
    if (G_LIKELY(self) && G_LIKELY(data) && G_LIKELY(items > 0)) {
        const void* in_data = gbinder_fmq_begin_read(self, items);

        if (in_data) {
            /*
             * The number of messages that can be read contiguously without
             * wrapping around the ring buffer.
             */
            const gsize contiguous_messages =
                gbinder_fmq_available_to_read_contiguous(self);
            const gsize item_size = self->desc->quantum;

            if (contiguous_messages < items) {
                /* A wrap around is required */
                memcpy(data, in_data, contiguous_messages * item_size);
                memcpy((char*)data + contiguous_messages * item_size,
                    self->ring, (items - contiguous_messages) * item_size);
            } else {
                /* A wrap around is not required */
                memcpy(data, in_data, items * item_size);
            }

            gbinder_fmq_end_read(self, items);
            return TRUE;
        }
    }
    return FALSE;
}

gboolean
gbinder_fmq_write(
    GBinderFmq* self,
    const void* data,
    gsize items)
{
    if (G_LIKELY(self) && G_LIKELY(data) && G_LIKELY(items > 0)) {
        void *out_data = gbinder_fmq_begin_write(self, items);

        if (out_data) {
            /*
             * The number of messages that can be written contiguously without
             * wrapping around the ring buffer.
             */
            const gsize contiguous_messages =
                gbinder_fmq_available_to_write_contiguous(self);
            const gsize item_size = self->desc->quantum;

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
            return TRUE;
        }
    }
    return FALSE;
}

int
gbinder_fmq_wait_timeout(
    GBinderFmq* self,
    guint32 bit_mask,
    guint32* state,
    int timeout_ms)
{
    if (G_UNLIKELY(!state) || G_UNLIKELY(!self)) {
        return (-EINVAL);
    } else if (!self->event_flag_ptr) {
        return (-ENOSYS);
    } else if (!bit_mask) {
        return (-EINVAL);
    } else {
        guint32 old_value = __atomic_fetch_and(self->event_flag_ptr, ~bit_mask,
            __ATOMIC_SEQ_CST);
        guint32 set_bits = old_value & bit_mask;

        /* Check if any of the bits was already set */
        if (set_bits != 0) {
            *state = set_bits;
            return 0;
        } else if (!timeout_ms) {
            return (-ETIMEDOUT);
        } else {
            int ret;

            if (timeout_ms > 0) {
                struct timespec deadline;
                const guint64 ms = 1000000;
                const guint64 sec = 1000 * ms;

                clock_gettime(CLOCK_MONOTONIC, &deadline);
                deadline.tv_sec += timeout_ms / 1000;
                deadline.tv_nsec += (timeout_ms % 1000) * ms;
                if (deadline.tv_nsec >= sec) {
                    deadline.tv_sec++;
                    deadline.tv_nsec -= sec;
                }

                ret = syscall(__NR_futex, self->event_flag_ptr,
                    FUTEX_WAIT_BITSET, old_value, &deadline, NULL, bit_mask);
            } else {
                ret = syscall(__NR_futex, self->event_flag_ptr,
                    FUTEX_WAIT_BITSET, old_value, NULL, NULL, bit_mask);
            }

            if (ret == -1) {
                return errno ? (-errno) : -EFAULT;
            } else {
                old_value = __atomic_fetch_and(self->event_flag_ptr, ~bit_mask,
                    __ATOMIC_SEQ_CST);
                *state = old_value & bit_mask;
                return (*state) ? 0 : (-EAGAIN);
            }
        }
    }
}

int
gbinder_fmq_wake(
    GBinderFmq* self,
    guint32 bit_mask)
{
    int ret = 0;

    if (G_LIKELY(self)) {
        if (!self->event_flag_ptr) {
            /* Event flag is not configured */
            ret = -ENOSYS;
        } else if (!bit_mask) {
            /* Ignore zero bit mask */
        } else {
            /* Set bit mask only if needed */
            guint32 old_value = __atomic_fetch_or(self->event_flag_ptr,
                bit_mask, __ATOMIC_SEQ_CST);

            if (~old_value & bit_mask) {
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
