/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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
#include "gbinder_servicemanager_p.h"
#include "gbinder_servicepoll.h"
#include "gbinder_rpc_protocol.h"

#include <gutil_strv.h>
#include <gutil_log.h>

#include <errno.h>

static TestOpt test_opt;

static
void
test_setup_ping(
    GBinderIpc* ipc)
{
    const int fd = gbinder_driver_fd(ipc->driver);

    test_binder_br_noop(fd);
    test_binder_br_transaction_complete(fd);
    test_binder_br_reply(fd, 0, 0, NULL);
}

/*==========================================================================*
 * TestServiceManager
 *==========================================================================*/

typedef GBinderServiceManagerClass TestServiceManagerClass;
typedef struct test_servicemanager {
    GBinderServiceManager manager;
    GMutex mutex;
    char** services;
} TestServiceManager;

G_DEFINE_TYPE(TestServiceManager, test_servicemanager,
    GBINDER_TYPE_SERVICEMANAGER)

#define TEST_SERVICEMANAGER_IFACE "android.os.IServiceManager"
#define TEST_TYPE_SERVICEMANAGER (test_servicemanager_get_type())
#define TEST_SERVICEMANAGER(obj) G_TYPE_CHECK_INSTANCE_CAST((obj), \
    TEST_TYPE_SERVICEMANAGER, TestServiceManager)

static
char**
test_servicemanager_list(
    GBinderServiceManager* manager)
{
    char** ret;
    TestServiceManager* self = TEST_SERVICEMANAGER(manager);

    g_mutex_lock(&self->mutex);
    ret = g_strdupv(self->services);
    GDEBUG("%u", gutil_strv_length(ret));
    g_mutex_unlock(&self->mutex);
    return ret;
}

static
GBinderRemoteObject*
test_servicemanager_get_service(
    GBinderServiceManager* manager,
    const char* name,
    int* status)
{
    *status = (-ENOENT);
    return NULL;
}

static
int
test_servicemanager_add_service(
    GBinderServiceManager* manager,
    const char* name,
    GBinderLocalObject* obj)
{
    TestServiceManager* self = TEST_SERVICEMANAGER(manager);

    g_mutex_lock(&self->mutex);
    if (!gutil_strv_contains(self->services, name)) {
        self->services = gutil_strv_add(self->services, name);
    }
    g_mutex_unlock(&self->mutex);
    return GBINDER_STATUS_OK;
}

static
GBINDER_SERVICEMANAGER_NAME_CHECK
test_servicemanager_check_name(
    GBinderServiceManager* manager,
    const char* name)
{
    return name ?
        GBINDER_SERVICEMANAGER_NAME_INVALID :
        GBINDER_SERVICEMANAGER_NAME_OK;
}

static
gboolean
test_servicemanager_watch(
    GBinderServiceManager* manager,
    const char* name)
{
    return TRUE;
}

static
void
test_servicemanager_unwatch(
    GBinderServiceManager* manager,
    const char* name)
{
}

static
void
test_servicemanager_init(
    TestServiceManager* self)
{
    g_mutex_init(&self->mutex);
}

static
void
test_servicemanager_finalize(
    GObject* object)
{
    TestServiceManager* self = TEST_SERVICEMANAGER(object);

    g_mutex_clear(&self->mutex);
    g_strfreev(self->services);
    G_OBJECT_CLASS(test_servicemanager_parent_class)->finalize(object);
}

static
void
test_servicemanager_class_init(
    TestServiceManagerClass* klass)
{
    klass->iface = TEST_SERVICEMANAGER_IFACE;
    klass->default_device = GBINDER_DEFAULT_HWBINDER;
    klass->list = test_servicemanager_list;
    klass->get_service = test_servicemanager_get_service;
    klass->add_service = test_servicemanager_add_service;
    klass->check_name = test_servicemanager_check_name;
    klass->watch = test_servicemanager_watch;
    klass->unwatch = test_servicemanager_unwatch;
    G_OBJECT_CLASS(klass)->finalize = test_servicemanager_finalize;
}

