/*
 * Copyright (C) 2018-2024 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2022 Jolla Ltd.
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
#include "gbinder_client_p.h"
#include "gbinder_config.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_ipc.h"
#include "gbinder_local_object_p.h"
#include "gbinder_servicemanager_p.h"
#include "gbinder_object_registry.h"

#include <gutil_strv.h>
#include <gutil_macros.h>
#include <gutil_log.h>

#include <errno.h>

static TestOpt test_opt;
static const char TMP_DIR_TEMPLATE[] = "gbinder-test-servicemanager-XXXXXX";

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

static
void
test_inc(
    GBinderServiceManager* sm,
    void* user_data)
{
    (*((int*)user_data))++;
}

static
void
test_reg_inc(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data)
{
    GVERBOSE_("\"%s\"", name);
    (*((int*)user_data))++;
}

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
test_setup_ping(
    GBinderIpc* ipc)
{
    const int fd = gbinder_driver_fd(ipc->driver);

    test_binder_br_noop(fd, THIS_THREAD);
    test_binder_br_transaction_complete(fd, THIS_THREAD);
    test_binder_br_reply(fd, THIS_THREAD, 0, 0, NULL);
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
    GBinderServiceManager* sm,
    const GBinderIpcSyncApi* api)
{
    TestServiceManager* self = TEST_SERVICEMANAGER(sm);

    return g_strdupv(self->services);
}

static
GBinderRemoteObject*
test_servicemanager_get_service(
    GBinderServiceManager* sm,
    const char* name,
    int* status,
    const GBinderIpcSyncApi* api)
{
    TestServiceManager* self = TEST_SERVICEMANAGER(sm);

    if (gutil_strv_contains(self->services, name)) {
        if (!self->remote) {
            self->remote = gbinder_object_registry_get_remote
                (gbinder_ipc_object_registry(gbinder_client_ipc(sm->client)),
                     1, TRUE);
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
    GBinderLocalObject* obj,
    const GBinderIpcSyncApi* api)
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
    klass->iface = TEST_HWSERVICEMANAGER_IFACE;
    klass->default_device = GBINDER_DEFAULT_HWBINDER;
    klass->list = test_servicemanager_list;
    klass->get_service = test_servicemanager_get_service;
    klass->add_service = test_servicemanager_add_service;
    klass->check_name = test_hwservicemanager_check_name;
    klass->normalize_name = test_hwservicemanager_normalize_name;
    klass->watch = test_hwservicemanager_watch;
    klass->unwatch = test_hwservicemanager_unwatch;
    G_OBJECT_CLASS(klass)->finalize = test_hwservicemanager_finalize;
}

GType
gbinder_servicemanager_hidl_get_type()
{
    return TEST_TYPE_HWSERVICEMANAGER;
}

/*==========================================================================*
 * TestDefServiceManager
 *==========================================================================*/

typedef TestServiceManagerClass TestDefServiceManagerClass;
typedef TestServiceManager TestDefServiceManager;

G_DEFINE_TYPE(TestDefServiceManager, test_defservicemanager,
    GBINDER_TYPE_SERVICEMANAGER)

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
    klass->iface = TEST_DEFSERVICEMANAGER_IFACE;
    klass->default_device = GBINDER_DEFAULT_BINDER;
    klass->list = test_servicemanager_list;
    klass->get_service = test_servicemanager_get_service;
    klass->add_service = test_servicemanager_add_service;
    klass->check_name = test_defservicemanager_check_name;
    klass->watch = test_defservicemanager_watch;
    G_OBJECT_CLASS(klass)->finalize = test_defservicemanager_finalize;
}

GType
gbinder_servicemanager_aidl_get_type()
{
    return TEST_TYPE_DEFSERVICEMANAGER;
}

GType
gbinder_servicemanager_aidl2_get_type()
{
    /* Dummy function to avoid pulling in gbinder_servicemanager_aidl2 */
    g_assert_not_reached();
    return 0;
}

