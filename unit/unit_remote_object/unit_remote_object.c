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

#include "test_binder.h"

#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_object_registry.h"
#include "gbinder_remote_object_p.h"

#include <gutil_log.h>

static TestOpt test_opt;

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!gbinder_remote_object_new(NULL, 0, FALSE));
    g_assert(!gbinder_remote_object_ref(NULL));
    g_assert(!gbinder_remote_object_ipc(NULL));
    gbinder_remote_object_unref(NULL);
    g_assert(gbinder_remote_object_is_dead(NULL));
    g_assert(!gbinder_remote_object_add_death_handler(NULL, NULL, NULL));
    gbinder_remote_object_remove_handler(NULL, 0);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    GBinderRemoteObject* obj1 = gbinder_object_registry_get_remote(reg,1,TRUE);
    GBinderRemoteObject* obj2 = gbinder_object_registry_get_remote(reg,2,TRUE);

    g_assert(obj1);
    g_assert(obj2);
    g_assert(obj1->handle == 1u);
    g_assert(obj2->handle == 2u);
    g_assert(gbinder_remote_object_ipc(obj1) == ipc);
    g_assert(gbinder_remote_object_ipc(obj2) == ipc);
    g_assert(!gbinder_remote_object_is_dead(obj1));
    g_assert(gbinder_remote_object_reanimate(obj1));
    g_assert(gbinder_remote_object_ref(obj1) == obj1);
    gbinder_remote_object_unref(obj1); /* Compensate the above reference */
    g_assert(!gbinder_remote_object_add_death_handler(obj1, NULL, NULL));
    g_assert(gbinder_object_registry_get_remote(reg, 1, FALSE) == obj1);
    gbinder_remote_object_unref(obj1); /* Compensate the above reference */
    gbinder_remote_object_unref(obj1);
    gbinder_remote_object_unref(obj2);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
}

/*==========================================================================*
 * dead
 *==========================================================================*/

