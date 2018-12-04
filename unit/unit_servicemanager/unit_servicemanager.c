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

#include "test_common.h"

#include "gbinder_client_p.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_ipc.h"
#include "gbinder_local_object_p.h"
#include "gbinder_servicemanager_p.h"
#include "gbinder_rpc_protocol.h"

#include <gutil_strv.h>
#include <gutil_macros.h>

#include <errno.h>

static TestOpt test_opt;

static
void
test_get_service_func(
    GBinderServiceManager* sm,
    GBinderRemoteObject* obj,
    int status,
    void* user_data)
{
    g_assert(FALSE);
}

static
void
test_add_service_func(
    GBinderServiceManager* sm,
    int status,
    void* user_data)
{
    g_assert(FALSE);
}

static
void
test_registration_func_inc(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data)
{
    int* count = user_data;

    (*count)++;
}

static
GBinderLocalReply*
test_transact_func(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    return NULL;
}

/*==========================================================================*
 * TestServiceManager
 *==========================================================================*/

typedef GBinderServiceManagerClass TestServiceManagerClass;
typedef struct test_servicemanager {
    GBinderServiceManager manager;
    GBinderRemoteObject* remote;
    char** services;
    gboolean reject_name;
} TestServiceManager;

#define TEST_SERVICEMANAGER(obj) \
    G_CAST(obj, TestServiceManager, manager.parent)
#define TEST_SERVICEMANAGER2(obj, type) \
    G_TYPE_CHECK_INSTANCE_CAST((obj), (type), TestServiceManager)

static
char**
test_servicemanager_list(
    GBinderServiceManager* sm)
{
    TestServiceManager* self = TEST_SERVICEMANAGER(sm);

    return g_strdupv(self->services);
}

static
GBinderRemoteObject*
test_servicemanager_get_service(
    GBinderServiceManager* sm,
    const char* name,
    int* status)
{
    TestServiceManager* self = TEST_SERVICEMANAGER(sm);

    if (gutil_strv_contains(self->services, name)) {
        if (!self->remote) {
            self->remote = gbinder_ipc_get_remote_object
                (gbinder_client_ipc(sm->client), 1);
        }
        *status = GBINDER_STATUS_OK;
        return gbinder_remote_object_ref(self->remote);
    } else {
        *status = (-ENOENT);
        return NULL;
    }
}

static
int
test_servicemanager_add_service(
    GBinderServiceManager* sm,
    const char* name,
    GBinderLocalObject* obj)
{
    TestServiceManager* self = TEST_SERVICEMANAGER(sm);

    if (!gutil_strv_contains(self->services, name)) {
        self->services = gutil_strv_add(self->services, name);
    }
    return GBINDER_STATUS_OK;
}

/*==========================================================================*
 * TestHwServiceManager
 *==========================================================================*/

typedef TestServiceManagerClass TestHwServiceManagerClass;
typedef TestServiceManager TestHwServiceManager;

G_DEFINE_TYPE(TestHwServiceManager, test_hwservicemanager,
    GBINDER_TYPE_SERVICEMANAGER)

#define TEST_HWSERVICEMANAGER_HANDLE (0)
#define TEST_HWSERVICEMANAGER_IFACE "android.hidl.manager@1.0::IServiceManager"
#define TEST_TYPE_HWSERVICEMANAGER (test_hwservicemanager_get_type())
#define TEST_IS_HWSERVICEMANAGER(obj) \
    G_TYPE_CHECK_INSTANCE_TYPE(obj, TEST_TYPE_HWSERVICEMANAGER)
#define TEST_HWSERVICEMANAGER(obj) \
    TEST_SERVICEMANAGER2(obj, TEST_TYPE_HWSERVICEMANAGER)

static
GBINDER_SERVICEMANAGER_NAME_CHECK
test_hwservicemanager_check_name(
    GBinderServiceManager* sm,
    const char* name)
{
    TestHwServiceManager* self = TEST_HWSERVICEMANAGER(sm);

    return (!name || self->reject_name) ? 
        GBINDER_SERVICEMANAGER_NAME_INVALID :
        GBINDER_SERVICEMANAGER_NAME_NORMALIZE;
}

static
char*
test_hwservicemanager_normalize_name(
    GBinderServiceManager* self,
    const char* name)
{
    return g_strdup(name);
}

static
gboolean
test_hwservicemanager_watch(
    GBinderServiceManager* manager,
    const char* name)
{
    return TRUE;
}

static
void
test_hwservicemanager_unwatch(
    GBinderServiceManager* manager,
    const char* name)
{
}

static
void
test_hwservicemanager_init(
    TestHwServiceManager* self)
{
}

