/*
 * Copyright (C) 2021-2022 Jolla Ltd.
 * Copyright (C) 2021 Franz-Josef Haider <franz.haider@jolla.com>
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

#include <unistd.h>
#include <binder-call.h>
#include <stdlib.h>

#define RET_OK          (0)
#define RET_NOTFOUND    (1)
#define RET_INVARG      (2)
#define RET_ERR         (3)

#define DEFAULT_DEVICE     GBINDER_DEFAULT_BINDER

#define GBINDER_TRANSACTION(c2,c3,c4)     GBINDER_FOURCC('_',c2,c3,c4)
#define GBINDER_INTERFACE_TRANSACTION     GBINDER_TRANSACTION('N','T','F')

static const char pname[] = "binder-call";

struct transaction_and_reply* ast;

enum transaction_pass {
    COMPUTE_SIZES = 0,
    FILL_BUFFERS,
    BUILD_TRANSACTION
};

enum reply_pass {
    PRINT_REPLY = 0,
    COMPUTE_SIZES_REPLY
};

static
int
go_through_transaction_ast(
    App* app,
    GList* node_list,
    int parent_idx,
    void* buf,
    enum transaction_pass cur_pass,
    int cont_offset)
{
    GList* l;
    int offset = cont_offset;

    for (l = node_list; l; l = l->next) {
        struct value_info* v = l->data;

        switch(v->type) {
        case INT8_TYPE:
            if (cur_pass == BUILD_TRANSACTION) {
                GDEBUG("int8 %u", (guint)(*((guint8*)v->value)));
            }
            if (parent_idx == -1) {
                gbinder_writer_append_int8(&app->writer, *((guint8*)v->value));
            } else if (cur_pass == FILL_BUFFERS) {
                *((unsigned char*)(((char*)buf)+offset)) =
                    *((unsigned char*)v->value);
            }
            offset++;
            break;

        case INT32_TYPE:
            if (cur_pass == BUILD_TRANSACTION) {
                GDEBUG("int32 %d", *((int*)v->value));
            }
            if (parent_idx == -1) {
                gbinder_writer_append_int32(&app->writer, *((int*)v->value));
            } else if (cur_pass == FILL_BUFFERS) {
                *((int*)(((char*)buf)+offset)) = *((int*)v->value);
            }
            offset += sizeof(gint32);
            break;

        case INT64_TYPE:
            if (cur_pass == BUILD_TRANSACTION) {
                GDEBUG("int64 %" G_GINT64_MODIFIER "d", *((gint64*)v->value));
            }
            if (parent_idx == -1) {
                gbinder_writer_append_int64(&app->writer, *((gint64*)v->value));
            } else if (cur_pass == FILL_BUFFERS) {
                *((gint64*)(((char*)buf)+offset)) = *((gint64*)v->value);
            }
            offset += sizeof(gint64);
            break;

        case FLOAT_TYPE:
            if (cur_pass == BUILD_TRANSACTION) {
                GDEBUG("float %g", (double)*((float*)v->value));
            }
            if (parent_idx == -1) {
                gbinder_writer_append_float(&app->writer, *((float*)v->value));
            } else if (cur_pass == FILL_BUFFERS) {
                *((float*)(((char*)buf)+offset)) = *((float*)v->value);
            }
            offset += sizeof(float);
            break;
        case DOUBLE_TYPE:
            if (cur_pass == BUILD_TRANSACTION) {
                GDEBUG("double %g", *((double*)v->value));
            }
            if (parent_idx == -1) {
                gbinder_writer_append_double(&app->writer,*((double*)v->value));
            } else if (cur_pass == FILL_BUFFERS) {
                *((double*)(((char*)buf)+offset)) = *((double*)v->value);
            }
            offset += sizeof(double);
            break;

        case STRING8_TYPE:
            if (cur_pass == BUILD_TRANSACTION) {
                GDEBUG("string8 %s", (char*)v->value);
            }
            gbinder_writer_append_string8(&app->writer, v->value);
            /* offset not incremented since it only makes sense for hidl */
            break;

        case STRING16_TYPE:
            if (cur_pass == BUILD_TRANSACTION) GDEBUG("string16");
            gbinder_writer_append_string16(&app->writer, v->value);
            /* offset not incremented since it only makes sense for hidl */
            break;

        case HSTRING_TYPE:
            if (cur_pass == BUILD_TRANSACTION) GDEBUG("hstring");
            if (parent_idx == -1) {
                gbinder_writer_append_hidl_string(&app->writer, v->value);
            } else {
                GBinderHidlString* hidl_str = (GBinderHidlString*)
                    (((char*)buf)+offset);

                if (cur_pass == FILL_BUFFERS) {
                    hidl_str->data.str = v->value;
                    hidl_str->len = strlen(v->value);
                    hidl_str->owns_buffer = TRUE;
                } else if (cur_pass == BUILD_TRANSACTION) {
                    GBinderParent p;

                    p.index = parent_idx;
                    p.offset = offset;
                    gbinder_writer_append_buffer_object_with_parent
                        (&app->writer, hidl_str->data.str, hidl_str->len+1, &p);
                }
            }
            offset += sizeof(GBinderHidlString);
            break;

        case STRUCT_TYPE:
            if (cur_pass == BUILD_TRANSACTION) GDEBUG("struct");
            if (!app->opt->aidl) {
                if (parent_idx == -1) {
                    int s = go_through_transaction_ast(app, v->value, 0,
                        NULL, COMPUTE_SIZES, 0);
                    void* new_buf = gbinder_writer_malloc(&app->writer, s);
                    int new_parent_idx;

                    go_through_transaction_ast(app, v->value, 0, new_buf,
                        FILL_BUFFERS, 0);
                    new_parent_idx = gbinder_writer_append_buffer_object
                        (&app->writer, new_buf, s);
                    /*
                     * if parent_idx == -1 there is no need to update the
                     * offset, since we are processing the argument list
                     *  and are not inside an argument.
                     */
                    go_through_transaction_ast(app, v->value,
                        new_parent_idx, new_buf, BUILD_TRANSACTION, 0);
                } else {
                    if (cur_pass == FILL_BUFFERS) {
                        /* fill struct mode */
                        offset += go_through_transaction_ast(app,
                            v->value, 0, ((char*)buf)+offset, cur_pass, 0);
                    } else if (cur_pass == BUILD_TRANSACTION) {
                        int s = go_through_transaction_ast(app,
                            v->value, 0, NULL, COMPUTE_SIZES, 0);

                        go_through_transaction_ast(app, v->value, 0,
                            buf, FILL_BUFFERS, offset);
                        go_through_transaction_ast(app, v->value,  parent_idx,
                            buf, BUILD_TRANSACTION, offset);
                        offset += s;
                    } else if (cur_pass == COMPUTE_SIZES) {
                        offset += go_through_transaction_ast(app,
                            v->value, 0, NULL, cur_pass, 0);
                    }
                }
            } else {
                go_through_transaction_ast(app, v->value, -1, NULL,
                    cur_pass, 0);
            }
            if (cur_pass == BUILD_TRANSACTION) GDEBUG("structend");
            break;

        case VECTOR_TYPE:
            if (cur_pass == BUILD_TRANSACTION) GDEBUG("vector");
            if (!app->opt->aidl) {
                if (parent_idx == -1) {
                    GBinderHidlVec* vec;
                    int vs = go_through_transaction_ast(app,
                        v->value, 0, NULL, COMPUTE_SIZES, 0);
                    int es = go_through_transaction_ast(app,
                        g_list_last(v->value), 0, NULL, COMPUTE_SIZES, 0);
                    void* new_buf = gbinder_writer_malloc(&app->writer, vs);
                    int new_parent_idx;
                    GBinderParent vec_parent;

                    go_through_transaction_ast(app, v->value, 0, new_buf,
                        FILL_BUFFERS, 0);
                    vec = gbinder_writer_new0(&app->writer, GBinderHidlVec);
                    vec->data.ptr = new_buf;
                    vec->count = vs / es;
                    if (vec->count != g_list_length(v->value)) {
                        GERR("SEMANTIC ERROR VECTOR");
                        abort();
                    }
                    vec_parent.index = gbinder_writer_append_buffer_object
                        (&app->writer, vec, sizeof(*vec));
                    vec_parent.offset = GBINDER_HIDL_VEC_BUFFER_OFFSET;
                    new_parent_idx =
                        gbinder_writer_append_buffer_object_with_parent
                            (&app->writer, new_buf, vs, &vec_parent);
                    go_through_transaction_ast(app, v->value,
                        new_parent_idx, new_buf, BUILD_TRANSACTION, 0);
                } else {
                    if (cur_pass == FILL_BUFFERS) {
                        /* fill struct mode */
                        int sl = go_through_transaction_ast(app,
                            v->value, 0, NULL, COMPUTE_SIZES, 0);
                        int es = go_through_transaction_ast(app,
                            g_list_last(v->value), 0, NULL, COMPUTE_SIZES, 0);
                        void* new_buf = gbinder_writer_malloc(&app->writer, sl);
                        GBinderHidlVec* vec = (GBinderHidlVec*)
                            (((char*)buf)+offset);

                        vec->data.ptr = new_buf;
                        vec->count = sl / es;
                    } else if (cur_pass == BUILD_TRANSACTION) {
                        int new_parent_idx;
                        int sl = go_through_transaction_ast(app,
                            v->value, 0, NULL, COMPUTE_SIZES, 0);
                        GBinderHidlVec* vec = (GBinderHidlVec*)
                            (((char*)buf)+offset);
                        GBinderParent p;
                        void* new_buf = (void*)vec->data.ptr;

                        go_through_transaction_ast(app, v->value, 0,
                            new_buf, FILL_BUFFERS, 0);
                        if (vec->count != g_list_length(v->value)) {
                            GERR("SEMANTIC ERROR VECTOR");
                            abort();
                        }
                        p.index = parent_idx;
                        p.offset = offset;
                        new_parent_idx =
                            gbinder_writer_append_buffer_object_with_parent
                                (&app->writer, new_buf, sl, &p);
                        go_through_transaction_ast(app, v->value,
                            new_parent_idx, new_buf, BUILD_TRANSACTION, 0);
                    }
                    offset += sizeof(GBinderHidlVec);
                }
            } else {
                int vl = g_list_length(v->value);

                gbinder_writer_append_int32(&app->writer, vl);
                go_through_transaction_ast(app, v->value, -1, NULL,
                    cur_pass, 0);
            }
            if (cur_pass == BUILD_TRANSACTION) GDEBUG("vectorend");
            break;
        default:
            GERR("unknown type: %d\n", v->type);
            break;
        }
    }

    return offset;
}


