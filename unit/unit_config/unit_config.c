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

#include <gutil_strv.h>
#include <gutil_log.h>

#include <sys/stat.h>
#include <sys/types.h>

static TestOpt test_opt;
static const char TMP_DIR_TEMPLATE[] = "gbinder-test-config-XXXXXX";

static
const char*
test_value(
    GKeyFile* keyfile,
    const char* group,
    const char* key,
    GString* buf)
{
    char* value = g_key_file_get_value(keyfile, group, key, NULL);

    g_string_set_size(buf, 0);
    if (value) {
        g_string_append(buf, value);
        g_free(value);
        return buf->str;
    } else {
        return NULL;
    }
}

static
gboolean
test_keyfiles_equal(
    GKeyFile* keyfile1,
    GKeyFile* keyfile2)
{
    gboolean equal = FALSE;
    gsize ngroups;
    char** groups = g_key_file_get_groups(keyfile1, &ngroups);
    char** groups2 = g_key_file_get_groups(keyfile2, NULL);

    gutil_strv_sort(groups, TRUE);
    gutil_strv_sort(groups2, TRUE);
    if (gutil_strv_equal(groups, groups2)) {
        gsize i;

        equal = TRUE;
        for (i = 0; i < ngroups && equal; i++) {
            const char* group = groups[i];
            gsize nkeys;
            char** keys = g_key_file_get_keys(keyfile1, group, &nkeys, NULL);
            char** keys2 = g_key_file_get_keys(keyfile2, group, &nkeys, NULL);

            equal = FALSE;
            gutil_strv_sort(keys, TRUE);
            gutil_strv_sort(keys2, TRUE);
            if (gutil_strv_equal(keys, keys2)) {
                gsize k;

                equal = TRUE;
                for (k = 0; k < nkeys && equal; k++) {
                    const char* key = keys[k];
                    char* v1 = g_key_file_get_value(keyfile1, group, key, NULL);
                    char* v2 = g_key_file_get_value(keyfile2, group, key, NULL);

                    if (g_strcmp0(v1, v2)) {
                        equal = FALSE;
                        GDEBUG("Values for %s/%s don't match ('%s' vs '%s')",
                            group, key, v1, v2);
                    }
                    g_free(v1);
                    g_free(v2);
                }
            } else {
                GDEBUG("Keys for %s don't match", group);
            }
            g_strfreev(keys);
            g_strfreev(keys2);
        }
    } else {
        GDEBUG("Groups don't match");
    }
    g_strfreev(groups);
    g_strfreev(groups2);
    if (!equal) {
        char* data1 = g_key_file_to_data(keyfile1, NULL, NULL);
        char* data2 = g_key_file_to_data(keyfile2, NULL, NULL);

        GDEBUG("This:");
        GDEBUG("%s", data1);
        GDEBUG("Doesn't match this:");
        GDEBUG("%s", data2);
        g_free(data1);
        g_free(data2);
    }
    return equal;
}

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
    gbinder_config_dir = NULL;
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
 * dirs
 *==========================================================================*/

static
void
test_dirs(
    void)
{
    GKeyFile* k;
    GString* b = g_string_new(NULL);
    const char* default_file = gbinder_config_file;
    const char* default_dir = gbinder_config_dir;
    char* dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    char* subdir = g_build_filename(dir, "d", NULL);
    char* notafile = g_build_filename(subdir, "dir.conf", NULL);
    char* file = g_build_filename(dir, "test.conf", NULL);
    char* file1 = g_build_filename(subdir, "a.conf", NULL);
    char* file2 = g_build_filename(subdir, "b.conf", NULL);
    char* random_file = g_build_filename(subdir, "foo", NULL);
    static const char garbage[] = "foo";
    static const char config[] =
        "[Protocol]\n"
        "/dev/binder = aidl\n"
        "/dev/hbinder = hidl\n";
    static const char config1[] =
        "[Protocol]\n"
        "/dev/hwbinder = hidl\n"
        "[ServiceManager]\n"
        "/dev/binder = aidl\n";
    static const char config2[] =
        "[Protocol]\n"
        "/dev/binder = aidl2\n"
        "[ServiceManager]\n"
        "/dev/binder = aidl2\n";

    g_assert_cmpint(mkdir(subdir, 0700), == ,0);
    g_assert_cmpint(mkdir(notafile, 0700), == ,0);
    g_assert(g_file_set_contents(file, config, -1, NULL));
    g_assert(g_file_set_contents(file1, config1, -1, NULL));
    g_assert(g_file_set_contents(file2, config2, -1, NULL));
    g_assert(g_file_set_contents(random_file, garbage, -1, NULL));

    /* Reset the state */
    gbinder_config_exit();
    gbinder_config_file = file;
    gbinder_config_dir = subdir;

    /* Load the config */
    k = gbinder_config_get();
    g_assert(k);
    g_assert_cmpstr(test_value(k,"Protocol","/dev/binder",b), == ,"aidl2");
    g_assert_cmpstr(test_value(k,"Protocol","/dev/hbinder",b), == ,"hidl");
    g_assert_cmpstr(test_value(k,"Protocol","/dev/hwbinder",b), == ,"hidl");
    g_assert_cmpstr(test_value(k,"ServiceManager","/dev/binder",b),==,"aidl2");

    /* Remove the default file and try again */
    gbinder_config_exit();
    g_assert_cmpint(remove(file), == ,0);
    k = gbinder_config_get();
    g_assert(k);
    g_assert(!test_value(k,"Protocol","/dev/hbinder",b));
    g_assert_cmpstr(test_value(k,"Protocol","/dev/binder",b), == ,"aidl2");
    g_assert_cmpstr(test_value(k,"Protocol","/dev/hwbinder",b), == ,"hidl");
    g_assert_cmpstr(test_value(k,"ServiceManager","/dev/binder",b),==,"aidl2");

    /* Damage one of the files and try again */
    gbinder_config_exit();
    g_assert(g_file_set_contents(file1, garbage, -1, NULL));
    k = gbinder_config_get();
    g_assert(k);
    g_assert(!test_value(k,"Protocol","/dev/hbinder",b));
    g_assert(!test_value(k,"Protocol","/dev/hwbinder",b));
    g_assert_cmpstr(test_value(k,"Protocol","/dev/binder",b), == ,"aidl2");
    g_assert_cmpstr(test_value(k,"ServiceManager","/dev/binder",b),==,"aidl2");

    /* Disallow access to one of the files and try again */
    gbinder_config_exit();
    g_assert_cmpint(chmod(file1, 0), == ,0);
    k = gbinder_config_get();
    g_assert(k);
    g_assert(!test_value(k,"Protocol","/dev/hbinder",b));
    g_assert(!test_value(k,"Protocol","/dev/hwbinder",b));
    g_assert_cmpstr(test_value(k,"Protocol","/dev/binder",b), == ,"aidl2");
    g_assert_cmpstr(test_value(k,"ServiceManager","/dev/binder",b),==,"aidl2");

    /* Delete the remaining files and try again */
    gbinder_config_exit();
    g_assert_cmpint(remove(file1), == ,0);
    g_assert_cmpint(remove(file2), == ,0);
    g_assert(!gbinder_config_get());

    /* Undo all the damage */
    gbinder_config_exit();
    gbinder_config_file = default_file;
    gbinder_config_dir = default_dir;

    remove(random_file);
    g_free(file);
    g_free(file1);
    g_free(file2);
    g_free(random_file);

    remove(notafile);
    remove(subdir);
    remove(dir);
    g_free(notafile);
    g_free(subdir);
    g_free(dir);
    g_string_free(b, TRUE);
}

