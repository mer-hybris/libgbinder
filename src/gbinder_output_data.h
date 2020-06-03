/*
 * Copyright (C) 2018-2020 Jolla Ltd.
 * Copyright (C) 2018-2020 Slava Monich <slava.monich@jolla.com>
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

#ifndef GBINDER_OUTPUT_DATA_H
#define GBINDER_OUTPUT_DATA_H

#include "gbinder_types_p.h"

typedef struct gbinder_output_data_functions GBinderOutputDataFunctions;

struct gbinder_output_data {
    const GBinderOutputDataFunctions* f;
    const GByteArray* bytes;
};

struct gbinder_output_data_functions {
    GUtilIntArray* (*offsets)(GBinderOutputData* data);
    gsize (*buffers_size)(GBinderOutputData* data);
};

/* Inline wrappers */

GBINDER_INLINE_FUNC
GUtilIntArray*
gbinder_output_data_offsets(
    GBinderOutputData* data)
{
    return data ? data->f->offsets(data) : NULL;
}

GBINDER_INLINE_FUNC
gsize
gbinder_output_data_buffers_size(
    GBinderOutputData* data)
{
    return data ? data->f->buffers_size(data) : 0;
}

#endif /* GBINDER_OUTPUT_DATA_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
