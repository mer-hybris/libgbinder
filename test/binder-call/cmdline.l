D              [0-9]
l              "l"
L              "L"
u              "u"
U              "U"
h              "h"
H              "H"
COLON          ":"
EIGHT          "8"

INT8_SUFFIX    {COLON}{EIGHT}
INT64_SUFFIX   [lL]
UTF16_SUFFIX   [uU]
HSTRING_SUFFIX [hH]

%{
#include <glib.h>
#include "cmdline.tab.h"
#include "binder-call.h"

#define YY_SKIP_YYWRAP
int cmdlinewrap(App* app);
#undef yywrap
#define yywrap() cmdlinewrap( (App*) args )
#define YY_DECL int cmdlinelex( void* args )

char* handle_str(char* text) {
    // extract str from "str"X
    char* str = g_strndup(text + 1, strlen(text) - 3);
    return str;
}

char* handle_str8(char* text) {
    // extract str from "str"
    char* str = g_strndup(text + 1, strlen(text) - 2);
    return str;
}

%}

%option never-interactive noinput nounput

%%
"i8"                                { return(INT8); }
"i32"                               { return(INT32); }
"i64"                               { return(INT64); }
"s8"                                { return(STRING8); }
"s16"                               { return(STRING16); }
"float"                             { return(FLOAT); }
"double"                            { return(DOUBLE); }
"f"                                 { return(FLOAT); }
"d"                                 { return(DOUBLE); }
"hstr"                              { return(HSTRING); }
"{"                                 { return('{'); }
"}"                                 { return('}'); }
"["                                 { return('['); }
"]"                                 { return(']'); }
-?{D}+{INT8_SUFFIX}                 { cmdlinelval.int8_value = atoi(yytext); return(INT8_VALUE); }
-?{D}+{INT64_SUFFIX}                { cmdlinelval.int64_value = atol(yytext); return(INT64_VALUE); }
-?{D}+                              { cmdlinelval.int32_value = atoi(yytext); return(INT32_VALUE); }
-?{D}+"."{D}*{INT64_SUFFIX}         { cmdlinelval.double_value = atof(yytext); return(DOUBLE_VALUE); }
-?{D}+"."{D}*                       { cmdlinelval.float_value = atof(yytext); return(FLOAT_VALUE); }
"reply"                             { return(REPLY); }
\".*\"{HSTRING_SUFFIX}              { cmdlinelval.hstring_value = handle_str(yytext); return(HSTRING_VALUE); }
\".*\"{UTF16_SUFFIX}                { cmdlinelval.string16_value = handle_str(yytext); return(STRING16_VALUE); }
\".*\"                              { cmdlinelval.string8_value = handle_str8(yytext); return(STRING8_VALUE); }
" "                                 { /* eat */ }
.                                   { fprintf(stderr, "Unrecognized character: '%c'\n", yytext[0]); }

%%

#include "binder-call.h"

int cmdlinewrap(App* app)
{
    if (YY_CURRENT_BUFFER) {
        yy_delete_buffer( YY_CURRENT_BUFFER );
    }

    if (app->rargc == app->opt->argc) {
        return 1;
    }

    yy_scan_string(app->opt->argv[app->rargc++]);

    return 0;
}

int cmdline_parse(App* app) {
    if (app->opt->argc > app->rargc) {
        cmdlinewrap(app);
    } else {
        return 1;
    }

    return cmdlineparse (app);
}