/* Avoid pulling in the actual objects */

GType
gbinder_servicemanager_aidl_get_type()
{
    return TEST_TYPE_SERVICEMANAGER;
}

GType
gbinder_servicemanager_aidl2_get_type()
{
    return TEST_TYPE_SERVICEMANAGER;
}

GType
gbinder_servicemanager_hidl_get_type()
{
    return TEST_TYPE_SERVICEMANAGER;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!gbinder_servicepoll_ref(NULL));
    g_assert(!gbinder_servicepoll_manager(NULL));
    g_assert(!gbinder_servicepoll_is_known_name(NULL, ""));
    g_assert(!gbinder_servicepoll_add_handler(NULL, NULL, NULL));
    gbinder_servicepoll_remove_handler(NULL, 0);
    gbinder_servicepoll_unref(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderServicePoll* weakptr = NULL;
    GBinderServiceManager* manager;
    GBinderServicePoll* poll;

    test_setup_ping(ipc);
    manager = gbinder_servicemanager_new(dev);
    poll = gbinder_servicepoll_new(manager, NULL);
    g_assert(poll);
    g_assert(gbinder_servicepoll_manager(poll) == manager);
    g_assert(!gbinder_servicepoll_is_known_name(poll, "foo"));
    g_assert(!gbinder_servicepoll_add_handler(poll, NULL, NULL));
    gbinder_servicepoll_remove_handler(poll, 0); /* this does nothing */
    gbinder_servicepoll_unref(poll);

    poll = gbinder_servicepoll_new(manager, &weakptr);
    g_assert(poll == weakptr);
    g_assert(poll == gbinder_servicepoll_new(manager, &weakptr));
    gbinder_servicepoll_unref(poll);
    gbinder_servicepoll_unref(poll);

    gbinder_servicemanager_unref(manager);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * notify1
 *==========================================================================*/

static
void
test_notify_proc(
    GBinderServicePoll* poll,
    const char* name_added,
    void* user_data)
{
    GDEBUG("\"%s\" added", name_added);
    if (!g_strcmp0(name_added, "foo")) {
        test_quit_later((GMainLoop*)user_data);
    }
}

static
gboolean
test_notify1_foo(
    gpointer user_data)
{
    TestServiceManager* test = user_data;

    g_mutex_lock(&test->mutex);
    GDEBUG("adding \"foo\"");
    test->services = gutil_strv_add(test->services, "foo");
    g_mutex_unlock(&test->mutex);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_notify1_bar(
    gpointer user_data)
{
    TestServiceManager* test = user_data;

    g_mutex_lock(&test->mutex);
    GDEBUG("adding \"bar\"");
    test->services = gutil_strv_add(test->services, "bar");
    g_mutex_unlock(&test->mutex);
    return G_SOURCE_REMOVE;
}

static
void
test_notify1(
    void)
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderServicePoll* weakptr = NULL;
    GBinderServiceManager* manager;
    TestServiceManager* test;
    GBinderServicePoll* poll;
    gulong id;

    test_setup_ping(ipc);
    manager = gbinder_servicemanager_new(dev);
    test = TEST_SERVICEMANAGER(manager);

    gbinder_servicepoll_interval_ms = 100;
    poll = gbinder_servicepoll_new(manager, &weakptr);
    g_timeout_add(2 * gbinder_servicepoll_interval_ms,
        test_notify1_bar, test);
    g_timeout_add(4 * gbinder_servicepoll_interval_ms,
        test_notify1_foo, test);

    id = gbinder_servicepoll_add_handler(poll, test_notify_proc, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    g_assert(gbinder_servicepoll_is_known_name(poll, "foo"));
    g_assert(gbinder_servicepoll_is_known_name(poll, "bar"));
    gbinder_servicepoll_remove_handler(poll, id);
    gbinder_servicepoll_unref(poll);
    g_assert(!weakptr);
    gbinder_servicemanager_unref(manager);
    gbinder_ipc_unref(ipc);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * notify2
 *==========================================================================*/

static
gboolean
test_notify2_foo(
    gpointer user_data)
{
    TestServiceManager* test = user_data;

    g_mutex_lock(&test->mutex);
    GDEBUG("services = [\"bar\",\"foo\"]");
    g_strfreev(test->services);
    test->services = g_strsplit("bar,bar3,foo", ",", -1);
    g_mutex_unlock(&test->mutex);
    return G_SOURCE_REMOVE;
}

static
gboolean
test_notify2_bar(
    gpointer user_data)
{
    TestServiceManager* test = user_data;

    g_mutex_lock(&test->mutex);
    GDEBUG("services = [\"bar1\",\"bar2\",\"bar3\"]");
    g_strfreev(test->services);
    test->services = g_strsplit("bar1,bar2,bar3", ",", -1);
    g_mutex_unlock(&test->mutex);
    return G_SOURCE_REMOVE;
}

static
void
test_notify2(
    void)
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderServicePoll* weakptr = NULL;
    GBinderServiceManager* manager;
    TestServiceManager* test;
    GBinderServicePoll* poll;
    gulong id;

    test_setup_ping(ipc);
    manager = gbinder_servicemanager_new(dev);
    test = TEST_SERVICEMANAGER(manager);

    gbinder_servicepoll_interval_ms = 100;
    poll = gbinder_servicepoll_new(manager, &weakptr);
    g_timeout_add(2 * gbinder_servicepoll_interval_ms,
        test_notify2_bar, test);
    g_timeout_add(4 * gbinder_servicepoll_interval_ms,
        test_notify2_foo, test);

    /* Reusing test_notify_proc */
    id = gbinder_servicepoll_add_handler(poll, test_notify_proc, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    g_assert(gbinder_servicepoll_is_known_name(poll, "foo"));
    g_assert(gbinder_servicepoll_is_known_name(poll, "bar"));
    g_assert(gbinder_servicepoll_is_known_name(poll, "bar3"));
    g_assert(!gbinder_servicepoll_is_known_name(poll, "bar1"));
    g_assert(!gbinder_servicepoll_is_known_name(poll, "bar2"));
    gbinder_servicepoll_remove_handler(poll, id);
    gbinder_servicepoll_unref(poll);
    g_assert(!weakptr);
    gbinder_servicemanager_unref(manager);
    gbinder_ipc_unref(ipc);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * already_there
 *==========================================================================*/

static
void
test_already_there_proc(
    GBinderServicePoll* poll,
    const char* name_added,
    void* user_data)
{
    g_assert(!g_strcmp0(name_added, "foo"));
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_already_there(
    void)
{
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderServicePoll* weakptr = NULL;
    GBinderServiceManager* manager;
    TestServiceManager* test;
    GBinderServicePoll* poll;
    gulong id;

    test_setup_ping(ipc);
    manager = gbinder_servicemanager_new(dev);
    poll = gbinder_servicepoll_new(manager, &weakptr);
    test = TEST_SERVICEMANAGER(manager);

    test->services = gutil_strv_add(test->services, "foo");
    id = gbinder_servicepoll_add_handler(poll, test_already_there_proc, loop);

    g_assert(id);
    test_run(&test_opt, loop);

    gbinder_servicepoll_remove_handler(poll, id);
    gbinder_servicepoll_unref(poll);
    g_assert(!weakptr);
    gbinder_servicemanager_unref(manager);
    gbinder_ipc_unref(ipc);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/servicepoll/" t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("notify1"), test_notify1);
    g_test_add_func(TEST_("notify2"), test_notify2);
    g_test_add_func(TEST_("already_there"), test_already_there);
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