static
int
go_through_reply_ast(
    App* app,
    GList* node_list,
    struct type_info* tt,
    const void* buf,
    enum reply_pass cur_pass)
{
    GList* l;
    int offset = 0;

    for (l = node_list; l || tt; l = l->next) {
        struct type_info* t = node_list ? l->data : tt;

        switch(t->type) {
        case INT8_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("int8");
            if (cur_pass != COMPUTE_SIZES_REPLY) {
                int val;

                if (!buf) {
                    gbinder_reader_read_int32(&app->reader, &val);
                } else if (cur_pass != COMPUTE_SIZES_REPLY) {
                    val = *((unsigned char*)(((char*)buf)+offset));
                }
                printf("%d:8 ", val);
            }
            offset += 1;
            break;

        case INT32_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("int32");
            if (cur_pass != COMPUTE_SIZES_REPLY) {
                gint32 val;

                if (!buf) {
                    gbinder_reader_read_int32(&app->reader, &val);
                } else if (cur_pass != COMPUTE_SIZES_REPLY) {
                    val = *((gint32*)(((char*)buf)+offset));
                }
                printf("%" G_GINT32_FORMAT " ", val);
            }
            offset += sizeof(gint32);
            break;

        case INT64_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("int64");
            if (cur_pass != COMPUTE_SIZES_REPLY) {
                gint64 val;

                if (!buf) {
                    gbinder_reader_read_int64(&app->reader, &val);
                } else if (cur_pass != COMPUTE_SIZES_REPLY) {
                    val = *((gint64*)(((char*)buf)+offset));
                }
                printf("%" G_GINT64_FORMAT " ", val);
            }
            offset += sizeof(gint64);
            break;

        case FLOAT_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("float");
            if (cur_pass != COMPUTE_SIZES_REPLY) {
                float val;

                if (!buf) {
                    gbinder_reader_read_float(&app->reader, &val);
                } else if (cur_pass != COMPUTE_SIZES_REPLY) {
                    val = *((float*)(((char*)buf)+offset));
                }
                printf("%f ", val);
            }
            offset += sizeof(float);
            break;

        case DOUBLE_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("double");
            if (cur_pass != COMPUTE_SIZES_REPLY) {
                double val;

                if (!buf) {
                    gbinder_reader_read_double(&app->reader, &val);
                } else if (cur_pass != COMPUTE_SIZES_REPLY) {
                    val = *((double*)(((char*)buf)+offset));
                }
                printf("%lfL ", val);
            }
            offset += sizeof(double);
            break;

        case STRING8_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("string8");
            printf("\"%s\" ", gbinder_reader_read_string8(&app->reader));
            /* offset not incremented since it only makes sense for hidl */
            break;

        case STRING16_TYPE: {
            char* val = gbinder_reader_read_string16(&app->reader);

            if (cur_pass == PRINT_REPLY) GDEBUG("string16");
            printf("\"%s\"U ", val);
            g_free(val);
            /* offset not incremented since it only makes sense for hidl */
            break;
        }
        case HSTRING_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("hstring");
            if (cur_pass != COMPUTE_SIZES_REPLY) {
                char* val = NULL;

                if (!buf) {
                    val = gbinder_reader_read_hidl_string(&app->reader);
                } else {
                    GBinderHidlString* hidl_str = (GBinderHidlString*)
                        (((char*)buf)+offset);

                    val = strdup(hidl_str->data.str);
                }
                printf("\"%s\"H ", val);
                g_free(val);
            }
            offset += sizeof(GBinderHidlString);
            break;

        case STRUCT_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("struct");
            if (!app->opt->aidl) {
                if (cur_pass == COMPUTE_SIZES_REPLY) {
                    offset += go_through_reply_ast(app, t->data, NULL, NULL,
                        COMPUTE_SIZES_REPLY);
                } else {
                    printf("{ ");
                    if (!buf) {
                        int sl = go_through_reply_ast(app, t->data, NULL, NULL,
                            COMPUTE_SIZES_REPLY);

                        offset += go_through_reply_ast(app, t->data, NULL,
                            gbinder_reader_read_hidl_struct1(&app->reader, sl),
                            PRINT_REPLY);
                    } else {
                        offset += go_through_reply_ast(app, t->data, NULL,
                            ((char*)buf) + offset, PRINT_REPLY);
                    }
                    printf("} ");
                }
            } else {
                go_through_reply_ast(app, t->data, NULL, NULL, cur_pass);
            }
            if (cur_pass == PRINT_REPLY) GDEBUG("structend");
            break;

        case VECTOR_TYPE:
            if (cur_pass == PRINT_REPLY) GDEBUG("vector");
            if (!app->opt->aidl) {
                if (cur_pass != COMPUTE_SIZES_REPLY) {
                    if (!buf) {
                        guint i;
                        gsize count, elemsize;
                        const void* new_buf = gbinder_reader_read_hidl_vec
                            (&app->reader, &count, &elemsize);

                        printf("[ ");
                        for (i = 0; i < count; i++) {
                            /* TODO: validate elemsize somehow? */
                            go_through_reply_ast(app, NULL, t->data,
                                new_buf + elemsize*i, cur_pass);
                        }
                        printf("] ");
                    } else {
                        guint i;
                        gsize count;
                        GBinderHidlVec* vec = (GBinderHidlVec*)
                            (((char*)buf) + offset);
                        int off;

                        count = vec->count;
                        printf("[ ");
                        off = 0;
                        for (i = 0; i < count; i++) {
                            off += go_through_reply_ast(app, NULL, t->data,
                                vec->data.ptr + off, cur_pass);
                        }
                        printf("] ");
                    }
                }
                offset += sizeof(GBinderHidlVec);
            } else {
                guint i;
                gint32 vl;

                gbinder_reader_read_int32(&app->reader, &vl);
                printf("[ ");
                for (i = 0; i < vl; i++) {
                    go_through_reply_ast(app, NULL, t->data, NULL, cur_pass);
                }
                printf("] ");
            }
            if (cur_pass == PRINT_REPLY) GDEBUG("vectorend");
            break;

        default:
            GERR("unknown type: %d\n", t->type);
            break;
        }

        if (tt) break;
    }

