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

#include "gbinder_config.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_log.h"

/*
 * The contents of the config file is queried from (at least) two places,
 * and pretty much always this happens the same stack. Which means that
 * we can avoid reading the same file twice if we delay dereferencing of
 * GKeyFile until the next idle loop.
 */
static GKeyFile* gbinder_config_keyfile = NULL;
static GBinderEventLoopCallback* gbinder_config_autorelease = NULL;

static const char gbinder_config_default_file[] = "/etc/gbinder.conf";
const char* gbinder_config_file = gbinder_config_default_file;

static
void
gbinder_config_autorelease_cb(
    gpointer data)
{
    GASSERT(gbinder_config_keyfile == data);
    gbinder_config_keyfile = NULL;
    g_key_file_unref(data);
}

GKeyFile* /* autoreleased */
gbinder_config_get()
{
    if (!gbinder_config_keyfile && gbinder_config_file &&
        g_file_test(gbinder_config_file, G_FILE_TEST_EXISTS)) {
        GError* error = NULL;

        gbinder_config_keyfile = g_key_file_new();
        if (g_key_file_load_from_file(gbinder_config_keyfile,
            gbinder_config_file, G_KEY_FILE_NONE, &error)) {
            gbinder_config_autorelease = gbinder_idle_callback_schedule_new
               (gbinder_config_autorelease_cb, gbinder_config_keyfile, NULL);
        } else {
            GERR("Error loading %s: %s", gbinder_config_file, error->message);
            g_error_free(error);
            g_key_file_unref(gbinder_config_keyfile);
            gbinder_config_keyfile = NULL;
            gbinder_config_file = NULL; /* Don't retry */
        }
    }
    return gbinder_config_keyfile;
}

void
gbinder_config_exit()
{
    if (gbinder_config_autorelease) {
        gbinder_idle_callback_destroy(gbinder_config_autorelease);
        gbinder_config_autorelease = NULL;
    }
    if (gbinder_config_keyfile) {
        g_key_file_unref(gbinder_config_keyfile);
        gbinder_config_keyfile = NULL;
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
