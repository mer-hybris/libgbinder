%{
#include <glib.h>
#include "binder-call.h"

struct transaction_and_reply* make_transaction_and_reply(GList* transaction, GList* reply);
struct value_info* handle_int8(App* app, int value);
struct value_info* handle_int32(App* app, int value);
struct value_info* handle_int64(App* app, long value);
struct value_info* handle_float(App* app, float value);
struct value_info* handle_double(App* app, double value);
struct value_info* handle_string8(App* app, char* value);
struct value_info* handle_string16(App* app, char* value);
struct value_info* handle_hstring(App* app, char* value);
struct value_info* handle_vector(App* app, GList* values);
struct value_info* handle_struct(App* app, GList* values);

void cmdlineerror(App* app, char const* s);

struct type_info* handle_type_int8(App* app);
struct type_info* handle_type_int32(App* app);
struct type_info* handle_type_int64(App* app);
struct type_info* handle_type_float(App* app);
struct type_info* handle_type_double(App* app);
struct type_info* handle_type_string8(App* app);
struct type_info* handle_type_string16(App* app);
struct type_info* handle_type_hstring(App* app);


struct type_info* handle_type_vector(App* app, struct type_info* t);
struct type_info* handle_type_struct(App* app, GList* l);

%}

%union {
    union {
        int int8_value;
        int int32_value;
        long int64_value;
        float float_value;
        double double_value;
        char* string8_value;
        char* string16_value;
        char* hstring_value;
    };
    struct value_info* value;
    struct type_info* type;
    GList* value_list;
    GList* type_list;
    GList* struct_type_list;
    struct transaction_and_reply* trans_and_reply;
}

%parse-param { void* args }
%lex-param { void* args }

%token INT8 INT32 INT64 FLOAT DOUBLE STRING8 STRING16 HSTRING

%token INT8_VALUE INT32_VALUE INT64_VALUE FLOAT_VALUE DOUBLE_VALUE STRING8_VALUE STRING16_VALUE HSTRING_VALUE
%type <value> values

%type <value> struct_values
%type <value> vec_values
%type <value> value_specifiers
%type <trans_and_reply> translation_unit

%type <value_list> values_list
%type <type_list> specifiers_list

%type <type> specifiers
%type <type> type_specifier
%type <type> vec_specifier
%type <type> struct_specifier
%type <struct_type_list> struct_declaration_list

%token REPLY

%start translation_unit
%%

type_specifier
    : INT8 { $$ = handle_type_int8(args); }
    | INT32 { $$ = handle_type_int32(args); }
    | INT64 { $$ = handle_type_int64(args); }
    | STRING8 { $$ = handle_type_string8(args); }
    | STRING16 { $$ = handle_type_string16(args); }
    | FLOAT { $$ = handle_type_float(args); }
    | DOUBLE { $$ = handle_type_double(args); }
    | HSTRING { $$ = handle_type_hstring(args); }
    ;

values
    : INT8_VALUE { $$ = handle_int8(args, cmdlinelval.int8_value); }
    | INT32_VALUE { $$ = handle_int32(args, cmdlinelval.int32_value); }
    | INT64_VALUE { $$ = handle_int64(args, cmdlinelval.int64_value); }
    | STRING8_VALUE { $$ = handle_string8(args, cmdlinelval.string8_value); }
    | STRING16_VALUE { $$ = handle_string16(args, cmdlinelval.string16_value); }
    | HSTRING_VALUE { $$ = handle_hstring(args, cmdlinelval.hstring_value); }
    | FLOAT_VALUE { $$ = handle_float(args, cmdlinelval.float_value); }
    | DOUBLE_VALUE { $$ = handle_double(args, cmdlinelval.double_value); }
    ;

struct
    : '{'
    ;

struct_end
    : '}'
    ;

vec
    : '['
    ;

vec_end
    : ']'
    ;

struct_specifier
    : struct struct_declaration_list struct_end { $$ = handle_type_struct(args, $2); }
    ;

vec_specifier
    : vec specifiers vec_end { $$ = handle_type_vector(args, $2); }
    ;

struct_declaration_list
    : specifiers { $$ = NULL; $$ = g_list_append($$, $1); }
    | struct_declaration_list specifiers { $$ = g_list_append($$, $2); }
    ;

specifiers
    : type_specifier
    | struct_specifier
    | vec_specifier
    ;

