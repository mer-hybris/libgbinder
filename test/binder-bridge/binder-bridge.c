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

#include <gbinder.h>

#include <gutil_log.h>

#include <glib-unix.h>

#define RET_OK      (0)
#define RET_NODEV   (1)
#define RET_INVARG  (2)

typedef struct app_options {
    const char* src;
    const char* dest;
    char* src_name;
    const char* dest_name;
    const char** ifaces;
} AppOptions;

static
gboolean
app_signal(
    gpointer loop)
{
    GINFO("Caught signal, shutting down...");
    g_main_loop_quit(loop);
    return G_SOURCE_CONTINUE;
}

static
int
app_run(
    const AppOptions* opt)
{
    int ret = RET_NODEV;
    GBinderServiceManager* src = gbinder_servicemanager_new(opt->src);

    if (src) {
        GBinderServiceManager* dest = gbinder_servicemanager_new(opt->dest);

        if (dest) {
            GMainLoop* loop = g_main_loop_new(NULL, TRUE);
            guint sigtrm = g_unix_signal_add(SIGTERM, app_signal, loop);
            guint sigint = g_unix_signal_add(SIGINT, app_signal, loop);
            GBinderBridge* bridge = gbinder_bridge_new2
                (opt->src_name, opt->dest_name, opt->ifaces, src, dest);

            g_main_loop_run(loop);

            if (sigtrm) g_source_remove(sigtrm);
            if (sigint) g_source_remove(sigint);
            g_main_loop_unref(loop);
            gbinder_bridge_free(bridge);
            gbinder_servicemanager_unref(dest);
            ret = RET_OK;
        } else {
            GERR("No servicemanager at %s", opt->dest);
        }
        gbinder_servicemanager_unref(src);
    } else {
        GERR("No servicemanager at %s", opt->src);
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
        { "source", 's', 0, G_OPTION_ARG_STRING, &opt->src_name,
          "Register a different name on source", "NAME" },
        { "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          app_log_verbose, "Enable verbose output", NULL },
        { "quiet", 'q', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          app_log_quiet, "Be quiet", NULL },
        { NULL }
    };

    GError* error = NULL;
    GOptionContext* options = g_option_context_new("SRC DST NAME IFACES...");

    gutil_log_default.level = GLOG_LEVEL_DEFAULT;

    g_option_context_add_main_entries(options, entries, NULL);
    g_option_context_set_summary(options,
        "Forwards calls from device SRC to device DST.");

    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if (argc >= 5) {
            int i;
            const int first_iface = 4;

            opt->src = argv[1];
            opt->dest = argv[2];
            opt->dest_name = argv[3];
            opt->ifaces = g_new(const char*, argc - first_iface + 1);
            for (i = first_iface; i < argc; i++) {
                opt->ifaces[i - first_iface] = argv[i];
            }
            opt->ifaces[i - first_iface] = NULL;
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
    g_free(opt.src_name);
    g_free(opt.ifaces);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
