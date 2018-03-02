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

#include <gutil_misc.h>
#include <gutil_log.h>

#include <glib-unix.h>

#define RET_OK            (0)
#define RET_NOTFOUND      (1)
#define RET_INVARG        (2)
#define RET_ERR           (3)

#define DEFAULT_DEVICE    "/dev/hwbinder"
#define DEFAULT_NAME      "slot1"

#define IFACE_RADIO       "android.hardware.radio@1.0::IRadio"
#define IFACE_RESPONSE    "android.hardware.radio@1.0::IRadioResponse"
#define IFACE_INDICATION  "android.hardware.radio@1.0::IRadioIndication"

/* android.hardware.radio@1.0::IRadio */
#define REQ_RADIO_SET_RESPONSE_FUNCTIONS    (1)
#define REQ_RADIO_GET_ICC_CARD_STATUS       (2)

/* android.hardware.radio@1.0::IRadioResponse */
#define RESP_GET_ICC_CARD_STATUS_RESPONSE   (1)

typedef struct app_options {
    char* dev;
    char* fqname;
    const char* name;
} AppOptions;

typedef struct app {
    const AppOptions* opt;
    GMainLoop* loop;
    GBinderServiceManager* sm;
    GBinderLocalObject* response;
    GBinderLocalObject* indication;
    int ret;
} App;

typedef struct radio_response_info {
    guint32 type;
    guint32 serial;
    guint32 error;
} RadioResponseInfo;

typedef struct radio_string {
    union {
        guint64 value;
        const char* str;
    } data;
    guint32 len;
    guint32 owns_buffer;
} RadioString;

typedef struct radio_app_status {
    guint32 appType;
    guint32 appState;
    guint32 persoSubstate;
    guint32 unused1;
    RadioString aid;
    RadioString label;
    guint32 pinReplaced;
    guint32 pin1;
    guint32 pin2;
    guint32 unused2;
} RadioAppStatus;

typedef struct radio_card_status {
    guint32 cardState;
    guint32 universalPinState;
    guint32 gsmUmtsSubscriptionAppIndex;
    guint32 cdmaSubscriptionAppIndex;
    guint32 imsSubscriptionAppIndex;
    guint32 unused1;
    union {
        guint64 value;
        const RadioAppStatus* array;
    } apps;
    guint32 numApps;
    guint32 unused2;
} RadioCardStatus;

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
app_dump(
    const void* buf,
    gsize len)
{
    const guint8* ptr = buf;
    while (len > 0) {
        char line[GUTIL_HEXDUMP_BUFSIZE];
        const guint consumed = gutil_hexdump(line, ptr, len);

        GDEBUG("  %s", line);
        len -= consumed;
        ptr += consumed;
    }
}

static
void
app_decode_card_status(
    GBinderRemoteRequest* req)
{
    GBinderReader reader;
    GBinderBuffer* buf;

    gbinder_remote_request_init_reader(req, &reader);
    /* RadioResponseInfo */
    buf = gbinder_reader_read_buffer(&reader);
    if (buf) {
        const RadioResponseInfo* info = buf->data;

        GASSERT(sizeof(*info) == buf->size);
        GDEBUG("RadioResponseInfo: type=%d serial=%d error=%d",
            (int)info->type, (int)info->serial, (int)info->error);
        app_dump(buf->data, buf->size);
        gbinder_buffer_free(buf);

        /* CardStatus */
        buf = gbinder_reader_read_buffer(&reader);
        if (buf) {
            guint i;
            const RadioCardStatus* status = buf->data;

            GINFO("CardStatus: state=%d pinState=%d gsmSubIndex=%d "
                "cdmsSubIndex=%d imsSubIndex=%d appCount=%u",
                (int)status->cardState, (int)status->universalPinState,
                (int)status->gsmUmtsSubscriptionAppIndex,
                (int)status->cdmaSubscriptionAppIndex,
                (int)status->imsSubscriptionAppIndex, status->numApps);
            GASSERT(sizeof(*status) == buf->size);
            app_dump(buf->data, buf->size);
            gbinder_buffer_free(buf);

            for (i = 0; i < status->numApps; i++) {
                const RadioAppStatus* app = status->apps.array + i;

                buf = gbinder_reader_read_buffer(&reader);
                GASSERT(buf->size == sizeof(RadioAppStatus));
                GINFO("AppStatus: type=%u state=%u substate=%u aid=%s "
                    "label=%s pin_replaced=%u pin1=%u pin2=%u",
                    app->appType, app->appState, app->persoSubstate,
                    app->aid.data.str, app->label.data.str,
                    app->pinReplaced, app->pin1, app->pin2);
                app_dump(buf->data, buf->size);
                gbinder_buffer_free(buf);
            }
        }
    }
}