    return offset;
}

static
void
go_through_ast(
    App* app,
    struct transaction_and_reply* ast,
    gboolean transaction)
{
    if (ast) {
        if (transaction && ast->tree_transaction) {
            go_through_transaction_ast(app, ast->tree_transaction, -1, NULL,
                BUILD_TRANSACTION, 0);
        } else if (!transaction && ast->tree_reply) {
            GDEBUG("REPLY:");
            go_through_reply_ast(app, ast->tree_reply, NULL, NULL, PRINT_REPLY);
            printf("\n");
        }
    }
}

static
void
free_ast_transaction_tree(
    gpointer data)
{
    struct value_info* v = data;

    if (v->type == STRUCT_TYPE || v->type == VECTOR_TYPE) {
        g_list_free_full(v->value, free_ast_transaction_tree);
    } else {
        g_free(v->value);
    }

    g_free(v);
}

static
void
free_ast_reply_tree(
    gpointer data)
{
    struct type_info* t = data;

    if (t->type == VECTOR_TYPE) {
        free_ast_reply_tree(t->data);
    } else if (t->type == STRUCT_TYPE) {
        g_list_free_full(t->data, free_ast_reply_tree);
    }

    g_free(t);
}

static
void
free_ast(
    struct transaction_and_reply* ast)
{
    if (ast) {
        g_list_free_full(ast->tree_transaction, free_ast_transaction_tree);
        g_list_free_full(ast->tree_reply, free_ast_reply_tree);
        g_free(ast);
    }
}

