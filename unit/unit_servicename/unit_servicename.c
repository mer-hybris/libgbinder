/*
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2019-2020 Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_servicename.h"
#include "gbinder_servicemanager_p.h"
#include "gbinder_local_object.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_driver.h"
#include "gbinder_ipc.h"

#include <gutil_strv.h>
#include <gutil_log.h>

#include <errno.h>

static TestOpt test_opt;

static
void
test_quit(
    GBinderServiceManager* sm,
    void* user_data)
{
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_quit_when_destroyed(
    gpointer loop,
    GObject* obj)
{
    test_quit_later((GMainLoop*)loop);
}

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
    GCond cond;
    GMutex mutex;
    char** services;
    gboolean block_add;
    int add_result;
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
    while (self->block_add) {
        g_cond_wait(&self->cond, &self->mutex);
    }
    g_mutex_unlock(&self->mutex);
    return self->add_result;
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
    g_cond_init(&self->cond);
    g_mutex_init(&self->mutex);
    self->add_result = GBINDER_STATUS_OK;
}

static
void
test_servicemanager_finalize(
    GObject* object)
{
    TestServiceManager* self = TEST_SERVICEMANAGER(object);

    g_cond_clear(&self->cond);
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
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GBinderServiceManager* sm;

    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);

    g_assert(!gbinder_servicename_new(NULL, NULL, NULL));
    g_assert(!gbinder_servicename_new(sm, NULL, NULL));
    g_assert(!gbinder_servicename_ref(NULL));
    gbinder_servicename_unref(NULL);

    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    const char* obj_name = "test";
    const char* dev = GBINDER_DEFAULT_BINDER;
    const char* const ifaces[] = { "interface", NULL };
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj;
    GBinderServiceManager* sm;
    GBinderServiceName* sn;

    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    obj = gbinder_local_object_new(ipc, ifaces, NULL, NULL);
    g_assert(!gbinder_servicename_new(sm, obj, NULL));

    sn = gbinder_servicename_new(sm, obj, obj_name);
    g_assert(sn);
    g_assert(!g_strcmp0(sn->name, obj_name));

    g_assert(gbinder_servicename_ref(sn) == sn);
    gbinder_servicename_unref(sn);

    gbinder_servicename_unref(sn);
    gbinder_local_object_unref(obj);
    gbinder_servicemanager_unref(sm);

    /* We need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_ipc_unref(ipc);
    test_run(&test_opt, loop);

    g_main_loop_unref(loop);
}

/*==========================================================================*
 * present
 *==========================================================================*/

static
void
test_present(
    int add_result)
{
    const char* obj_name = "test";
    const char* const ifaces[] = { "interface", NULL };
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    const int fd = gbinder_driver_fd(ipc->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj;
    GBinderServiceManager* sm;
    GBinderServiceName* sn;
    gulong id;

    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    TEST_SERVICEMANAGER(sm)->add_result = add_result;
    obj = gbinder_local_object_new(ipc, ifaces, NULL, NULL);

    sn = gbinder_servicename_new(sm, obj, obj_name);
    g_assert(sn);
    g_assert(!g_strcmp0(sn->name, obj_name));

    /* Immediately generate death notification (need looper for that) */
    test_binder_br_dead_binder(fd, 0);
    test_binder_set_looper_enabled(fd, TRUE);
    id = gbinder_servicemanager_add_presence_handler(sm, test_quit, loop);
    test_run(&test_opt, loop);

    gbinder_servicename_unref(sn);
    gbinder_local_object_unref(obj);
    gbinder_servicemanager_remove_handler(sm, id);
    gbinder_servicemanager_unref(sm);

    /* We need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_ipc_unref(ipc);
    test_run(&test_opt, loop);

    gbinder_ipc_exit();
    g_main_loop_unref(loop);
}

static
void
test_present_ok(
    void)
{
    test_present(GBINDER_STATUS_OK);
}

static
void
test_present_err(
    void)
{
    test_present(-1);
}

/*==========================================================================*
 * not_present
 *==========================================================================*/

static
void
test_not_present(
    void)
{
    const char* obj_name = "test";
    const char* const ifaces[] = { "interface", NULL };
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    const int fd = gbinder_driver_fd(ipc->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj;
    GBinderServiceManager* sm;
    GBinderServiceName* sn;
    gulong id;

    /* This makes presence detection PING fail */
    test_binder_br_reply_status(fd, -1);
    sm = gbinder_servicemanager_new(dev);
    g_assert(!gbinder_servicemanager_is_present(sm));
    id = gbinder_servicemanager_add_presence_handler(sm, test_quit, loop);
    obj = gbinder_local_object_new(ipc, ifaces, NULL, NULL);

    sn = gbinder_servicename_new(sm, obj, obj_name);
    g_assert(sn);
    g_assert(!g_strcmp0(sn->name, obj_name));

    /* Make the next presence detection PING succeed */
    test_binder_br_transaction_complete_later(fd);
    test_binder_br_reply_later(fd, 0, 0, NULL);
    test_run(&test_opt, loop);

    gbinder_servicename_unref(sn);
    gbinder_local_object_unref(obj);
    gbinder_servicemanager_remove_handler(sm, id);
    gbinder_servicemanager_unref(sm);

    /* We need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_ipc_unref(ipc);
    test_run(&test_opt, loop);

    g_main_loop_unref(loop);
}

/*==========================================================================*
 * cancel
 *==========================================================================*/

static
void
test_cancel(
    void)
{
    const char* obj_name = "test";
    const char* const ifaces[] = { "interface", NULL };
    const char* dev = GBINDER_DEFAULT_BINDER;
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    const int fd = gbinder_driver_fd(ipc->driver);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderLocalObject* obj;
    GBinderServiceManager* sm;
    TestServiceManager* test;
    GBinderServiceName* sn;
    gulong id;

    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    obj = gbinder_local_object_new(ipc, ifaces, NULL, NULL);

    /* Block name add calls */
    test = TEST_SERVICEMANAGER(sm);
    g_mutex_lock(&test->mutex);
    test->block_add = TRUE;
    g_mutex_unlock(&test->mutex);

    /* This adds the name but the call blocks */
    sn = gbinder_servicename_new(sm, obj, obj_name);
    g_assert(sn);
    g_assert(!g_strcmp0(sn->name, obj_name));

    /* Immediately generate death notification (need looper for that) */
    test_binder_br_dead_binder(fd, 0);
    test_binder_set_looper_enabled(fd, TRUE);
    id = gbinder_servicemanager_add_presence_handler(sm, test_quit, loop);
    test_run(&test_opt, loop);

    /* Add call is supposed to be cancelled */
    gbinder_servicename_unref(sn);
    gbinder_local_object_unref(obj);
    gbinder_servicemanager_remove_handler(sm, id);
    gbinder_servicemanager_unref(sm);

    /* Unblock pending add */
    g_mutex_lock(&test->mutex);
    test->block_add = FALSE;
    g_cond_signal(&test->cond);
    g_mutex_unlock(&test->mutex);

    /* We need to wait until GBinderIpc is destroyed */
    GDEBUG("waiting for GBinderIpc to get destroyed");
    g_object_weak_ref(G_OBJECT(ipc), test_quit_when_destroyed, loop);
    gbinder_ipc_unref(ipc);
    test_run(&test_opt, loop);

    gbinder_ipc_exit();
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(test) "/servicename/" test

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("present_ok"), test_present_ok);
    g_test_add_func(TEST_("present_err"), test_present_err);
    g_test_add_func(TEST_("not_present"), test_not_present);
    g_test_add_func(TEST_("cancel"), test_cancel);
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
