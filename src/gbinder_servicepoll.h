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

#ifndef GBINDER_SERVICEPOLL_H
#define GBINDER_SERVICEPOLL_H

#include "gbinder_types_p.h"

extern guint gbinder_servicepoll_interval_ms GBINDER_INTERNAL;

typedef
void
(*GBinderServicePollFunc)(
    GBinderServicePoll* poll,
    const char* name_added,
    void* user_data);

GBinderServicePoll*
gbinder_servicepoll_new(
    GBinderServiceManager* manager,
    GBinderServicePoll** weakptr)
    GBINDER_INTERNAL;

GBinderServicePoll*
gbinder_servicepoll_ref(
    GBinderServicePoll* poll)
    GBINDER_INTERNAL;

void
gbinder_servicepoll_unref(
    GBinderServicePoll* poll)
    GBINDER_INTERNAL;

GBinderServiceManager*
gbinder_servicepoll_manager(
    GBinderServicePoll* poll)
    GBINDER_INTERNAL;

gboolean
gbinder_servicepoll_is_known_name(
    GBinderServicePoll* poll,
    const char* name)
    GBINDER_INTERNAL;

gulong
gbinder_servicepoll_add_handler(
    GBinderServicePoll* poll,
    GBinderServicePollFunc func,
    void* user_data)
    GBINDER_INTERNAL;

void
gbinder_servicepoll_remove_handler(
    GBinderServicePoll* poll,
    gulong id)
    GBINDER_INTERNAL;

#endif /* GBINDER_SERVICEPOLL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