static
GBinderLocalReply*
IRadioIndication_transact(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    const char* iface = gbinder_remote_request_interface(req);

    GDEBUG("%s %u", iface, code);
    if (!g_strcmp0(iface, IFACE_INDICATION)) {
        /* Those should all be one-way */
        GASSERT(flags & GBINDER_TX_FLAG_ONEWAY);
        *status = GBINDER_STATUS_OK;
    } else {
        *status = GBINDER_STATUS_FAILED;
    }
    return NULL;
}

static
GBinderLocalReply*
IRadioResponse_transact(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    App* app = user_data;
    const char* iface = gbinder_remote_request_interface(req);

    if (!g_strcmp0(iface, IFACE_RESPONSE)) {
        /* Those should all be one-way */
        GASSERT(flags & GBINDER_TX_FLAG_ONEWAY);
        if (code == RESP_GET_ICC_CARD_STATUS_RESPONSE) {
            GDEBUG("%s getIccCardStatusResponse", iface);
            *status = GBINDER_STATUS_OK;
            app_decode_card_status(req);
            g_main_loop_quit(app->loop);
            return NULL;
        }
    }
    GDEBUG("%s %u", iface, code);
    *status = GBINDER_STATUS_FAILED;
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
        g_strconcat(IFACE_RADIO "/", opt->name, NULL);
    int status = 0;
    GBinderRemoteObject* remote = gbinder_remote_object_ref
        (gbinder_servicemanager_get_service_sync(app->sm, fqname, &status));
    if (remote) {
        guint sigtrm = g_unix_signal_add(SIGTERM, app_signal, app);
        guint sigint = g_unix_signal_add(SIGINT, app_signal, app);
        gulong death_id = gbinder_remote_object_add_death_handler
            (remote, app_remote_died, app);
        GBinderClient* client = gbinder_client_new(remote, IFACE_RADIO);
        GBinderLocalRequest* req;
        GBinderRemoteReply* reply;
        int status;

        GINFO("Connected to %s", fqname);

        app->ret = RET_OK;
        app->loop = g_main_loop_new(NULL, TRUE);

        /* IRadio::setResponseFunctions */
        req = gbinder_client_new_request(client);
        gbinder_local_request_append_local_object(req, app->response);
        gbinder_local_request_append_local_object(req, app->indication);
        reply = gbinder_client_transact_sync_reply(client,
            REQ_RADIO_SET_RESPONSE_FUNCTIONS, req, &status);
        GDEBUG("setResponseFunctions status %d", status);
        gbinder_local_request_unref(req);
        gbinder_remote_reply_unref(reply);

        /* IRadio::getIccCardStatus */
        req = gbinder_client_new_request(client);
        gbinder_local_request_append_int32(req, 1 /* serial */);
        status = gbinder_client_transact_sync_oneway(client,
            REQ_RADIO_GET_ICC_CARD_STATUS, req);
        GDEBUG("getIccCardStatus status %d", status);
        gbinder_local_request_unref(req);

        g_main_loop_run(app->loop);

        g_source_remove(sigtrm);
        g_source_remove(sigint);
        g_main_loop_unref(app->loop);

        gbinder_remote_object_remove_handler(remote, death_id);
        gbinder_remote_object_unref(remote);
        gbinder_client_unref(client);
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
        { "fqname", 'n', 0, G_OPTION_ARG_STRING, &opt->fqname,
          "Fully qualified name", "FQNAME" },
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
            app.indication = gbinder_servicemanager_new_local_object(app.sm,
                IFACE_INDICATION, IRadioIndication_transact, &app);
            app.response = gbinder_servicemanager_new_local_object(app.sm,
                IFACE_RESPONSE, IRadioResponse_transact, &app);
            app_run(&app);
            gbinder_local_object_unref(app.indication);
            gbinder_local_object_unref(app.response);
            gbinder_servicemanager_unref(app.sm);
        }
    }
    g_free(opt.fqname);
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
