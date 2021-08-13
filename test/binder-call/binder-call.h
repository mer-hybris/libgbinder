#ifndef BINDER_CALL_H__
#define BINDER_CALL_H__

#include <glib.h>
#include <gbinder.h>

typedef struct app_options {
    char* dev;
    char *iface;
    gboolean oneway;
    gboolean aidl;
    gint transaction;
    int argc;
    char** argv;
} AppOptions;

typedef struct app {
    const AppOptions* opt;
    GMainLoop* loop;
    GBinderServiceManager* sm;
    GBinderWriter writer;
    GBinderReader reader;
    int code;
    int rargc;
    int ret;
} App;

int cmdlinelex(void *args);

enum TYPE_INFO {
    INT8_TYPE = 0,
    INT32_TYPE,
    INT64_TYPE,
    FLOAT_TYPE,
    DOUBLE_TYPE,
    STRING8_TYPE,
    STRING16_TYPE,
    HSTRING_TYPE,
    STRUCT_TYPE,
    VECTOR_TYPE
};

struct type_info {
    enum TYPE_INFO type;
    void *data;
};

struct value_info {
    enum TYPE_INFO type;
    void *value;
};

struct transaction_and_reply {
    GList *tree_transaction;
    GList *tree_reply;
};

int cmdline_parse(App *app);

extern struct transaction_and_reply *ast;

#endif

