/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_config.h"

static TestOpt test_opt;
static const char TMP_DIR_TEMPLATE[] = "gbinder-test-config-XXXXXX";

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    const char* default_name = gbinder_config_file;

    /* Reset the state */
    gbinder_config_exit();
    gbinder_config_file = NULL;
    g_assert(!gbinder_config_get());

    /* Reset the state again */
    gbinder_config_file = default_name;
}

/*==========================================================================*
 * non_exist
 *==========================================================================*/

static
void
test_non_exit(
    void)
{
    const char* default_name = gbinder_config_file;
    char* dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    char* file = g_build_filename(dir, "test.conf", NULL);

    /* Reset the state */
    gbinder_config_exit();

    gbinder_config_file = file;
    g_assert(!gbinder_config_get());

    /* Reset the state again */
    gbinder_config_file = default_name;

    g_free(file);
    remove(dir);
    g_free(dir);
}

/*==========================================================================*
 * bad_config
 *==========================================================================*/

static
void
test_bad_config(
    void)
{
    const char* default_name = gbinder_config_file;
    char* dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    char* file = g_build_filename(dir, "test.conf", NULL);
    static const char garbage[] = "foo";

    /* Reset the state */
    gbinder_config_exit();

    /* Try to load the garbage */
    g_assert(g_file_set_contents(file, garbage, -1, NULL));
    gbinder_config_file = file;
    g_assert(!gbinder_config_get());

    /* Reset the state again */
    gbinder_config_file = default_name;

    remove(file);
    g_free(file);

    remove(dir);
    g_free(dir);
}

/*==========================================================================*
 * autorelease
 *==========================================================================*/

static
void
test_autorelease(
    void)
{
    const char* default_name = gbinder_config_file;
    char* dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    char* file = g_build_filename(dir, "test.conf", NULL);
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GKeyFile* keyfile;
    static const char config[] = "[Protocol]";

    gbinder_config_exit(); /* Reset the state */

    /* Load the file */
    g_assert(g_file_set_contents(file, config, -1, NULL));
    gbinder_config_file = file;
    keyfile = gbinder_config_get();
    g_assert(keyfile);

    /* Second call returns the same pointer */
    g_assert(keyfile == gbinder_config_get());

    test_quit_later_n(loop, 2);
    test_run(&test_opt, loop);
    g_main_loop_unref(loop);

    /* Reset the state again */
    gbinder_config_exit();
    gbinder_config_file = default_name;

    remove(file);
    g_free(file);

    remove(dir);
    g_free(dir);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/config/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("non_exist"), test_non_exit);
    g_test_add_func(TEST_("bad_config"), test_bad_config);
    g_test_add_func(TEST_("autorelease"), test_autorelease);
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
