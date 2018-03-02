/*
 * Copyright (C) 2018 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
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
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
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

#define RET_OK          (0)
#define RET_NOTFOUND    (1)
#define RET_INVARG      (2)
#define RET_ERR         (3)

#define DEFAULT_DEVICE  "/dev/binder"
#define DEFAULT_NAME    "test"
#define DEFAULT_IFACE   "test@1.0"

typedef struct app_options {
    char* dev;
    char* iface;
    char* fqname;
    const char* name;
} AppOptions;

typedef struct app {
    const AppOptions* opt;
    GMainLoop* loop;
    GBinderServiceManager* sm;
    GBinderLocalObject* local;
    GBinderClient* client;
    int ret;
} App;

typedef struct app_input {
    App* app;
    char* str;
} AppInput;

static const char pname[] = "binder-client";

static
gboolean
app_signal(
    gpointer user_data)
{
    App* app = user_data;

    GINFO("Caught signal, shutting down...");
    g_main_loop_quit(app->loop);
    return G_SOURCE_CONTINUE;
}

static
void
app_remote_died(
    GBinderRemoteObject* obj,
    void* user_data)
{
    App* app = user_data;

    GINFO("Remote has died, exiting...");
    g_main_loop_quit(app->loop);
}

static
void
app_call(
    App* app,
    char* str)
{
    GBinderLocalRequest* req = gbinder_client_new_request(app->client);
    GBinderRemoteReply* reply;
    int status;

    gbinder_local_request_append_string16(req, str);
    reply = gbinder_client_transact_sync_reply(app->client,
        GBINDER_FIRST_CALL_TRANSACTION, req, &status);
    gbinder_local_request_unref(req);

    if (status == GBINDER_STATUS_OK) {
        GBinderReader reader;
        char* ret;

        gbinder_remote_reply_init_reader(reply, &reader);
        ret = gbinder_reader_read_string16(&reader);
        GDEBUG_("Reply: \"%s\"", ret);
        g_free(ret);
    } else {
        GERR_("status %d", status);
    }

    gbinder_remote_reply_unref(reply);
}

static
gboolean
app_input(
    void* user_data)
{
    AppInput* input = user_data;

    GDEBUG_("\"%s\"", input->str);
    app_call(input->app, input->str);
    g_free(input->str);
    g_free(input);
    return G_SOURCE_REMOVE;
}

static
gpointer
app_input_thread(
    gpointer data)
{
    int c;
    App* app = data;
    GString* buf = g_string_new("");

    while ((c = getc(stdin)) != EOF) {
        if (c == '\n' || c == '\r') {
            AppInput* input = g_new0(AppInput, 1);

            input->app = app;
            input->str = g_strdup(buf->str);
            g_idle_add(app_input, input);
            g_string_truncate(buf, 0);

            while (c == '\n' || c == '\r') c = getc(stdin);
            if (c == EOF) {
                break;
            } else {
                ungetc(c, stdin);
                continue;
            }
        } else {
            g_string_append_c(buf, (char)c);
        }
    }

    GDEBUG_("Input thread exiting...");
    g_string_free(buf, TRUE);
    return NULL;
}

static
void
app_run(
   App* app)
{
    const AppOptions* opt = app->opt;
    char* fqname = opt->fqname ? g_strdup(opt->fqname) :
        strchr(opt->name, '/') ? g_strdup(opt->name) :
        g_strconcat(opt->iface, "/", opt->name, NULL);
    int status = 0;
    GBinderRemoteObject* remote = gbinder_remote_object_ref
        (gbinder_servicemanager_get_service_sync(app->sm, fqname, &status));
    if (remote) {
        guint sigtrm = g_unix_signal_add(SIGTERM, app_signal, app);
        guint sigint = g_unix_signal_add(SIGINT, app_signal, app);
        gulong death_id = gbinder_remote_object_add_death_handler
            (remote, app_remote_died, app);
        GThread* thread = g_thread_new("input", app_input_thread, app);

        GINFO("Connected to %s\n", fqname);

        app->client = gbinder_client_new(remote, opt->iface);
        app->ret = RET_OK;
        app->loop = g_main_loop_new(NULL, TRUE);
        g_main_loop_run(app->loop);

        g_source_remove(sigtrm);
        g_source_remove(sigint);
        g_main_loop_unref(app->loop);

        gbinder_remote_object_remove_handler(remote, death_id);
        gbinder_remote_object_unref(remote);

        /* Not the cleanest exit, just dropping the thread... */
        g_thread_unref(thread);
        app->loop = NULL;
    } else {
        GERR("No such service: %s (%d)", fqname, status);
    }
    g_free(fqname);
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
    gutil_log_default.level = GLOG_LEVEL_ERR;
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
          "Binder device [" DEFAULT_DEVICE "]", "DEVICE" },
        { "interface", 'i', 0, G_OPTION_ARG_STRING, &opt->iface,
          "Interface name [" DEFAULT_IFACE "]", "IFACE" },
        { "fqname", 'n', 0, G_OPTION_ARG_STRING, &opt->fqname,
          "Fully qualified name [IFACE/NAME]", "FQNAME" },
        { NULL }
    };

    GError* error = NULL;
    GOptionContext* options = g_option_context_new("[NAME]");

    memset(opt, 0, sizeof(*opt));

    gutil_log_timestamp = FALSE;
    gutil_log_set_type(GLOG_TYPE_STDERR, pname);
    gutil_log_default.level = GLOG_LEVEL_DEFAULT;

    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        char* help;

        if (!opt->dev || !opt->dev[0]) opt->dev = g_strdup(DEFAULT_DEVICE);
        if (!opt->iface || !opt->iface[0]) opt->iface = g_strdup(DEFAULT_IFACE);
        switch (argc) {
        case 2:
            opt->name = argv[1];
            ok = TRUE;
            break;
        case 1:
            opt->name = DEFAULT_NAME;
            ok = TRUE;
            break;
        default:
            help = g_option_context_get_help(options, TRUE, NULL);
            fprintf(stderr, "%s", help);
            g_free(help);
            break;
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
    App app;
    AppOptions opt;

    memset(&app, 0, sizeof(app));
    app.ret = RET_INVARG;
    app.opt = &opt;
    if (app_init(&opt, argc, argv)) {
        app.sm = gbinder_servicemanager_new(opt.dev);
        if (app.sm) {
            app.local = gbinder_servicemanager_new_local_object(app.sm,
                NULL, NULL, NULL);
            app_run(&app);
            gbinder_local_object_unref(app.local);
            gbinder_client_unref(app.client);
            gbinder_servicemanager_unref(app.sm);
        }
    }
    g_free(opt.fqname);
    g_free(opt.iface);
    g_free(opt.dev);
    return app.ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
