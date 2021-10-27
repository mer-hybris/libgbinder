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

#include <gutil_misc.h>
#include <gutil_log.h>

#include <sys/mman.h>

#define RET_OK          (0)
#define RET_NOTFOUND    (1)
#define RET_INVARG      (2)
#define RET_ERR         (3)

#define DEFAULT_BINDER  GBINDER_DEFAULT_HWBINDER
#define ALLOCATOR_IFACE "android.hidl.allocator@1.0::IAllocator"
#define DEFAULT_FQNAME  ALLOCATOR_IFACE "/ashmem"
#define TX_ALLOCATE     GBINDER_FIRST_CALL_TRANSACTION

typedef struct app_options {
    const char* fqname;
    char* dev;
    gsize size;
} AppOptions;

static
void
app_dumpmem(
    const GBinderHidlMemory* mem)
{
    const GBinderFds* fds = mem->data.fds;

    GDEBUG("Name: %s", mem->name.data.str);
    GDEBUG("Size: %" G_GUINT64_FORMAT " bytes", mem->size);

    GASSERT(fds->version == GBINDER_HIDL_FDS_VERSION);
    GDEBUG("Contains %u fd(s)", fds->num_fds);
    if (fds->num_fds) {
        guint i;

        for (i = 0; i < fds->num_fds; i++) {
            int fd = gbinder_fds_get_fd(fds, i);
            guint8* ptr = mmap(NULL, mem->size, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, 0);

            if (ptr) {
                gsize off = 0;

                GDEBUG("fd %d => %p", fd, ptr);
                while (off < mem->size) {
                    char line[GUTIL_HEXDUMP_BUFSIZE];
                    guint n = gutil_hexdump(line, ptr + off, mem->size - off);

                    GDEBUG("%04X: %s", (uint) off, line);
                    off += n;
                }
                munmap(ptr, mem->size);
            } else {
                GDEBUG("fd %d", fd);
            }
        }
    }
}

static
int
app_allocate(
    const AppOptions* opt,
    GBinderClient* client)
{
    GBinderLocalRequest* request = gbinder_client_new_request(client);
    GBinderRemoteReply* reply;
    int status, ret;

    gbinder_local_request_append_int64(request, opt->size);
    reply = gbinder_client_transact_sync_reply(client, TX_ALLOCATE,
        request, &status);
                             
    if (reply) {
        GBinderReader reader;
        gint32 tx_status;
        gboolean success;

        gbinder_remote_reply_init_reader(reply, &reader);
        if (gbinder_reader_read_int32(&reader, &tx_status) &&
            gbinder_reader_read_bool(&reader, &success) &&
            tx_status == GBINDER_STATUS_OK &&
            success) {
            const GBinderHidlMemory* mem = gbinder_reader_read_hidl_struct
                (&reader, GBinderHidlMemory);

            if (mem) {
                GINFO("OK");
                app_dumpmem(mem);
            } else {
                GINFO("OOPS");
            }
        } else {
            GINFO("FAILED");
        }
        ret = RET_OK;
    } else {
        GERR("Call failed (%d)", status);
        ret = RET_ERR;
    }

    gbinder_local_request_unref(request);
    gbinder_remote_reply_unref(reply);
    return ret;
}

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
            GBinderClient* client = gbinder_client_new(remote, ALLOCATOR_IFACE);

            ret = app_allocate(opt, client);
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
        if (argc < 3) {
            opt->fqname = ((argc == 2) ? argv[1] : DEFAULT_FQNAME);
            opt->size = 64;
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
