/*
 * Copyright (C) 2021 Jolla Ltd.
 * Copyright (C) 2021 Slava Monich <slava.monich@jolla.com>
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
#include "test_servicemanager_hidl.h"

#include "gbinder_ipc.h"
#include "gbinder_config.h"
#include "gbinder_driver.h"
#include "gbinder_servicemanager_p.h"
#include "gbinder_local_object_p.h"

#include <gutil_log.h>
#include <gutil_strv.h>

static TestOpt test_opt;
#define MAIN_DEV GBINDER_DEFAULT_HWBINDER
#define OTHER_DEV GBINDER_DEFAULT_HWBINDER "-private"
static const char TMP_DIR_TEMPLATE[] = "gbinder-test-svcmgr-hidl-XXXXXX";
static const char DEFAULT_CONFIG_DATA[] =
    "[Protocol]\n"
    MAIN_DEV " = hidl\n"
    OTHER_DEV " = hidl\n"
    "[ServiceManager]\n"
    MAIN_DEV " = hidl\n";

typedef struct test_config {
    char* dir;
    char* file;
} TestConfig;

GType
gbinder_servicemanager_aidl_get_type()
{
    /* Avoid pulling in gbinder_servicemanager_aidl object */
    return 0;
}

GType
gbinder_servicemanager_aidl2_get_type()
{
    /* Avoid pulling in gbinder_servicemanager_aidl2 object */
    return 0;
}

/*==========================================================================*
 * Common
 *==========================================================================*/

static
void
test_config_init(
    TestConfig* config,
    char* config_data)
{
    config->dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    config->file = g_build_filename(config->dir, "test.conf", NULL);
    g_assert(g_file_set_contents(config->file, config_data ? config_data :
        DEFAULT_CONFIG_DATA, -1, NULL));

    gbinder_config_exit();
    gbinder_config_dir = config->dir;
    gbinder_config_file = config->file;
    GDEBUG("Wrote config to %s", config->file);
}

static
void
test_config_deinit(
    TestConfig* config)
{
    gbinder_config_exit();

    remove(config->file);
    g_free(config->file);

    remove(config->dir);
    g_free(config->dir);
}

static
TestServiceManagerHidl*
test_servicemanager_impl_new(
    const char* dev)
{
    GBinderIpc* ipc = gbinder_ipc_new(dev);
    const int fd = gbinder_driver_fd(ipc->driver);
    TestServiceManagerHidl* sm = test_servicemanager_hidl_new(ipc);

    test_binder_set_looper_enabled(fd, TEST_LOOPER_ENABLE);
    test_binder_register_object(fd, GBINDER_LOCAL_OBJECT(sm),
        GBINDER_SERVICEMANAGER_HANDLE);
    gbinder_ipc_unref(ipc);
    return sm;
}

/*==========================================================================*
 * get
 *==========================================================================*/

static
void
test_add_cb(
    GBinderServiceManager* sm,
    int status,
    void* user_data)
{
    GDEBUG("Name added");
    g_assert(status == GBINDER_STATUS_OK);
    if (user_data) {
        g_main_loop_quit(user_data);
    }
}

static
void
test_get_none_cb(
    GBinderServiceManager* sm,
    GBinderRemoteObject* obj,
    int status,
    void* user_data)
{
    g_assert(!obj);
    g_assert(status == GBINDER_STATUS_OK);
    g_main_loop_quit(user_data);
}

static
void
test_get_cb(
    GBinderServiceManager* sm,
    GBinderRemoteObject* obj,
    int status,
    void* user_data)
{
    g_assert(obj);
    g_assert(status == GBINDER_STATUS_OK);
    g_main_loop_quit(user_data);
}

