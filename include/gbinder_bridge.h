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

#ifndef GBINDER_BRIDGE_H
#define GBINDER_BRIDGE_H

#include "gbinder_types.h"

/* Since 1.1.5 */

G_BEGIN_DECLS

/*
 * A binder bridge object.
 *
 * For example, bridging "foobar" with interfaces ["example@1.0::IFoo",
 * "example@1.0::IBar"] would:
 *
 * 1. Watch "example@1.0::IFoo/foobar" and "example@1.0::IBar/foobar" on dest
 * 2. When those names appears, register objects with the same name on src
 * 3. Pass calls coming from src to the dest objects and replies in the
 *    opposite direction
 * 4. When dest objects disappear, remove the corresponding bridging objects
 *    from src
 *
 * and so on.
 */

GBinderBridge*
gbinder_bridge_new(
    const char* name,
    const char* const* ifaces,
    GBinderServiceManager* src,
    GBinderServiceManager* dest) /* Since 1.1.5 */
    G_GNUC_WARN_UNUSED_RESULT;

GBinderBridge*
gbinder_bridge_new2(
    const char* src_name,
    const char* dest_name,
    const char* const* ifaces,
    GBinderServiceManager* src,
    GBinderServiceManager* dest) /* Since 1.1.6 */
    G_GNUC_WARN_UNUSED_RESULT;

void
gbinder_bridge_free(
    GBinderBridge* bridge); /* Since 1.1.5 */

G_END_DECLS

#endif /* GBINDER_BRIDGE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
