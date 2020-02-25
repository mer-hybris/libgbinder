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

#include <gbinder.h>

#include <gutil_log.h>

#define RET_OK          (0)
#define RET_NOTFOUND    (1)
#define RET_INVARG      (2)
#define RET_ERR         (3)

#define DEFAULT_BINDER          GBINDER_DEFAULT_HWBINDER
#define AIDL_PING_TRANSACTION   GBINDER_FOURCC('_','P','N','G')
#define HIDL_PING_TRANSACTION   GBINDER_FOURCC(0x0f,'P','N','G')

typedef struct app_options {
    const char* fqname;
    char* dev;
    guint32 ping_code;
    const char* iface;
} AppOptions;

static
int
app_run(
    const AppOptions* opt)
{
    int ret = RET_NOTFOUND;
    GBinderServiceManager* sm = gbinder_servicemanager_new(opt->dev);

    if (sm) {
        int status = 0;
        GBinderRemoteObject* remote = gbinder_servicemanager_get_service_sync
            (sm, opt->fqname, &status);

        if (remote) {
            int status;
            GBinderClient* client = gbinder_client_new(remote, opt->iface);
            GBinderRemoteReply* reply = gbinder_client_transact_sync_reply
                (client, opt->ping_code, NULL, &status);
                             
            if (reply) {
                GINFO("OK");
                ret = RET_OK;
            } else {
                GERR("Ping failed (%d)", status);
                ret = RET_ERR;
            }
            gbinder_remote_reply_unref(reply);
            gbinder_client_unref(client);
        } else {
            GERR("%s not found", opt->fqname);
        }
        gbinder_servicemanager_unref(sm);
    } else {
        GERR("No servicemanager at %s", opt->dev);
    }
    return ret;
}

static
gboolean
app_log_verbose(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    gutil_log_default.level = GLOG_LEVEL_VERBOSE;
    return TRUE;
}

static
gboolean
app_log_quiet(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    gutil_log_default.level = GLOG_LEVEL_NONE;
    return TRUE;
}

static
gboolean
app_init(
    AppOptions* opt,
    int argc,
    char* argv[])
{
    gboolean ok = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          app_log_verbose, "Enable verbose output", NULL },
        { "quiet", 'q', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          app_log_quiet, "Be quiet", NULL },
        { "device", 'd', 0, G_OPTION_ARG_STRING, &opt->dev,
          "Binder device [" DEFAULT_BINDER "]", "DEVICE" },
        { NULL }
    };

    GError* error = NULL;
    GOptionContext* options = g_option_context_new("[FQNAME]");

    gutil_log_timestamp = FALSE;
    gutil_log_default.level = GLOG_LEVEL_DEFAULT;

    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if (!opt->dev || !opt->dev[0]) {
            opt->dev = g_strdup(DEFAULT_BINDER);
        }
        if (argc == 2) {
            opt->fqname = argv[1];
            if (g_strcmp0(opt->dev, GBINDER_DEFAULT_BINDER)) {
                opt->ping_code = HIDL_PING_TRANSACTION;
                opt->iface = "android.hidl.base@1.0::IBase";
            } else {
                opt->ping_code = AIDL_PING_TRANSACTION;
                opt->iface = "android.os.IBinder";
            }
            ok = TRUE;
        } else {
            char* help = g_option_context_get_help(options, TRUE, NULL);

            fprintf(stderr, "%s", help);
            g_free(help);
        }
    } else {
        GERR("%s", error->message);
        g_error_free(error);
    }
    g_option_context_free(options);
    return ok;
}

int main(int argc, char* argv[])
{
    AppOptions opt;
    int ret = RET_INVARG;

    memset(&opt, 0, sizeof(opt));
    if (app_init(&opt, argc, argv)) {
        ret = app_run(&opt);
    }
    g_free(opt.dev);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