static
void
test_get_run()
{
    TestConfig config;
    GBinderIpc* ipc;
    TestServiceManagerHidl* smsvc;
    GBinderLocalObject* obj;
    int fd;
    GBinderServiceManager* sm;
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    const char* name = "android.hidl.base@1.0::IBase/test";

    test_config_init(&config, NULL);
    ipc = gbinder_ipc_new(MAIN_DEV);
    smsvc = test_servicemanager_impl_new(OTHER_DEV);
    obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
    test_binder_set_passthrough(fd, TRUE);
    test_binder_set_looper_enabled(fd, TEST_LOOPER_ENABLE);
    sm = gbinder_servicemanager_new(MAIN_DEV);

    /* This one fails because of unexpected name format */
    g_assert(!gbinder_servicemanager_get_service_sync(sm, "test", NULL));

    /* Query the object (it's not there yet) and wait for completion */
    GDEBUG("Querying '%s'", name);
    g_assert(gbinder_servicemanager_get_service(sm, name, test_get_none_cb,
        loop));
    test_run(&test_opt, loop);

    /* Register object and wait for completion */
    GDEBUG("Registering object '%s' => %p", name, obj);
    g_assert(gbinder_servicemanager_add_service(sm, name, obj,
        test_add_cb, loop));
    test_run(&test_opt, loop);

    g_assert_cmpuint(test_servicemanager_hidl_object_count(smsvc), == ,1);
    g_assert(test_servicemanager_hidl_lookup(smsvc, name));

    /* Query the object (this time it must be there) and wait for completion */
    GDEBUG("Querying '%s' again", name);
    g_assert(gbinder_servicemanager_get_service(sm, name, test_get_cb, loop));
    test_run(&test_opt, loop);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(obj);
    test_servicemanager_hidl_free(smsvc);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, loop);
    test_config_deinit(&config);
    g_main_loop_unref(loop);
}

static
void
test_get()
{
    test_run_in_context(&test_opt, test_get_run);
}

/*==========================================================================*
 * list
 *==========================================================================*/

typedef struct test_list {
    char** list;
    GMainLoop* loop;
} TestList;

static
gboolean
test_list_cb(
    GBinderServiceManager* sm,
    char** services,
    void* user_data)
{
    TestList* test = user_data;

    GDEBUG("Got %u name(s)", gutil_strv_length(services));
    g_strfreev(test->list);
    test->list = services;
    g_main_loop_quit(test->loop);
    return TRUE;
}

static
void
test_list_run()
{
    TestList test;
    TestConfig config;
    GBinderIpc* ipc;
    TestServiceManagerHidl* smsvc;
    GBinderLocalObject* obj;
    int fd;
    GBinderServiceManager* sm;
    const char* name = "android.hidl.base@1.0::IBase/test";

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    test_config_init(&config, NULL);
    ipc = gbinder_ipc_new(MAIN_DEV);
    smsvc = test_servicemanager_impl_new(OTHER_DEV);
    obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
    test_binder_set_passthrough(fd, TRUE);
    test_binder_set_looper_enabled(fd, TEST_LOOPER_ENABLE);
    sm = gbinder_servicemanager_new(MAIN_DEV);

    /* Request the list and wait for completion */
    g_assert(gbinder_servicemanager_list(sm, test_list_cb, &test));
    test_run(&test_opt, test.loop);

    /* There's nothing there yet */
    g_assert(test.list);
    g_assert(!test.list[0]);

    /* Register object and wait for completion */
    g_assert(gbinder_servicemanager_add_service(sm, name, obj,
        test_add_cb, test.loop));
    test_run(&test_opt, test.loop);

    /* Request the list again */
    g_assert(gbinder_servicemanager_list(sm, test_list_cb, &test));
    test_run(&test_opt, test.loop);

    /* Now the name must be there */
    g_assert_cmpuint(gutil_strv_length(test.list), == ,1);
    g_assert_cmpstr(test.list[0], == ,name);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(obj);
    test_servicemanager_hidl_free(smsvc);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, test.loop);
    test_config_deinit(&config);

    g_strfreev(test.list);
    g_main_loop_unref(test.loop);
}

