/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 * Copyright (C) 2018-2021 Slava Monich <slava.monich@jolla.com>
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
#include "gbinder_handler.h"
#include "gbinder_local_request_p.h"
#include "gbinder_output_data.h"
#include "gbinder_rpc_protocol.h"

#include <poll.h>

static TestOpt test_opt;

#define STRICT_MODE_PENALTY_GATHER (0x40 << 16)
#define BINDER_RPC_FLAGS (STRICT_MODE_PENALTY_GATHER)

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    GBinderDriver* driver;
    const char* dev = GBINDER_DEFAULT_BINDER;

    g_assert(!gbinder_driver_new("", NULL));
    driver = gbinder_driver_new(dev, NULL);
    g_assert(driver);
    g_assert(!g_strcmp0(dev, gbinder_driver_dev(driver)));
    g_assert(gbinder_driver_protocol(driver));
    g_assert(gbinder_driver_protocol(driver) ==
        gbinder_rpc_protocol_for_device(dev));
    g_assert(gbinder_driver_ref(driver) == driver);
    gbinder_driver_unref(driver);
    gbinder_driver_free_buffer(driver, NULL);
    g_assert(gbinder_driver_io(driver));
    g_assert(gbinder_driver_increfs(driver, 0));
    g_assert(gbinder_driver_decrefs(driver, 0));
    g_assert(gbinder_driver_acquire(driver, 0));
    g_assert(gbinder_driver_release(driver, 0));
    g_assert(gbinder_driver_enter_looper(driver));
    g_assert(gbinder_driver_exit_looper(driver));
    g_assert(!gbinder_driver_request_death_notification(driver, NULL));
    g_assert(!gbinder_driver_clear_death_notification(driver, NULL));
    g_assert(!gbinder_driver_dead_binder_done(NULL, NULL));
    gbinder_driver_unref(driver);

    g_assert(!gbinder_handler_transact(NULL, NULL, NULL, 0, 0, NULL));
    g_assert(!gbinder_handler_can_loop(NULL));
}

/*==========================================================================*
 * noop
 *==========================================================================*/

static
void
test_noop(
    void)
{
    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    const int fd = gbinder_driver_fd(driver);

    g_assert(driver);
    g_assert(fd >= 0);
    test_binder_br_noop(fd);
    g_assert(gbinder_driver_poll(driver, NULL) == POLLIN);
    g_assert(gbinder_driver_read(driver, NULL, NULL) == 0);

    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * local_request
 *==========================================================================*/

static
void
test_local_request(
    void)
{
    static const char iface[] = "test";
    static const guint8 rpc_header [] =  {
        TEST_INT32_BYTES(BINDER_RPC_FLAGS),
        TEST_INT32_BYTES(4),
        TEST_INT16_BYTES('t'), TEST_INT16_BYTES('e'),
        TEST_INT16_BYTES('s'), TEST_INT16_BYTES('t'),
        0x00, 0x00, 0x00, 0x00
    };

    GBinderDriver* driver = gbinder_driver_new(GBINDER_DEFAULT_BINDER, NULL);
    GBinderLocalRequest* req = gbinder_driver_local_request_new(driver, iface);
    GBinderOutputData* data = gbinder_local_request_data(req);

    g_assert(data->bytes->len == sizeof(rpc_header));
    g_assert(!memcmp(data->bytes->data, rpc_header, sizeof(rpc_header)));
    gbinder_local_request_unref(req);
    gbinder_driver_unref(driver);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/driver/"

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_PREFIX "basic", test_basic);
    g_test_add_func(TEST_PREFIX "noop", test_noop);
    g_test_add_func(TEST_PREFIX "local_request", test_local_request);
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