/*==========================================================================*
 * autorelease
 *==========================================================================*/

static
void
test_autorelease(
    void)
{
    const char* default_file = gbinder_config_file;
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
    gbinder_config_file = default_file;

    remove(file);
    g_free(file);

    remove(dir);
    g_free(dir);
}

/*==========================================================================*
 * Presets
 *==========================================================================*/

typedef struct test_presets_data {
    const char* name;
    const char* in;
    const char* out;
} TestPresetsData;

static
void
test_presets(
    gconstpointer test_data)
{
    const TestPresetsData* test = test_data;
    const char* default_file = gbinder_config_file;
    char* dir = g_dir_make_tmp(TMP_DIR_TEMPLATE, NULL);
    char* file = g_build_filename(dir, "test.conf", NULL);
    GKeyFile* expected = g_key_file_new();
    GKeyFile* keyfile;

    /* Reset the state */
    gbinder_config_exit();

    /* Load the file */
    if (test->in) {
        g_assert(g_file_set_contents(file, test->in, -1, NULL));
        gbinder_config_file = file;
    } else {
        gbinder_config_file = NULL;
    }
    keyfile = gbinder_config_get();
    g_assert(keyfile);

    /* Compare it against the expected value */
    g_assert(g_key_file_load_from_data(expected, test->out, (gsize)-1,
        G_KEY_FILE_NONE, NULL));
    g_assert(test_keyfiles_equal(keyfile, expected));

    /* Reset the state again */
    gbinder_config_exit();
    gbinder_config_file = default_file;

    remove(file);
    g_free(file);

    remove(dir);
    g_free(dir);
    g_key_file_unref(expected);
}

static const TestPresetsData test_presets_data [] = {
    {
        "override",

        "[General]\n"
        "ApiLevel = 28\n"
        "[ServiceManager]\n"
        "/dev/vndbinder = aidl\n",

        "[General]\n"
        "ApiLevel = 28\n"
        "[ServiceManager]\n"
        "/dev/binder = aidl2\n"
        "/dev/vndbinder = aidl\n" /* Preset is overridden */
    },{
        "too_small",

        "[General]\n"
        "ApiLevel = 27\n",

        "[General]\n"
        "ApiLevel = 27\n"
    },{
       "28",

        "[General]\n"
        "ApiLevel = 28",

        "[General]\n"
        "ApiLevel = 28\n"
        "[ServiceManager]\n"
        "/dev/binder = aidl2\n"
        "/dev/vndbinder = aidl2\n"
    },{
        "29",

        "[General]\n"
        "ApiLevel = 29",

        "[General]\n"
        "ApiLevel = 29\n"
        "[Protocol]\n"
        "/dev/binder = aidl2\n"
        "/dev/vndbinder = aidl2\n"
        "[ServiceManager]\n"
        "/dev/binder = aidl2\n"
        "/dev/vndbinder = aidl2\n"
    }
};

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/config/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("non_exist"), test_non_exit);
    g_test_add_func(TEST_("dirs"), test_dirs);
    g_test_add_func(TEST_("bad_config"), test_bad_config);
    g_test_add_func(TEST_("autorelease"), test_autorelease);
    for (i = 0; i < G_N_ELEMENTS(test_presets_data); i++) {
        const TestPresetsData* test = test_presets_data + i;
        char* path;

        path = g_strconcat(TEST_("presets/"), test->name, NULL);
        g_test_add_data_func(path, test, test_presets);
        g_free(path);
    }
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