static
void
app_run(
    App* app)
{
    const AppOptions* opt = app->opt;
    char* iface;
    int status = 0;
    int rargc = 1;
    char* service = opt->argv[rargc++];
    int code = atoi(opt->argv[rargc++]);
    GBinderClient* client;
    GBinderLocalRequest* req;
    GBinderRemoteReply* reply;
    GBinderRemoteObject* obj;

    if (!code) {
        GERR("Transaction code must be > GBINDER_FIRST_CALL_TRANSACTION(=1).");
        return;
    }

    obj = gbinder_servicemanager_get_service_sync(app->sm,
        service, &status);
    if (!obj) {
        GERR("No such service: %s", service);
        return;
    }

    if (strstr(service, "/") != NULL) {
        iface = g_strndup(service, strchr(service, '/') - service);
    } else {
        GBinderReader reader;

        client = gbinder_client_new(obj, NULL);
        req = gbinder_client_new_request(client);
        reply = gbinder_client_transact_sync_reply(client,
            GBINDER_INTERFACE_TRANSACTION, req, &status);
        gbinder_remote_reply_init_reader(reply, &reader);
        iface = gbinder_reader_read_string16(&reader);
        gbinder_local_request_unref(req);
        gbinder_remote_reply_unref(reply);
        gbinder_client_unref(client);
    }

    if (!iface) {
        GERR("Failed to get interface");
        return;
    }

    GDEBUG("Got iface: %s", iface);

    client = gbinder_client_new(obj, iface);
    g_free(iface);

    req = gbinder_client_new_request(client);

    app->rargc = rargc;
    app->code = code;

    cmdline_parse(app);

    gbinder_local_request_init_writer(req, &app->writer);
    go_through_ast(app, ast, TRUE);

    if (opt->oneway) {
        gbinder_client_transact_sync_oneway(client, code, req);
        reply = NULL;
    } else {
        reply = gbinder_client_transact_sync_reply(client, code, req, &status);
    }

    gbinder_local_request_unref(req);

    if (!reply) {
        printf("NO REPLY\n");
    } else {
        if (ast && !ast->tree_reply) {
            guchar b;

            gbinder_remote_reply_init_reader(reply, &app->reader);
            printf("TRANSACTION BUFFER: 0x");
            while (gbinder_reader_read_byte(&app->reader, &b)) {
                printf("%02X", b);
            }
            printf("\n");
        } else {
            gbinder_remote_reply_init_reader(reply, &app->reader);
            go_through_ast(app, ast, FALSE);
        }
        gbinder_remote_reply_unref(reply);
    }
    gbinder_client_unref(client);
    free_ast(ast);
}

