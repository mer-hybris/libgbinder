/*
 * Copyright (C) 2020-2021 Jolla Ltd.
 * Copyright (C) 2020-2021 Slava Monich <slava.monich@jolla.com>
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

#include "test_common.h"
#include "gbinder_eventloop_p.h"

static TestOpt test_opt;

static int test_eventloop_timeout_add_called;
static int test_eventloop_callback_new_called;
static int test_eventloop_cleanup_called;

static
gboolean
test_unreached_proc(
    gpointer data)
{
    g_assert_not_reached();
    return G_SOURCE_CONTINUE;
}

static
void
test_quit_cb(
    gpointer data)
{
    g_main_loop_quit((GMainLoop*)data);
}

/*==========================================================================*
 * Test event loop integration
 *==========================================================================*/

static
GBinderEventLoopTimeout*
test_eventloop_timeout_add(
    guint interval,
    GSourceFunc func,
    gpointer data)
{
    test_eventloop_timeout_add_called++;
    return NULL;
}

static
void
test_eventloop_timeout_remove(
    GBinderEventLoopTimeout* timeout)
{
    g_assert_not_reached();
}

static
GBinderEventLoopCallback*
test_eventloop_callback_new(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify destroy)
{
    test_eventloop_callback_new_called++;
    return NULL;
}

static
void
test_eventloop_callback_ref(
    GBinderEventLoopCallback* cb)
{
    g_assert_not_reached();
}

static
void
test_eventloop_callback_unref(
    GBinderEventLoopCallback* cb)
{
    g_assert_not_reached();
}

static
void
test_eventloop_callback_schedule(
    GBinderEventLoopCallback* cb)
{
    g_assert_not_reached();
}

static
void
test_eventloop_callback_cancel(
    GBinderEventLoopCallback* cb)
{
    g_assert_not_reached();
}

static
void
test_eventloop_cleanup(
    void)
{
    test_eventloop_cleanup_called++;
}

static const GBinderEventLoopIntegration test_eventloop = {
    test_eventloop_timeout_add,
    test_eventloop_timeout_remove,
    test_eventloop_callback_new,
    test_eventloop_callback_ref,
    test_eventloop_callback_unref,
    test_eventloop_callback_schedule,
    test_eventloop_callback_cancel,
    test_eventloop_cleanup
};

/*==========================================================================*
 * replace
 *==========================================================================*/

static
void
test_replace(
    void)
{
    test_eventloop_timeout_add_called = 0;
    test_eventloop_callback_new_called = 0;
    test_eventloop_cleanup_called = 0;

    gbinder_eventloop_set(NULL);
    gbinder_eventloop_set(&test_eventloop);

    g_assert(!gbinder_timeout_add(0, test_unreached_proc, NULL));
    g_assert_cmpint(test_eventloop_timeout_add_called, == ,1);
    g_assert(!gbinder_idle_add(test_unreached_proc, NULL));
    g_assert_cmpint(test_eventloop_timeout_add_called, == ,2);
    gbinder_timeout_remove(NULL);
    g_assert(!gbinder_idle_callback_new(NULL, NULL, NULL));
    g_assert_cmpint(test_eventloop_callback_new_called, == ,1);
    g_assert(!gbinder_idle_callback_ref(NULL));
    gbinder_idle_callback_unref(NULL);
    gbinder_idle_callback_schedule(NULL);
    gbinder_idle_callback_cancel(NULL);

    gbinder_eventloop_set(NULL);
    g_assert_cmpint(test_eventloop_cleanup_called, == ,1);
}

/*==========================================================================*
 * idle
 *==========================================================================*/

static
gboolean
test_quit_func(
    gpointer data)
{
    g_main_loop_quit((GMainLoop*)data);
    return G_SOURCE_REMOVE;
}

static
void
test_idle(
    void)
{
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    gbinder_eventloop_set(NULL);
    g_assert(gbinder_idle_add(test_quit_func, loop));
    test_run(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * timeout
 *==========================================================================*/

static
void
test_timeout(
    void)
{
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    gbinder_eventloop_set(NULL);
    g_assert(gbinder_timeout_add(10, test_quit_func, loop));
    test_run(&test_opt, loop);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * callback
 *==========================================================================*/

static
void
test_callback(
    void)
{
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderEventLoopCallback* cb;

    gbinder_eventloop_set(NULL);
    cb = gbinder_idle_callback_new(test_quit_cb, loop, NULL);
    g_assert(gbinder_idle_callback_ref(cb) == cb);
    gbinder_idle_callback_unref(cb);
    gbinder_idle_callback_schedule(cb);
    test_run(&test_opt, loop);
    gbinder_idle_callback_unref(cb);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * invoke
 *==========================================================================*/

static
void
test_invoke(
    void)
{
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    gbinder_eventloop_set(NULL);
    gbinder_idle_callback_invoke_later(test_quit_cb, loop, NULL);
    test_run(&test_opt, loop);

    gbinder_eventloop_set(NULL);
    gbinder_idle_callback_invoke_later(NULL, loop, test_quit_cb);
    test_run(&test_opt, loop);

    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/eventloop/" t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("replace"), test_replace);
    g_test_add_func(TEST_("idle"), test_idle);
    g_test_add_func(TEST_("timeout"), test_timeout);
    g_test_add_func(TEST_("callback"), test_callback);
    g_test_add_func(TEST_("invoke"), test_invoke);
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