static
void
test_dead_done(
    GBinderRemoteObject* obj,
    void* user_data)
{
    GVERBOSE_("");
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_dead_run(
    void)
{
    const guint h = 1;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);
    GBinderRemoteObject* obj = gbinder_object_registry_get_remote(reg, h, TRUE);
    gulong id = gbinder_remote_object_add_death_handler
        (obj, test_dead_done, loop);

    test_binder_br_dead_binder(fd, ANY_THREAD, h);
    test_run(&test_opt, loop);
    g_assert(gbinder_remote_object_is_dead(obj));

    gbinder_remote_object_remove_handler(obj, id);
    gbinder_remote_object_remove_handler(obj, 0); /* has no effect */
    gbinder_remote_object_unref(obj);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_dead(
    void)
{
    test_run_in_context(&test_opt, test_dead_run);
}

/*==========================================================================*
 * dead_acquired
 *
 * BC_DEAD_BINDER_DONE must reach the driver before the last reference
 * to the dead node is released, otherwise the kernel has already freed
 * the death notification.
 *==========================================================================*/

static
void
test_dead_acquired_run(
    void)
{
    const guint h = 1;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);
    GBinderRemoteObject* obj = gbinder_object_registry_get_remote(reg, h,
        REMOTE_REGISTRY_CAN_CREATE_AND_ACQUIRE);
    gulong id = gbinder_remote_object_add_death_handler
        (obj, test_dead_done, loop);

    test_binder_br_dead_binder(fd, ANY_THREAD, h);
    test_run(&test_opt, loop);
    g_assert(gbinder_remote_object_is_dead(obj));
    g_assert_cmpuint(test_binder_stray_dead_binder_done(fd), == ,0);

    gbinder_remote_object_remove_handler(obj, id);
    gbinder_remote_object_unref(obj);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_dead_acquired(
    void)
{
    test_run_in_context(&test_opt, test_dead_acquired_run);
}

/*==========================================================================*
 * recreate
 *
 * A new object is created for the handle after the old one has been
 * removed from the registry but before it has been finalized. The old
 * one must not take the new one's death notification with it.
 *==========================================================================*/

typedef struct test_recreate {
    GBinderObjectRegistry* reg;
    guint32 handle;
    GBinderRemoteObject* fresh;
} TestRecreate;

static
void
test_recreate_weak_notify(
    gpointer user_data,
    GObject* old)
{
    TestRecreate* test = user_data;

    /* The old object is being disposed of, and is no longer registered */
    g_assert(!test->fresh);
    test->fresh = gbinder_object_registry_get_remote(test->reg, test->handle,
        REMOTE_REGISTRY_CAN_CREATE_AND_ACQUIRE);
    g_assert(test->fresh);
    g_assert(test->fresh != (gpointer)old);
}

static
void
test_recreate_run(
    void)
{
    const guint h = 1;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    const int fd = gbinder_driver_fd(ipc->driver);
    GBinderRemoteObject* old;
    GBinderRemoteObject* found;
    TestRecreate test;

    memset(&test, 0, sizeof(test));
    test.reg = gbinder_ipc_object_registry(ipc);
    test.handle = h;
    old = gbinder_object_registry_get_remote(test.reg, h,
        REMOTE_REGISTRY_CAN_CREATE_AND_ACQUIRE);
    g_assert(test_binder_death_requested(fd, h));

    g_object_weak_ref(G_OBJECT(old), test_recreate_weak_notify, &test);
    gbinder_remote_object_unref(old);
    g_assert(test.fresh);

    /* The new object is registered and still has its death notification */
    found = gbinder_object_registry_get_remote(test.reg, h,
        REMOTE_REGISTRY_DONT_CREATE);
    g_assert(found == test.fresh);
    gbinder_remote_object_unref(found);
    g_assert(test_binder_death_requested(fd, h));

    gbinder_remote_object_unref(test.fresh);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_recreate(
    void)
{
    test_run_in_context(&test_opt, test_recreate_run);
}

/*==========================================================================*
 * reuse
 *
 * When a dead object finally goes away, it must not evict a newer object
 * registered for the same (reused) handle.
 *==========================================================================*/

static
void
test_reuse_run(
    void)
{
    const guint h = 1;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderObjectRegistry* reg = gbinder_ipc_object_registry(ipc);
    const int fd = gbinder_driver_fd(ipc->driver);
    GBinderRemoteObject* old = gbinder_object_registry_get_remote(reg, h, TRUE);
    GBinderRemoteObject* fresh;
    GBinderRemoteObject* found;
    gulong id = gbinder_remote_object_add_death_handler
        (old, test_dead_done, loop);

    test_binder_br_dead_binder(fd, ANY_THREAD, h);
    test_run(&test_opt, loop);
    g_assert(gbinder_remote_object_is_dead(old));
    gbinder_remote_object_remove_handler(old, id);

    /* Kernel hands out the same handle number for a new node */
    fresh = gbinder_object_registry_get_remote(reg, h, TRUE);
    g_assert(fresh != old);
    g_assert(!gbinder_remote_object_is_dead(fresh));

    /* User finally drops the dead object */
    gbinder_remote_object_unref(old);

    found = gbinder_object_registry_get_remote(reg, h, FALSE);
    GDEBUG("fresh=%p found=%p", fresh, found);
    g_assert(found == fresh);
    gbinder_remote_object_unref(found);
    gbinder_remote_object_unref(fresh);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    g_main_loop_unref(loop);
}

static
void
test_reuse(
    void)
{
    test_run_in_context(&test_opt, test_reuse_run);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/remote_object/"

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "null", test_null);
    g_test_add_func(TEST_PREFIX "basic", test_basic);
    g_test_add_func(TEST_PREFIX "dead", test_dead);
    g_test_add_func(TEST_PREFIX "dead_acquired", test_dead_acquired);
    g_test_add_func(TEST_PREFIX "recreate", test_recreate);
    g_test_add_func(TEST_PREFIX "reuse", test_reuse);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
