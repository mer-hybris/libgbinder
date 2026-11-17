/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 * Copyright (C) 2018-2026 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2020 Jolla Ltd.
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

#include "gbinder_config.h"
#include "gbinder_eventloop.h"
#include "gbinder_rpc_protocol.h"

#include <gutil_log.h>

#include <unistd.h>

typedef struct test_quit_later_data{
    GMainLoop* loop;
    guint n;
} TestQuitLaterData;

typedef struct test_context_data{
    GMainLoop* loop;
    GTestDataFunc func;
    gconstpointer param;
} TestContextData;

static
void
test_quit_later_n_free(
    gpointer user_data)
{
    TestQuitLaterData* data = user_data;

    g_main_loop_unref(data->loop);
    g_free(data);
}

static
gboolean
test_quit_later_n_func(
    gpointer user_data)
{
    TestQuitLaterData* data = user_data;

    if (data->n > 0) {
        data->n--;
        return G_SOURCE_CONTINUE;
    } else {
        g_main_loop_quit(data->loop);
        return G_SOURCE_REMOVE;
    }
}

void
test_quit_later_n(
    GMainLoop* loop,
    guint n)
{
    TestQuitLaterData* data = g_new0(TestQuitLaterData, 1);

    data->loop = g_main_loop_ref(loop);
    data->n = n;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, test_quit_later_n_func, data,
        test_quit_later_n_free);
}

static
gboolean
test_quit_later_cb(
    gpointer data)
{
    g_main_loop_quit((GMainLoop*)data);
    return G_SOURCE_REMOVE;
}

void
test_quit_later(
    GMainLoop* loop)
{
    g_idle_add(test_quit_later_cb, loop);
}

static
void
test_run_in_context_cb(
    gconstpointer param)
{
    ((GTestFunc)param)();
}

void
test_run_in_context(
    const TestOpt* opt,
    GTestFunc func)
{
    test_run_in_context_param(opt, test_run_in_context_cb, func);
}

static
gboolean
test_run_in_context_param_cb(
    gpointer data)
{
    const TestContextData* test = data;

    test->func(test->param);
    g_main_loop_quit(test->loop);
    return G_SOURCE_REMOVE;
}

void
test_run_in_context_param(
    const TestOpt* opt,
    GTestDataFunc func,
    gconstpointer param)
{
    TestContextData test;

    test.loop = g_main_loop_new(NULL, FALSE);
    test.func = func;
    test.param = param;

    /*
     * This makes sure that we own the context for the entire duration
     * of the test. That prevents many race conditions - all callbacks
     * that are supposed to be invoked on the main thread, are actually
     * invoked on the main thread (rather than a random worker thread
     * which happens to acquire the context).
     */
    g_idle_add(test_run_in_context_param_cb, &test);
    test_run(opt, test.loop);
    g_main_loop_unref(test.loop);
}

void
test_run(
    const TestOpt* opt,
    GMainLoop* loop)
{
    if (opt->flags & TEST_FLAG_DEBUG) {
        g_main_loop_run(loop);
    } else {
        static guint depth;

        /* SIGALRM terminates the process even if the main thread is stuck.
         * Nested loops must neither extend nor cancel the outer timeout. */
        if (!depth++) {
            alarm(TEST_TIMEOUT_SEC);
        }
        g_main_loop_run(loop);
        if (!--depth) {
            alarm(0);
        }
    }
}

/*==========================================================================*
 * Event loop integration counting live callbacks
 *==========================================================================*/

typedef struct test_counting_timeout {
    GBinderEventLoopTimeout timeout;
    GSourceFunc func;
    gpointer data;
    guint id;
} TestCountingTimeout;

typedef struct test_counting_callback {
    GBinderEventLoopCallback cb;
    gint refcount;
    guint id;
    GBinderEventLoopCallbackFunc func;
    gpointer data;
    GDestroyNotify finalize;
} TestCountingCallback;

static const GBinderEventLoopIntegration test_counting_eventloop;
static gint test_counting_callbacks = 0;
G_LOCK_DEFINE_STATIC(test_counting);

static
gboolean
test_counting_timeout_proc(
    gpointer user_data)
{
    TestCountingTimeout* t = user_data;

    return t->func(t->data);
}

static
GBinderEventLoopTimeout*
test_counting_timeout_add(
    guint interval,
    GSourceFunc func,
    gpointer data)
{
    TestCountingTimeout* t = g_new0(TestCountingTimeout, 1);

    t->timeout.eventloop = &test_counting_eventloop;
    t->func = func;
    t->data = data;
    t->id = g_timeout_add_full(G_PRIORITY_DEFAULT, interval,
        test_counting_timeout_proc, t, g_free);
    return &t->timeout;
}

static
void
test_counting_timeout_remove(
    GBinderEventLoopTimeout* timeout)
{
    g_source_remove(((TestCountingTimeout*)timeout)->id);
}

static
GBinderEventLoopCallback*
test_counting_callback_new(
    GBinderEventLoopCallbackFunc func,
    gpointer data,
    GDestroyNotify finalize)
{
    TestCountingCallback* c = g_new0(TestCountingCallback, 1);

    c->cb.eventloop = &test_counting_eventloop;
    c->refcount = 1;
    c->func = func;
    c->data = data;
    c->finalize = finalize;
    g_atomic_int_inc(&test_counting_callbacks);
    return &c->cb;
}

