/*
 * Copyright (C) 2018-2022 Jolla Ltd.
 * Copyright (C) 2018-2022 Slava Monich <slava.monich@jolla.com>
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

#ifndef TEST_BINDER_H
#define TEST_BINDER_H

#include "test_common.h"

#define B_TYPE_LARGE 0x85
#define BINDER_TYPE_BINDER  GBINDER_FOURCC('s', 'b', '*', B_TYPE_LARGE)
#define BINDER_TYPE_HANDLE  GBINDER_FOURCC('s', 'h', '*', B_TYPE_LARGE)
#define BINDER_TYPE_PTR     GBINDER_FOURCC('p', 't', '*', B_TYPE_LARGE)

#define BUFFER_OBJECT_SIZE_32 (24)
#define BUFFER_OBJECT_SIZE_64 (40)
#define BINDER_OBJECT_SIZE_32 (16)
#define BINDER_OBJECT_SIZE_64 (24)

typedef enum test_br_thread {
    THIS_THREAD = -3,
    LOOPER_THREAD = -2,
    TX_THREAD = -1,
    ANY_THREAD = 0
} TEST_BR_THREAD;

void
test_binder_br_noop(
    int fd,
    TEST_BR_THREAD dest);

void
test_binder_br_increfs(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr);

void
test_binder_br_acquire(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr);

void
test_binder_br_release(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr);

void
test_binder_br_decrefs(
    int fd,
    TEST_BR_THREAD dest,
    void* ptr);

void
test_binder_br_transaction_complete(
    int fd,
    TEST_BR_THREAD dest);

void
test_binder_br_dead_binder(
    int fd,
    TEST_BR_THREAD dest,
    guint handle);

void
test_binder_br_dead_binder_obj(
    int fd,
    GBinderLocalObject* obj);

void
test_binder_br_dead_reply(
    int fd,
    TEST_BR_THREAD dest);

void
test_binder_br_failed_reply(
    int fd,
    TEST_BR_THREAD dest);

void
test_binder_br_transaction(
    int fd,
    TEST_BR_THREAD dest,
    void* target,
    guint32 code,
    const GByteArray* bytes);

void
test_binder_br_reply(
    int fd,
    TEST_BR_THREAD dest,
    guint32 handle,
    guint32 code,
    const GByteArray* bytes);

void
test_binder_br_reply_status(
    int fd,
    TEST_BR_THREAD dest,
    gint32 status);

void
test_binder_ignore_dead_object(
    int fd);

int
test_binder_handle(
    int fd,
    GBinderLocalObject* obj);

GBinderLocalObject*
test_binder_object(
    int fd,
    guint handle)
    G_GNUC_WARN_UNUSED_RESULT; /* Need to unref */

guint
test_binder_register_object(
    int fd,
    GBinderLocalObject* obj,
    guint handle);

#define AUTO_HANDLE ((guint)-1)

void
test_binder_unregister_objects(
    int fd);

void
test_binder_set_destroy(
    int fd,
    gpointer ptr,
    GDestroyNotify destroy);

void
test_binder_exit_wait(
    const TestOpt* opt,
    GMainLoop* loop);

#endif /* TEST_BINDER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