static
void
test_list()
{
    test_run_in_context(&test_opt, test_list_run);
}

/*==========================================================================*
 * notify
 *==========================================================================*/

typedef struct test_notify {
    GMainLoop* loop;
    TestServiceManagerHidl* smsvc;
    int notify_count;
    gboolean name_added;
} TestNotify;

static
void
test_notify_never(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data)
{
    g_assert_not_reached();
}

static
void
test_notify_add_cb(
    GBinderServiceManager* sm,
    int status,
    void* user_data)
{
    TestNotify* test = user_data;

    GDEBUG("Name added");
    g_assert(status == GBINDER_STATUS_OK);
    g_assert(!test->name_added);
    test->name_added = TRUE;
    if (test->notify_count) {
        g_main_loop_quit(test->loop);
    }
}

static
void
test_notify_cb(
    GBinderServiceManager* sm,
    const char* name,
    void* user_data)
{
    TestNotify* test = user_data;

    g_assert(name);
    GDEBUG("'%s' is registered", name);
    g_assert_cmpint(test->notify_count, == ,0);
    test->notify_count++;
    /* Exit the loop after both things happen */
    if (test->name_added) {
        g_main_loop_quit(test->loop);
    }
}

static
void
test_notify_run()
{
    TestNotify test;
    TestConfig config;
    GBinderIpc* ipc;
    GBinderLocalObject* obj;
    int fd;
    GBinderServiceManager* sm;
    const char* name = "android.hidl.base@1.0::IBase/test";
    gulong id;

    memset(&test, 0, sizeof(test));
    test.loop = g_main_loop_new(NULL, FALSE);

    test_config_init(&config, NULL);
    ipc = gbinder_ipc_new(MAIN_DEV);
    test.smsvc = test_servicemanager_impl_new(OTHER_DEV);
    obj = gbinder_local_object_new(ipc, NULL, NULL, NULL);
    fd = gbinder_driver_fd(ipc->driver);

    /* Set up binder simulator */
    test_binder_register_object(fd, obj, AUTO_HANDLE);
    test_binder_set_passthrough(fd, TRUE);
    test_binder_set_looper_enabled(fd, TEST_LOOPER_ENABLE);
    sm = gbinder_servicemanager_new(MAIN_DEV);

    /* This one fails because of invalid names */
    g_assert(!gbinder_servicemanager_add_registration_handler(sm, NULL,
        test_notify_never, NULL));
    g_assert(!gbinder_servicemanager_add_registration_handler(sm, "",
        test_notify_never, NULL));
    g_assert(!gbinder_servicemanager_add_registration_handler(sm, ",",
        test_notify_never, NULL));

    /* Start watching */
    id = gbinder_servicemanager_add_registration_handler(sm, name,
        test_notify_cb, &test);
    g_assert(id);

    /* Register the object and wait for completion */
    GDEBUG("Registering object '%s' => %p", name, obj);
    g_assert(gbinder_servicemanager_add_service(sm, name, obj,
        test_notify_add_cb, &test));

    /* The loop quits after the name is added and notification is received */
    test_run(&test_opt, test.loop);
    gbinder_servicemanager_remove_handler(sm, id);

    test_binder_unregister_objects(fd);
    gbinder_local_object_unref(obj);
    test_servicemanager_hidl_free(test.smsvc);
    gbinder_servicemanager_unref(sm);
    gbinder_ipc_unref(ipc);

    gbinder_ipc_exit();
    test_binder_exit_wait(&test_opt, test.loop);
    test_config_deinit(&config);
    g_main_loop_unref(test.loop);
}

static
void
test_notify()
{
    test_run_in_context(&test_opt, test_notify_run);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(t) "/servicemanager_hidl/" t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("get"), test_get);
    g_test_add_func(TEST_("list"), test_list);
    g_test_add_func(TEST_("notify"), test_notify);
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