static
void
test_counting_callback_ref(
    GBinderEventLoopCallback* cb)
{
    g_atomic_int_inc(&((TestCountingCallback*)cb)->refcount);
}

static
void
test_counting_callback_unref(
    GBinderEventLoopCallback* cb)
{
    TestCountingCallback* c = (TestCountingCallback*)cb;

    if (g_atomic_int_dec_and_test(&c->refcount)) {
        /* Pending callbacks hold a reference, so this one isn't pending */
        if (c->finalize) {
            c->finalize(c->data);
        }
        g_free(c);
        g_atomic_int_add(&test_counting_callbacks, -1);
    }
}

static
gboolean
test_counting_callback_proc(
    gpointer user_data)
{
    TestCountingCallback* c = user_data;

    G_LOCK(test_counting);
    c->id = 0;
    G_UNLOCK(test_counting);
    c->func(c->data);
    return G_SOURCE_REMOVE;
}

static
void
test_counting_callback_pending_done(
    gpointer user_data)
{
    /* GLib calls this after the dispatch (if any) has completed */
    test_counting_callback_unref(user_data);
}

static
void
test_counting_callback_schedule(
    GBinderEventLoopCallback* cb)
{
    TestCountingCallback* c = (TestCountingCallback*)cb;

    G_LOCK(test_counting);
    if (!c->id) {
        test_counting_callback_ref(cb);
        c->id = g_idle_add_full(G_PRIORITY_DEFAULT,
            test_counting_callback_proc, c,
            test_counting_callback_pending_done);
    }
    G_UNLOCK(test_counting);
}

static
void
test_counting_callback_cancel(
    GBinderEventLoopCallback* cb)
{
    TestCountingCallback* c = (TestCountingCallback*)cb;
    guint id;

    G_LOCK(test_counting);
    id = c->id;
    c->id = 0;
    G_UNLOCK(test_counting);
    if (id) {
        /* The pending reference goes with the source */
        g_source_remove(id);
    }
}

static
void
test_counting_cleanup(
    void)
{
}

static const GBinderEventLoopIntegration test_counting_eventloop = {
    test_counting_timeout_add,
    test_counting_timeout_remove,
    test_counting_callback_new,
    test_counting_callback_ref,
    test_counting_callback_unref,
    test_counting_callback_schedule,
    test_counting_callback_cancel,
    test_counting_cleanup
};

void
test_eventloop_counting_set(
    void)
{
    gbinder_eventloop_set(&test_counting_eventloop);
}

int
test_eventloop_counting_callbacks(
    void)
{
    return g_atomic_int_get(&test_counting_callbacks);
}

void
test_init(
    TestOpt* opt,
    int argc,
    char* argv[])
{
    const char* sep1;
    const char* sep2;
    int i;

    memset(opt, 0, sizeof(*opt));
    for (i=1; i<argc; i++) {
        const char* arg = argv[i];
        if (!strcmp(arg, "-d") || !strcmp(arg, "--debug")) {
            opt->flags |= TEST_FLAG_DEBUG;
        } else if (!strcmp(arg, "-v")) {
            GTestConfig* config = (GTestConfig*)g_test_config_vars;
            config->test_verbose = TRUE;
        } else {
            GWARN("Unsupported command line option %s", arg);
        }
    }

    /* Setup logging */
    sep1 = strrchr(argv[0], '/');
    sep2 = strrchr(argv[0], '\\');
    gutil_log_default.name = (sep1 && sep2) ? (MAX(sep1, sep2) + 1) :
        sep1 ? (sep1 + 1) : sep2 ? (sep2 + 1) : argv[0];
    gutil_log_default.level = g_test_verbose() ?
        GLOG_LEVEL_VERBOSE : GLOG_LEVEL_NONE;
}

/*
 * Unit tests shouldn't depend on the system gbinder config. This is
 * achieved by pointing gbinder_config_file to a non-existent file
 * and gbinder_config_dir to an empty directory (where individual
 * tests may create their own config files when they need it)
 */
void
test_config_init(
    TestConfig* config,
    const char* template)
{
    config->config_dir = g_dir_make_tmp("gbinder-test-driver-XXXXXX", NULL);
    g_assert(config->config_dir);
    config->non_existent_config_file = g_build_filename(config->config_dir,
        "non-existent.conf", NULL);

    /* Point gbinder_config_file to a non-existent file */
    config->default_config_dir = gbinder_config_dir;
    config->default_config_file = gbinder_config_file;
    gbinder_config_dir = config->config_dir;
    gbinder_config_file = config->non_existent_config_file;

    /* Reset the state */
    gbinder_rpc_protocol_exit();
    gbinder_config_exit();
}

void
test_config_cleanup(
    TestConfig* config)
{
    gbinder_config_dir = config->default_config_dir;
    gbinder_config_file = config->default_config_file;
    remove(config->config_dir);
    g_free(config->config_dir);
    g_free(config->non_existent_config_file);
    memset(config, 0, sizeof(*config));

    /* Reset the state */
    gbinder_rpc_protocol_exit();
    gbinder_config_exit();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
