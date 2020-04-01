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

#include "test_binder.h"

#include "gbinder_log.h"

#include <stdlib.h>

static TestOpt test_opt;

static const char env[] = "GBINDER_DEFAULT_LOG_LEVEL";

/*==========================================================================*
 * empty
 *==========================================================================*/

static
void
test_empty(
    void)
{
    const int level = GLOG_MODULE_NAME.level;
    unsetenv(env);
    gbinder_log_init();
    g_assert_cmpint(level, == ,GLOG_MODULE_NAME.level);
}

/*==========================================================================*
 * invalid
 *==========================================================================*/

static
void
test_invalid(
    void)
{
    const int level = GLOG_MODULE_NAME.level;

    setenv(env, "-2" /* GLOG_LEVEL_ALWAYS */, TRUE);
    gbinder_log_init();
    g_assert_cmpint(level, == ,GLOG_MODULE_NAME.level);

    setenv(env, "6" /* GLOG_LEVEL_VERBOSE + 1 */, TRUE);
    gbinder_log_init();
    g_assert_cmpint(level, == ,GLOG_MODULE_NAME.level);

    setenv(env, "foo", TRUE);
    gbinder_log_init();
    g_assert_cmpint(level, == ,GLOG_MODULE_NAME.level);
}

/*==========================================================================*
 * level
 *==========================================================================*/

typedef struct test_level_data {
    const char* test_name;
    const char* env_value;
    int level;
} TestLevelData;

static
void
test_level(
    gconstpointer data)
{
    const TestLevelData* test = data;

    GLOG_MODULE_NAME.level = GLOG_LEVEL_ALWAYS;
    g_assert_cmpint(GLOG_MODULE_NAME.level, != ,test->level);
    setenv(env, test->env_value, TRUE);
    gbinder_log_init();
    g_assert_cmpint(GLOG_MODULE_NAME.level, == ,test->level);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/log/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
#define TEST_LEVEL_INIT(X,x)  \
    { TEST_(#x), #x, x }
    static const TestLevelData level_tests[] = {
        { TEST_("inherit"), "-1", GLOG_LEVEL_INHERIT },
        { TEST_("none"), "0", GLOG_LEVEL_NONE },
        { TEST_("err"), "1", GLOG_LEVEL_ERR },
        { TEST_("warn"), "2", GLOG_LEVEL_WARN },
        { TEST_("info"), "3", GLOG_LEVEL_INFO },
        { TEST_("debug"), "4", GLOG_LEVEL_DEBUG },
        { TEST_("verbose"), "5", GLOG_LEVEL_VERBOSE }
    };

    guint i;

    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("empty"), test_empty);
    g_test_add_func(TEST_("invalid"), test_invalid);
    for (i = 0; i < G_N_ELEMENTS(level_tests); i++) {
        g_test_add_data_func(level_tests[i].test_name, level_tests + i,
            test_level);
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
