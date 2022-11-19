/*
 * Copyright (C) 2021-2022 Jolla Ltd.
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

#ifndef BINDER_CALL_H__
#define BINDER_CALL_H__

#include <gbinder.h>

typedef struct app_options {
    char* dev;
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
    void* data;
};

struct value_info {
    enum TYPE_INFO type;
    void* value;
};

struct transaction_and_reply {
    GList* tree_transaction;
    GList* tree_reply;
};

int
cmdline_parse(
    App* app);

int
cmdlinelex(
    void* args);

extern struct transaction_and_reply* ast;

#endif

