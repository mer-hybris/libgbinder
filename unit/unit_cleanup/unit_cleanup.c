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

#include "test_binder.h"

#include "gbinder_cleanup.h"

static TestOpt test_opt;

static
void
test_cleanup_inc(
    gpointer data)
{
    (*((int*)data))++;
}

/*==========================================================================*
 * null
 *==========================================================================*/

static
void
test_null(
    void)
{
    g_assert(!gbinder_cleanup_add(NULL, NULL, NULL));
    gbinder_cleanup_free(NULL);
    gbinder_cleanup_reset(NULL);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    int n1 = 0, n2 =0;
    GBinderCleanup* cleanup = gbinder_cleanup_add(NULL, test_cleanup_inc, &n1);

    g_assert(cleanup);
    g_assert(gbinder_cleanup_add(cleanup, test_cleanup_inc, &n2) == cleanup);
    gbinder_cleanup_free(cleanup);
    g_assert(n1 == 1);
    g_assert(n2 == 1);
}

/*==========================================================================*
 * reset
 *==========================================================================*/

static
void
test_reset(
    void)
{
    int n1 = 0, n2 =0;
    GBinderCleanup* cleanup = gbinder_cleanup_add(NULL, test_cleanup_inc, &n1);

    g_assert(cleanup);
    g_assert(gbinder_cleanup_add(cleanup, test_cleanup_inc, &n2) == cleanup);
    gbinder_cleanup_reset(cleanup);
    g_assert(n1 == 1);
    g_assert(n2 == 1);

    gbinder_cleanup_free(cleanup);
    g_assert(n1 == 1);
    g_assert(n2 == 1);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/cleanup/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("null"), test_null);
    g_test_add_func(TEST_("basic"), test_basic);
    g_test_add_func(TEST_("reset"), test_reset);
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