GType
gbinder_servicemanager_aidl3_get_type()
{
    /* Dummy function to avoid pulling in gbinder_servicemanager_aidl3 */
    g_assert_not_reached();
    return 0;
}

GType
gbinder_servicemanager_aidl4_get_type()
{
    /* Dummy function to avoid pulling in gbinder_servicemanager_aidl4 */
    g_assert_not_reached();
    return 0;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!gbinder_servicemanager_new(NULL));
    g_assert(!gbinder_servicemanager_new_with_type(0, NULL, NULL));
    g_assert(!gbinder_servicemanager_new_local_object(NULL, NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_ref(NULL));
    g_assert(!gbinder_servicemanager_device(NULL));
    g_assert(!gbinder_servicemanager_is_present(NULL));
    g_assert(!gbinder_servicemanager_wait(NULL, 0));
    g_assert(!gbinder_servicemanager_list(NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_list_sync(NULL));
    g_assert(!gbinder_servicemanager_get_service(NULL, NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_get_service_sync(NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_add_service(NULL, NULL, NULL, NULL, NULL));
    g_assert(gbinder_servicemanager_add_service_sync(NULL, NULL, NULL) ==
        (-EINVAL));
    g_assert(!gbinder_servicemanager_add_presence_handler(NULL, NULL, NULL));
    g_assert(!gbinder_servicemanager_add_registration_handler(NULL, NULL,
        NULL, NULL));
    gbinder_servicemanager_remove_handler(NULL, 0);
    gbinder_servicemanager_remove_handlers(NULL, NULL, 0);
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
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    TestConfig config;
    gulong id = 0;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    test_setup_ping(ipc);
    g_assert(!gbinder_servicemanager_new2(GBINDER_DEFAULT_HWBINDER, "a", NULL));
    sm = gbinder_servicemanager_new(dev);
    g_assert(!gbinder_servicemanager_new_with_type(GBINDER_TYPE_LOCAL_OBJECT,
        NULL, NULL));
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
    g_assert(!gbinder_servicemanager_add_presence_handler(sm, NULL, NULL));
    g_assert(!gbinder_servicemanager_add_registration_handler(sm, NULL, NULL,
        NULL));

    gbinder_servicemanager_cancel(sm, 0);
    gbinder_servicemanager_remove_handler(sm, 0);
    gbinder_servicemanager_remove_handlers(sm, NULL, 0);
    gbinder_servicemanager_remove_handlers(sm, &id, 0);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
    test_config_cleanup(&config);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    GBinderLocalObject* obj;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    g_assert(sm);
    obj = gbinder_servicemanager_new_local_object(sm, "foo.bar",
        test_transact_func, NULL);
    g_assert(obj);
    g_assert_cmpstr(gbinder_servicemanager_device(sm), == ,dev);
    gbinder_local_object_unref(obj);

    g_assert(gbinder_servicemanager_ref(sm) == sm);
    gbinder_servicemanager_unref(sm);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
    test_config_cleanup(&config);
}

/*==========================================================================*
 * legacy
 *==========================================================================*/

static
void
test_legacy(
    void)
{
    const char* otherdev = "/dev/otherbinder";
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    test_setup_ping(ipc);
    sm = gbinder_hwservicemanager_new(dev);
    g_assert(TEST_IS_HWSERVICEMANAGER(sm));
    gbinder_servicemanager_unref(sm);

    test_setup_ping(ipc);
    sm = gbinder_defaultservicemanager_new(dev);
    g_assert(TEST_IS_DEFSERVICEMANAGER(sm));
    gbinder_servicemanager_unref(sm);

    gbinder_ipc_unref(ipc);

    /* Legacy default */
    ipc = gbinder_ipc_new(otherdev, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(otherdev);
    g_assert(TEST_IS_DEFSERVICEMANAGER(sm));
    gbinder_servicemanager_unref(sm);

    gbinder_ipc_unref(ipc);
    gbinder_servicemanager_exit();
    test_binder_exit_wait(&test_opt, NULL);
    test_config_cleanup(&config);
}

/*==========================================================================*
 * config
 *==========================================================================*/

static
void
test_config(
    void)
{
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    const char* strange_name = "/dev/notbinder";
    const char* legacy_name = "/dev/legacybinder";
    TestConfig test;
    char* file;

    static const char config[] =
        "[ServiceManager]\n"
        "Default = hidl\n"
        "/dev/binder = hidl\n" /* Redefined name for /dev/binder */
        "/dev/hwbinder = foo\n" /* Invalid name */
        "/dev/legacybinder = aidl\n";

    test_config_init(&test, TMP_DIR_TEMPLATE);
    file = g_build_filename(test.config_dir, "test.conf", NULL);

    /* Write the config file */
    g_assert(g_file_set_contents(file, config, -1, NULL));
    GDEBUG("Config file %s", file);
    gbinder_config_file = file;

    /* Unknown device instantiates the default */
    ipc = gbinder_ipc_new(strange_name, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(strange_name);
    g_assert(TEST_IS_HWSERVICEMANAGER(sm));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    /* This one was redefined */
    ipc = gbinder_ipc_new(GBINDER_DEFAULT_BINDER, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(GBINDER_DEFAULT_BINDER);
    g_assert(TEST_IS_HWSERVICEMANAGER(sm));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    /* This one was not (since name was invalid) */
    ipc = gbinder_ipc_new(GBINDER_DEFAULT_HWBINDER, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(GBINDER_DEFAULT_HWBINDER);
    g_assert(TEST_IS_HWSERVICEMANAGER(sm));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    /* This one points to legacy manager */
    ipc = gbinder_ipc_new(legacy_name, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(legacy_name);
    g_assert(TEST_IS_DEFSERVICEMANAGER(sm));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    /* Overwrite the legacy manager with "hidl" */
    ipc = gbinder_ipc_new(legacy_name, "hidl");
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new2(legacy_name, "hidl", "hidl");
    g_assert(TEST_IS_HWSERVICEMANAGER(sm));
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    /* Clear the state */
    gbinder_servicemanager_exit();
    test_binder_exit_wait(&test_opt, NULL);
    remove(file);
    g_free(file);
    test_config_cleanup(&test);
}

/*==========================================================================*
 * not_present
 *==========================================================================*/

static
void
test_not_present(
    void)
{
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    TestConfig config;
    int fd;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    /* This makes presence detection PING fail */
    test_binder_br_reply_status(fd, THIS_THREAD, -1);

    sm = gbinder_servicemanager_new(dev);
    g_assert(sm);
    g_assert(!gbinder_servicemanager_is_present(sm));

    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
    test_config_cleanup(&config);
}

/*==========================================================================*
 * wait
 *==========================================================================*/

static
void
test_wait(
    void)
{
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    const glong forever = (test_opt.flags & TEST_FLAG_DEBUG) ?
        (TEST_TIMEOUT_SEC * 1000) : -1;
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    gulong id;
    int fd, count = 0;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    /* This makes presence detection PING fail */
    test_binder_br_reply_status(fd, THIS_THREAD, -1);

    sm = gbinder_servicemanager_new(dev);
    g_assert(sm);
    g_assert(!gbinder_servicemanager_is_present(sm));

    /* Register the listener */
    id = gbinder_servicemanager_add_presence_handler(sm, test_inc, &count);
    g_assert(id);

    /* Make this wait fail */
    test_binder_br_reply_status(fd, THIS_THREAD, -1);
    g_assert(!gbinder_servicemanager_wait(sm, 0));

    /* This makes presence detection PING succeed */
    test_binder_br_noop(fd, THIS_THREAD);
    test_binder_br_transaction_complete(fd, THIS_THREAD);
    test_binder_br_reply(fd, THIS_THREAD, 0, 0, NULL);
    g_assert(gbinder_servicemanager_wait(sm, forever));

    /* The next check succeeds too (without any I/O ) */
    g_assert(gbinder_servicemanager_is_present(sm));
    g_assert(gbinder_servicemanager_wait(sm, 0));

    /* The listener must have been invoked exactly once */
    g_assert(count == 1);
    gbinder_servicemanager_remove_handler(sm, id);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
    test_config_cleanup(&config);
}

/*==========================================================================*
 * wait_long
 *==========================================================================*/

static
void
test_wait_long(
    void)
{
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    gulong id;
    int fd, count = 0;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    /* This makes presence detection PING fail */
    test_binder_br_reply_status(fd, THIS_THREAD, -1);

    sm = gbinder_servicemanager_new(dev);
    g_assert(sm);
    g_assert(!gbinder_servicemanager_is_present(sm));

    /* Register the listener */
    id = gbinder_servicemanager_add_presence_handler(sm, test_inc, &count);
    g_assert(id);

    /* Make the first presence detection PING fail and second succeed */
    test_binder_br_reply_status(fd, THIS_THREAD, -1);
    test_binder_br_reply_status(fd, TX_THREAD, -1);
    test_binder_br_transaction_complete(fd, TX_THREAD);
    test_binder_br_reply(fd, TX_THREAD, 0, 0, NULL);
    g_assert(gbinder_servicemanager_wait(sm, TEST_TIMEOUT_SEC * 1000));

    /* The next check succeeds too (without any I/O ) */
    g_assert(gbinder_servicemanager_is_present(sm));
    g_assert(gbinder_servicemanager_wait(sm, 0));

    /* The listener must have been invoked exactly once */
    g_assert(count == 1);
    gbinder_servicemanager_remove_handler(sm, id);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
    test_config_cleanup(&config);
}

/*==========================================================================*
 * wait_async
 *==========================================================================*/

static
void
test_wait_async(
    void)
{
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    gulong id[2];
    int fd, count = 0;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    /* This makes presence detection PING fail */
    test_binder_br_reply_status(fd, THIS_THREAD, -1);

    sm = gbinder_servicemanager_new(dev);
    g_assert(sm);
    g_assert(!gbinder_servicemanager_is_present(sm));

    /* Register the listeners */
    id[0] = gbinder_servicemanager_add_presence_handler(sm, test_inc, &count);
    id[1] = gbinder_servicemanager_add_presence_handler(sm, test_quit, loop);
    g_assert(id[0]);
    g_assert(id[1]);

    /* Make the first presence detection PING fail and second succeed */
    test_binder_br_reply_status(fd, THIS_THREAD, -1);
    test_binder_br_transaction_complete(fd, TX_THREAD);
    test_binder_br_reply(fd, TX_THREAD, 0, 0, NULL);
    test_run(&test_opt, loop);

    /* The listener must have been invoked exactly once */
    g_assert(count == 1);
    gbinder_servicemanager_remove_all_handlers(sm, id);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    test_config_cleanup(&config);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * death
 *==========================================================================*/

static
void
test_death_run()
{
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    gulong id[3];
    int fd, count = 0, reg_count = 0;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    g_assert(sm);
    g_assert(gbinder_servicemanager_is_present(sm));

    /* Register the listeners */
    id[0] = gbinder_servicemanager_add_presence_handler(sm, test_inc, &count);
    id[1] = gbinder_servicemanager_add_presence_handler(sm, test_quit, loop);
    id[2] = gbinder_servicemanager_add_registration_handler(sm, "foo",
        test_reg_inc, &reg_count);
    g_assert(id[0]);
    g_assert(id[1]);
    g_assert(id[2]);

    /* Generate death notification */
    test_binder_br_dead_binder(fd, ANY_THREAD, 0);
    test_run(&test_opt, loop);

    /* No registrations must have occured */
    g_assert(!reg_count);

    /* The listener must have been invoked exactly once */
    g_assert(count == 1);
    g_assert(!gbinder_servicemanager_is_present(sm));
    gbinder_servicemanager_remove_all_handlers(sm, id);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    test_config_cleanup(&config);
    g_main_loop_unref(loop);
}

static
void
test_death()
{
    test_run_in_context(&test_opt, test_death_run);
}

/*==========================================================================*
 * reanimate
 *==========================================================================*/

static
void
test_reanimate_quit(
    GBinderServiceManager* sm,
    void* user_data)
{
    if (gbinder_servicemanager_is_present(sm)) {
        GDEBUG("Service manager is back");
        test_quit_later((GMainLoop*)user_data);
    } else {
        const int fd = gbinder_driver_fd(sm->client->remote->ipc->driver);

        /* Reanimate the object */
        GDEBUG("Reanimating...");
        test_binder_br_transaction_complete(fd, THIS_THREAD);
        test_binder_br_reply(fd, THIS_THREAD, 0, 0, NULL);
    }
}

static
void
test_reanimate(
    void)
{
    const char* dev = GBINDER_DEFAULT_HWBINDER;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    gulong id[3];
    int fd, count = 0, reg_count = 0;
    TestConfig config;

    /* Create live service manager */
    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    g_assert(sm);
    g_assert(gbinder_servicemanager_is_present(sm));

    /* Register the listeners */
    id[0] = gbinder_servicemanager_add_presence_handler(sm, test_inc, &count);
    id[1] = gbinder_servicemanager_add_presence_handler(sm,
        test_reanimate_quit, loop);
    id[2] = gbinder_servicemanager_add_registration_handler(sm, "foo",
        test_reg_inc, &reg_count);
    g_assert(id[0]);
    g_assert(id[1]);
    g_assert(id[2]);

    /* Generate death notification */
    test_binder_br_dead_binder(fd, ANY_THREAD, 0);
    test_run(&test_opt, loop);

    /* No registrations must have occured */
    g_assert(!reg_count);

    /* Presence must have changed twice */
    g_assert(count == 2);
    g_assert(gbinder_servicemanager_is_present(sm));

    gbinder_servicemanager_remove_all_handlers(sm, id);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    test_config_cleanup(&config);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * reuse
 *==========================================================================*/

static
void
test_reuse(
    void)
{
    const char* binder_dev = GBINDER_DEFAULT_BINDER;
    const char* vndbinder_dev = "/dev/vpnbinder";
    const char* hwbinder_dev = GBINDER_DEFAULT_HWBINDER;
    GBinderIpc* binder_ipc;
    GBinderIpc* vndbinder_ipc;
    GBinderIpc* hwbinder_ipc;
    GBinderServiceManager* m1;
    GBinderServiceManager* m2;
    GBinderServiceManager* vnd1;
    GBinderServiceManager* vnd2;
    GBinderServiceManager* hw1;
    GBinderServiceManager* hw2;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    binder_ipc = gbinder_ipc_new(binder_dev, NULL);
    vndbinder_ipc = gbinder_ipc_new(vndbinder_dev, NULL);
    hwbinder_ipc = gbinder_ipc_new(hwbinder_dev, NULL);

    test_setup_ping(binder_ipc);
    test_setup_ping(vndbinder_ipc);
    test_setup_ping(hwbinder_ipc);

    m1 = gbinder_servicemanager_new(binder_dev);
    m2 = gbinder_servicemanager_new(binder_dev);
    vnd1 = gbinder_servicemanager_new(vndbinder_dev);
    vnd2 = gbinder_servicemanager_new(vndbinder_dev);
    hw1 = gbinder_servicemanager_new(hwbinder_dev);
    hw2 = gbinder_servicemanager_new(hwbinder_dev);

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
    gbinder_ipc_unref(binder_ipc);
    gbinder_ipc_unref(vndbinder_ipc);
    gbinder_ipc_unref(hwbinder_ipc);
    test_binder_exit_wait(&test_opt, NULL);
    test_config_cleanup(&config);
}

/*==========================================================================*
 * notify
 *==========================================================================*/

static
void
test_notify_type(
    GType t,
    const char* dev)
{
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    TestHwServiceManager* test;
    const char* name = "foo";
    int count = 0;
    gulong id1, id2;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new_with_type(t, NULL, NULL);
    test = TEST_SERVICEMANAGER2(sm, t);
    id1 = gbinder_servicemanager_add_registration_handler(sm, name,
        test_registration_func_inc, &count);
    id2 = gbinder_servicemanager_add_registration_handler(sm, name,
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
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, NULL);
    test_config_cleanup(&config);
}

static
void
test_notify(
    void)
{
    test_notify_type(TEST_TYPE_HWSERVICEMANAGER, GBINDER_DEFAULT_HWBINDER);
    test_notify_type(TEST_TYPE_DEFSERVICEMANAGER, GBINDER_DEFAULT_BINDER);
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
    const char* dev = GBINDER_DEFAULT_BINDER;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    TestHwServiceManager* test;
    char** list;
    gulong id;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    test = TEST_SERVICEMANAGER(sm);
    test->services = gutil_strv_add(test->services, "foo");
    list = gbinder_servicemanager_list_sync(sm);
    g_assert(gutil_strv_equal(test->services, list));
    g_strfreev(list);

    id = gbinder_servicemanager_list(sm, test_list_func, loop);
    g_assert(id);

    test_run(&test_opt, loop);

    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    test_config_cleanup(&config);
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
    const char* dev = GBINDER_DEFAULT_BINDER;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    TestHwServiceManager* test;
    int status = -1;
    GBinderLocalObject* obj;
    gulong id;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    test = TEST_SERVICEMANAGER(sm);
    obj = gbinder_servicemanager_new_local_object(sm, "foo.bar",
       test_transact_func, NULL);

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
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    test_config_cleanup(&config);
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
    const char* dev = GBINDER_DEFAULT_BINDER;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GBinderIpc* ipc;
    GBinderServiceManager* sm;
    TestHwServiceManager* test;
    GBinderLocalObject* obj;
    gulong id;
    TestConfig config;

    test_config_init(&config, TMP_DIR_TEMPLATE);
    ipc = gbinder_ipc_new(dev, NULL);
    test_setup_ping(ipc);
    sm = gbinder_servicemanager_new(dev);
    test = TEST_SERVICEMANAGER(sm);

    obj = gbinder_servicemanager_new_local_object(sm, "foo.bar",
        test_transact_func, NULL);
    id = gbinder_servicemanager_add_service(sm, "foo", obj,
        test_add_func, loop);

    g_assert(id);
    test_run(&test_opt, loop);
    g_assert(gutil_strv_contains(test->services, "foo"));

    gbinder_local_object_unref(obj);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);
    test_binder_exit_wait(&test_opt, loop);
    test_config_cleanup(&config);
    g_main_loop_unref(loop);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/servicemanager/" t

int main(int argc, char* argv[])
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
    g_type_init();
    G_GNUC_END_IGNORE_DEPRECATIONS;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("invalid"), test_invalid);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("legacy"), test_legacy);
    g_test_add_func(TEST_("config"), test_config);
    g_test_add_func(TEST_("not_present"), test_not_present);
    g_test_add_func(TEST_("wait"), test_wait);
    g_test_add_func(TEST_("wait_long"), test_wait_long);
    g_test_add_func(TEST_("wait_async"), test_wait_async);
    g_test_add_func(TEST_("death"), test_death);
    g_test_add_func(TEST_("reanimate"), test_reanimate);
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