specifiers_list
    : specifiers { $$ = NULL; $$ = g_list_append($$, $1); }
    | specifiers_list specifiers { $$ = g_list_append($$, $2); }
    ;

struct_values
    : struct values_list struct_end { $$ = handle_struct(args, $2); }
    ;

vec_values
    : vec values_list vec_end { $$ = handle_vector(args, $2); }
    ;

value_specifiers
    : values
    | struct_values
    | vec_values
    ;

values_list
    : value_specifiers { $$ = NULL;  $$ = g_list_append($$, $1); }
    | values_list value_specifiers { $$ = g_list_append($$, $2); }
    ;

reply
    : REPLY
    ;

translation_unit
    : values_list reply specifiers_list { $$ = make_transaction_and_reply($1, $3); ast = $$; }
    | values_list { $$ = make_transaction_and_reply($1, 0); ast = $$; }
    | reply specifiers_list { $$ = make_transaction_and_reply(0, $2); ast = $$; }
    ;

%%

#include <stdio.h>
#include <glib.h>
#include <gutil_log.h>
#include <binder-call.h>

extern char yytext[];

struct value_info* handle_int8(App* app, int value)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = INT8_TYPE;
    v->value = g_new0(unsigned char, 1);
   * ((unsigned char*)v->value) = value;

    return v;
}

struct value_info* handle_int32(App* app, int value)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = INT32_TYPE;
    v->value = g_new0(int, 1);
   * ((int*)v->value) = value;

    return v;
}

struct value_info* handle_int64(App* app, long value)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = INT64_TYPE;
    v->value = g_new0(long, 1);
   * ((long*)v->value) = value;

    return v;
}

struct value_info* handle_float(App* app, float value)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = FLOAT_TYPE;
    v->value = g_new0(float, 1);
   * ((float*)v->value) = value;

    return v;
}

struct value_info* handle_double(App* app, double value)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = DOUBLE_TYPE;
    v->value = g_new0(double, 1);
   * ((double*)v->value) = value;

    return v;
}

struct value_info* handle_string8(App* app, char* value)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = STRING8_TYPE;
    v->value = value;

    return v;
}

struct value_info* handle_string16(App* app, char* value)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = STRING16_TYPE;
    v->value = value;

    return v;
}

struct value_info* handle_hstring(App* app, char* value)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = HSTRING_TYPE;
    v->value = value;

    return v;
}

struct value_info* handle_vector(App* app, GList* values)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = VECTOR_TYPE;
    v->value = values;

    return v;
}

struct value_info* handle_struct(App* app, GList* values)
{
    struct value_info* v = g_new0(struct value_info, 1);

    v->type = STRUCT_TYPE;
    v->value = values;

    return v;
}

struct type_info* handle_type_int8(App* app)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = INT8_TYPE;
    info->data = 0;

    return info;
}

struct type_info* handle_type_int32(App* app)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = INT32_TYPE;
    info->data = 0;

    return info;
}

struct type_info* handle_type_int64(App* app)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = INT64_TYPE;
    info->data = 0;

    return info;
}

struct type_info* handle_type_float(App* app)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = FLOAT_TYPE;
    info->data = 0;

    return info;
}

struct type_info* handle_type_double(App* app)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = DOUBLE_TYPE;
    info->data = 0;

    return info;
}

struct type_info* handle_type_string8(App* app)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = STRING8_TYPE;
    info->data = 0;

    return info;
}

struct type_info* handle_type_string16(App* app)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = STRING16_TYPE;
    info->data = 0;

    return info;
}

struct type_info* handle_type_hstring(App* app)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = HSTRING_TYPE;
    info->data = 0;

    return info;
}

struct type_info* handle_type_vector(App* app, struct type_info* t)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = VECTOR_TYPE;
    info->data = t;

    return info;
}

struct type_info* handle_type_struct(App* app, GList* l)
{
    struct type_info* info = g_new0(struct type_info, 1);

    info->type = STRUCT_TYPE;
    info->data = l;

    return info;
}

struct transaction_and_reply* make_transaction_and_reply(GList* transaction, GList* reply)
{
    struct transaction_and_reply* tar = g_new0(struct transaction_and_reply, 1);

    tar->tree_transaction = transaction;
    tar->tree_reply = reply;

    return tar;
}

void cmdlineerror(App* app, char const* s)
{
    fprintf(stderr, "@%d %s: %s\n", app->rargc - 1, s, app->opt->argv[app->rargc - 1]);
}