static
gboolean
app_log_verbose(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    gutil_log_default.level = (gutil_log_default.level < GLOG_LEVEL_DEBUG) ?
        GLOG_LEVEL_DEBUG : GLOG_LEVEL_VERBOSE;
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
        { "oneway", 'o', 0, G_OPTION_ARG_NONE, &opt->oneway,
          "Use a oneway transaction", NULL },
        { "aidl", 'a', 0, G_OPTION_ARG_NONE, &opt->aidl,
          "Treat types as aidl types (default: hidl)", NULL },
        { NULL }
    };

    GError* error = NULL;
    GOptionContext* options = g_option_context_new("NAME CODE [[VALUE1] [VALUE2] ...] [reply [TYPE1] [TYPE2] ...]");
    g_option_context_set_description(options,
    "Performs binder transactions from the command line.\n\n"
    "NAME is the name of the object to call, registered with servicemanager.\n"
    "For example \"android.hardware.sensors@1.0::ISensors/default\".\n\n"
    "CODE is the transaction id (must be >=1).\n\n"
    "Optional transaction arguments follow the transaction code.\n"
    "Possible arguments are:\n\n"
    "\t[0-9]*:8 for an 8-bit integer\n"
    "\t[0-9]* for a 32-bit integer\n"
    "\t[0-9]*L for an 64-bit integer\n"
    "\t[0-9]*.[0-9]* for a 32-bit float\n"
    "\t[0-9]*.[0-9]*L for a 64-bit double\n"
    "\t\"[.*]\" for an 8-bit aidl string\n"
    "\t\"[.*]\"u for an utf16 aidl string\n"
    "\t\"[.*]\"h for an 8-bit hidl string\n"
    "\t{ VALUE1 VALUE2 ... VALUEN } for a struct containing VALUE1, VALUE2, etc., where\n"
    "\t all of these values can be any of the possible values described here.\n"
    "\t[ VALUE1 VALUE2 ... VALUEN ] for a vector of length N containing VALUE1, VALUE2, etc., where\n"
    "\t all of these values can be one of the possible VALUES described here.\n"
    "\t They must be of the same type.\n\n"
    "The structure of the reply follows the \"reply\" keyword.\n"
    "The following types are accepted:\n\n"
    "\ti8 for an 8-bit integer\n"
    "\ti32 for a 32-bit integer\n"
    "\ti64 for a 64-bit integer\n"
    "\ts8 for an 8-bit aidl string\n"
    "\ts16 for an utf16 aidl string\n"
    "\thstr for an 8-bit hidl string\n"
    "\tf|float for a 32-bit float\n"
    "\td|double for a 64-bit double\n"
    "\t[ TYPE ] for a vector<TYPE> where TYPE can be any of the possible types decribed here\n"
    "\t{ TYPE1 TYPE2 ... TYPEN } for a struct containing TYPE1, TYPE2, etc. where\n"
    "\t all of the types can be any of the possible types decribed here.\n\n"
    "The following example calls getSensorsList method on \"android.hardware.sensors@1.0::ISensors/default\"\n"
    "service:\n\n"
    "\tbinder-call -d /dev/hwbinder android.hardware.sensors@1.0::ISensors/default 1 reply i32 \"[ { i32 i32 hstr hstr i32 i32 hstr f f f i32 i32 i32 hstr i32 i32 } ]\"\n");

    g_option_context_add_main_entries(options, entries, NULL);

    memset(opt, 0, sizeof(*opt));

    gutil_log_timestamp = FALSE;
    gutil_log_set_type(GLOG_TYPE_STDERR, pname);
    gutil_log_default.level = GLOG_LEVEL_DEFAULT;

    if (g_option_context_parse(options, &argc, &argv, &error)) {
        int i;

        /*
         * Remove the "--" argument. If any of our arguments is a negative
         * number, the user will have to add the "--" flag to stop the parser.
         * But "--" is still passed to us and we have to ignore it.
         */
	for (i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--")) {
                if (i < (argc - 1)) {
                    memmove(argv + i, argv + (i + 1),
                        sizeof(char*) * (argc - i - 1));
                }
                i--;
                argc--;

                /*
                 * There's no need to have more than one "--", but let's
                 * remove any number of those.
                 */
            }
        }
        if (argc > 2) {
            opt->argc = argc;
            opt->argv = argv;
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
    App app;
    AppOptions opt;

    memset(&app, 0, sizeof(app));
    app.ret = RET_INVARG;
    app.opt = &opt;
    if (app_init(&opt, argc, argv)) {
        app.sm = gbinder_servicemanager_new(opt.dev);
        if (app.sm) {
            app_run(&app);
            gbinder_servicemanager_unref(app.sm);
        } else {
            GERR("servicemanager seems to be missing");
        }
    }
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