static
void
test_hwservicemanager_finalize(
    GObject* object)
{
    TestHwServiceManager* self = TEST_HWSERVICEMANAGER(object);

    gbinder_remote_object_unref(self->remote);
    g_strfreev(self->services);
    G_OBJECT_CLASS(test_hwservicemanager_parent_class)->finalize(object);
}

static
void
test_hwservicemanager_class_init(
    TestHwServiceManagerClass* klass)
{
    klass->handle = TEST_HWSERVICEMANAGER_HANDLE;
    klass->iface = TEST_HWSERVICEMANAGER_IFACE;
    klass->default_device = GBINDER_DEFAULT_HWBINDER;
    klass->rpc_protocol = &gbinder_rpc_protocol_hwbinder;
    klass->list = test_servicemanager_list;
    klass->get_service = test_servicemanager_get_service;
    klass->add_service = test_servicemanager_add_service;
    klass->check_name = test_hwservicemanager_check_name;
    klass->normalize_name = test_hwservicemanager_normalize_name;
    klass->watch = test_hwservicemanager_watch;
    klass->unwatch = test_hwservicemanager_unwatch;
    G_OBJECT_CLASS(klass)->finalize = test_hwservicemanager_finalize;
}

GBinderServiceManager*
gbinder_hwservicemanager_new(
    const char* dev)
{
    return gbinder_servicemanager_new_with_type(TEST_TYPE_HWSERVICEMANAGER,
        dev);
}

/*==========================================================================*
 * TestDefServiceManager
 *==========================================================================*/

typedef TestServiceManagerClass TestDefServiceManagerClass;
typedef TestServiceManager TestDefServiceManager;

G_DEFINE_TYPE(TestDefServiceManager, test_defservicemanager,
    GBINDER_TYPE_SERVICEMANAGER)

#define TEST_DEFSERVICEMANAGER_HANDLE (0)
#define TEST_DEFSERVICEMANAGER_IFACE "android.os.IServiceManager"
#define TEST_TYPE_DEFSERVICEMANAGER (test_defservicemanager_get_type())
#define TEST_IS_DEFSERVICEMANAGER(obj) \
    G_TYPE_CHECK_INSTANCE_TYPE(obj, TEST_TYPE_DEFSERVICEMANAGER)
#define TEST_DEFSERVICEMANAGER(obj) \
    TEST_SERVICEMANAGER2(obj, TEST_TYPE_DEFSERVICEMANAGER)

static
GBINDER_SERVICEMANAGER_NAME_CHECK
test_defservicemanager_check_name(
    GBinderServiceManager* sm,
    const char* name)
{
    TestDefServiceManager* self = TEST_DEFSERVICEMANAGER(sm);

    return (!name || self->reject_name) ? 
        GBINDER_SERVICEMANAGER_NAME_INVALID :
        GBINDER_SERVICEMANAGER_NAME_OK;
}

static
gboolean
test_defservicemanager_watch(
    GBinderServiceManager* manager,
    const char* name)
{
    return FALSE;
}

static
void
test_defservicemanager_init(
    TestDefServiceManager* self)
{
}

static
void
test_defservicemanager_finalize(
    GObject* object)
{
    TestDefServiceManager* self = TEST_DEFSERVICEMANAGER(object);

    gbinder_remote_object_unref(self->remote);
    g_strfreev(self->services);
    G_OBJECT_CLASS(test_defservicemanager_parent_class)->finalize(object);
}

static
void
test_defservicemanager_class_init(
    TestDefServiceManagerClass* klass)
{
    klass->handle = TEST_DEFSERVICEMANAGER_HANDLE;
    klass->iface = TEST_DEFSERVICEMANAGER_IFACE;
    klass->default_device = GBINDER_DEFAULT_BINDER;
    klass->rpc_protocol = &gbinder_rpc_protocol_binder;
    klass->list = test_servicemanager_list;
    klass->get_service = test_servicemanager_get_service;
    klass->add_service = test_servicemanager_add_service;
    klass->check_name = test_defservicemanager_check_name;
    klass->watch = test_defservicemanager_watch;
    G_OBJECT_CLASS(klass)->finalize = test_defservicemanager_finalize;
}

GBinderServiceManager*
gbinder_defaultservicemanager_new(
    const char* dev)
{
    return gbinder_servicemanager_new_with_type(TEST_TYPE_DEFSERVICEMANAGER,
        dev);
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!gbinder_servicemanager_new_with_type(0, NULL));
    g_assert(!gbinder_servicemanager_new_local_object(NULL, NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_ref(NULL));
    g_assert(!gbinder_servicemanager_list(NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_list_sync(NULL));
    g_assert(!gbinder_servicemanager_get_service(NULL, NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_get_service_sync(NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_add_service(NULL, NULL, NULL, NULL, NULL));
    g_assert(gbinder_servicemanager_add_service_sync(NULL, NULL, NULL) ==
        (-EINVAL));
    g_assert(!gbinder_servicemanager_add_registration_handler(NULL, NULL,
        NULL, NULL));
    gbinder_servicemanager_remove_handler(NULL, 0);
    gbinder_servicemanager_cancel(NULL, 0);
    gbinder_servicemanager_unref(NULL);
}

/*==========================================================================*
 * invalid
 *==========================================================================*/

static
void
test_invalid(
    void)
{
    int status = 0;
    GBinderServiceManager* sm =
        gbinder_servicemanager_new(GBINDER_DEFAULT_HWBINDER);

    g_assert(!gbinder_servicemanager_new_with_type(GBINDER_TYPE_LOCAL_OBJECT,
        NULL));
    g_assert(TEST_IS_HWSERVICEMANAGER(sm));
    g_assert(!gbinder_servicemanager_list(sm, NULL, NULL));
    g_assert(!gbinder_servicemanager_get_service(sm, "foo", NULL, NULL));
    g_assert(!gbinder_servicemanager_get_service(sm, NULL,
        test_get_service_func, NULL));
    g_assert(!gbinder_servicemanager_get_service_sync(sm, NULL, NULL));
    g_assert(!gbinder_servicemanager_get_service_sync(sm, NULL, &status));
    g_assert(status == (-EINVAL));
    g_assert(!gbinder_servicemanager_add_service(sm, "foo", NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_add_service(sm, NULL, NULL,
        test_add_service_func, NULL));
    g_assert(gbinder_servicemanager_add_service_sync(sm, NULL, NULL) ==
        (-EINVAL));
    g_assert(gbinder_servicemanager_add_service_sync(sm, "foo", NULL) ==
        (-EINVAL));
    g_assert(!gbinder_servicemanager_add_registration_handler(sm, NULL, NULL,
        NULL));

    gbinder_servicemanager_cancel(sm, 0);
    gbinder_servicemanager_remove_handler(sm, 0);
    gbinder_servicemanager_unref(sm);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{ 
    GBinderServiceManager* sm =
        gbinder_servicemanager_new(GBINDER_DEFAULT_HWBINDER);
    GBinderLocalObject* obj;

    g_assert(sm);
    obj = gbinder_servicemanager_new_local_object(sm, "foo.bar",
        test_transact_func, NULL);
    g_assert(obj);
    gbinder_local_object_unref(obj);

    g_assert(gbinder_servicemanager_ref(sm) == sm);
    gbinder_servicemanager_unref(sm);
    gbinder_servicemanager_unref(sm);
}

/*==========================================================================*
 * reuse
 *==========================================================================*/

static
void
test_reuse(
    void)
{ 
    GBinderServiceManager* m1 =
        gbinder_servicemanager_new(GBINDER_DEFAULT_BINDER);
    GBinderServiceManager* m2 =
        gbinder_servicemanager_new(GBINDER_DEFAULT_BINDER);
    GBinderServiceManager* vnd1 =
        gbinder_servicemanager_new("/dev/vpnbinder");
    GBinderServiceManager* vnd2 =
        gbinder_servicemanager_new("/dev/vpnbinder");
    GBinderServiceManager* hw1 =
        gbinder_servicemanager_new(GBINDER_DEFAULT_HWBINDER);
    GBinderServiceManager* hw2 =
        gbinder_servicemanager_new(GBINDER_DEFAULT_HWBINDER);

    g_assert(m1);
    g_assert(m1 == m2);

    g_assert(vnd1);
    g_assert(vnd1 == vnd2);
    g_assert(vnd1 != m1);

    g_assert(hw1);
    g_assert(hw1 == hw2);
    g_assert(hw1 != m1);
    g_assert(hw1 != vnd1);

    gbinder_servicemanager_unref(m1);
    gbinder_servicemanager_unref(m2);
    gbinder_servicemanager_unref(vnd1);
    gbinder_servicemanager_unref(vnd2);
    gbinder_servicemanager_unref(hw1);
    gbinder_servicemanager_unref(hw2);
}

/*==========================================================================*
 * notify
 *==========================================================================*/

static
void
test_notify_type(
    GType t)
{
    GBinderServiceManager* sm = gbinder_servicemanager_new_with_type(t, NULL);
    TestHwServiceManager* test = TEST_SERVICEMANAGER2(sm, t);
    const char* name = "foo";
    int count = 0;
    gulong id1 = gbinder_servicemanager_add_registration_handler(sm, name,
        test_registration_func_inc, &count);
    gulong id2 = gbinder_servicemanager_add_registration_handler(sm, name,
        test_registration_func_inc, &count);

    g_assert(id1 && id2);
    test->services = gutil_strv_add(test->services, name);
    gbinder_servicemanager_service_registered(sm, name);
    g_assert(count == 2);
    count = 0;

    /* Nothing is going to happen if the name get rejected by the class */
    test->reject_name = TRUE;
    g_assert(!gbinder_servicemanager_add_registration_handler(sm, name,
        test_registration_func_inc, &count));
    gbinder_servicemanager_service_registered(sm, name);
    g_assert(!count);

    gbinder_servicemanager_remove_handler(sm, id1);
    gbinder_servicemanager_remove_handler(sm, id2);
    gbinder_servicemanager_unref(sm);
}

static
void
test_notify(
    void)
{
    test_notify_type(TEST_TYPE_HWSERVICEMANAGER);
    test_notify_type(TEST_TYPE_DEFSERVICEMANAGER);
}

/*==========================================================================*
 * list
 *==========================================================================*/

static
gboolean
test_list_func(
    GBinderServiceManager* sm,
    char** services,
    void* user_data)
{
    TestHwServiceManager* test = TEST_SERVICEMANAGER(sm);

    g_assert(gutil_strv_equal(test->services, services));
    test_quit_later((GMainLoop*)user_data);
    return FALSE;
}

static
void
test_list(
    void)
{
    GBinderServiceManager* sm = gbinder_servicemanager_new(NULL);
    TestHwServiceManager* test = TEST_SERVICEMANAGER(sm);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    char** list;
    gulong id;

    test->services = gutil_strv_add(test->services, "foo");
    list = gbinder_servicemanager_list_sync(sm);
    g_assert(gutil_strv_equal(test->services, list));
    g_strfreev(list);

    id = gbinder_servicemanager_list(sm, test_list_func, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    gbinder_servicemanager_unref(sm);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * get
 *==========================================================================*/

static
void
test_get_func(
    GBinderServiceManager* sm,
    GBinderRemoteObject* obj,
    int status,
    void* user_data)
{
    g_assert(status == GBINDER_STATUS_OK);
    g_assert(obj);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_get(
    void)
{
    GBinderServiceManager* sm = gbinder_servicemanager_new(NULL);
    TestHwServiceManager* test = TEST_SERVICEMANAGER(sm);
    int status = -1;
    GBinderLocalObject* obj =
        gbinder_servicemanager_new_local_object(sm, "foo.bar",
            test_transact_func, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id;

    /* Add a service */
    g_assert(obj);
    g_assert(gbinder_servicemanager_add_service_sync(sm, "foo", obj) ==
        GBINDER_STATUS_OK);
    gbinder_local_object_unref(obj);
    g_assert(gutil_strv_contains(test->services, "foo"));

    /* And get it back */
    g_assert(gbinder_servicemanager_get_service_sync(sm, "foo", &status));
    g_assert(status == GBINDER_STATUS_OK);

    /* Wrong name */
    g_assert(!gbinder_servicemanager_get_service_sync(sm, "bar", &status));
    g_assert(status == (-ENOENT));

    /* Get it asynchronously */
    id = gbinder_servicemanager_get_service(sm, "foo", test_get_func, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    gbinder_servicemanager_unref(sm);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * add
 *==========================================================================*/

static
void
test_add_func(
    GBinderServiceManager* sm,
    int status,
    void* user_data)
{
    g_assert(status == GBINDER_STATUS_OK);
    test_quit_later((GMainLoop*)user_data);
}

static
void
test_add(
    void)
{
    GBinderServiceManager* sm = gbinder_servicemanager_new(NULL);
    TestHwServiceManager* test = TEST_SERVICEMANAGER(sm);
    GBinderLocalObject* obj =
        gbinder_servicemanager_new_local_object(sm, "foo.bar",
            test_transact_func, NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    gulong id = gbinder_servicemanager_add_service(sm, "foo", obj,
        test_add_func, loop);

    g_assert(id);
    test_run(&test_opt, loop);
    g_assert(gutil_strv_contains(test->services, "foo"));

    gbinder_servicemanager_unref(sm);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/servicemanager/" t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("invalid"), test_invalid);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("reuse"), test_reuse);
    g_test_add_func(TEST_("notify"), test_notify);
    g_test_add_func(TEST_("list"), test_list);
    g_test_add_func(TEST_("get"), test_get);
    g_test_add_func(TEST_("add"), test_add);
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
