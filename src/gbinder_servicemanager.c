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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gbinder_servicemanager_p.h"
#include "gbinder_client_p.h"
#include "gbinder_config.h"
#include "gbinder_local_object_p.h"
#include "gbinder_remote_object_p.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_driver.h"
#include "gbinder_ipc.h"
#include "gbinder_log.h"

#include <gbinder_client.h>

#include <gutil_misc.h>

#include <errno.h>

/*==========================================================================*
 *
 * Different versions of Android come with different flavors of service
 * managers. They are usually based on these two more or less independent
 * variants:
 *
 *   platform/frameworks/native/cmds/servicemanager/ServiceManager.cpp
 *   platform/system/hwservicemanager/ServiceManager.cpp
 *
 * They are talking slightly different protocols which slightly mutate
 * from version to version. If that's not complex enough, different
 * kinds of service managers can be running simultaneously, serving
 * different binder devices. Specific device => servicemanager mapping
 * can be optionally configured in /etc/gbinder.conf file. The default
 * service manager configuration looks like this:
 *
 *   [ServiceManager]
 *   Default = aidl
 *   /dev/binder = aidl
 *   /dev/hwbinder = hidl
 *
 *==========================================================================*/

#define CONF_GROUP GBINDER_CONFIG_GROUP_SERVICEMANAGER
#define CONF_DEFAULT GBINDER_CONFIG_VALUE_DEFAULT

typedef struct gbinder_servicemanager_type {
    const char* name;
    GType (*get_type)(void);
} GBinderServiceManagerType;

static const GBinderServiceManagerType gbinder_servicemanager_types[] = {
    { "aidl", gbinder_servicemanager_aidl_get_type },
    { "aidl2", gbinder_servicemanager_aidl2_get_type },
    { "hidl", gbinder_servicemanager_hidl_get_type }
};

#define SERVICEMANAGER_TYPE_AIDL (gbinder_servicemanager_types + 0)
#define SERVICEMANAGER_TYPE_HIDL (gbinder_servicemanager_types + 2)
#define SERVICEMANAGER_TYPE_DEFAULT SERVICEMANAGER_TYPE_AIDL

static GHashTable* gbinder_servicemanager_map = NULL;
static const GBinderServiceManagerType* gbinder_servicemanager_default =
    SERVICEMANAGER_TYPE_DEFAULT;

#define PRESENSE_WAIT_MS_MIN  (100)
#define PRESENSE_WAIT_MS_MAX  (1000)
#define PRESENSE_WAIT_MS_STEP (100)

typedef struct gbinder_servicemanager_watch {
    char* name;
    char* detail;
    GQuark quark;
    gboolean watched;
} GBinderServiceManagerWatch;

struct gbinder_servicemanager_priv {
    GHashTable* watch_table;
    gulong death_id;
    gboolean present;
    GBinderEventLoopTimeout* presence_check;
    guint presence_check_delay_ms;
    GBinderEventLoopCallback* autorelease_cb;
    GSList* autorelease;
};

G_DEFINE_ABSTRACT_TYPE(GBinderServiceManager, gbinder_servicemanager,
    G_TYPE_OBJECT)

#define PARENT_CLASS gbinder_servicemanager_parent_class
#define GBINDER_SERVICEMANAGER(obj) \
    G_TYPE_CHECK_INSTANCE_CAST((obj), GBINDER_TYPE_SERVICEMANAGER, \
    GBinderServiceManager)
#define GBINDER_SERVICEMANAGER_GET_CLASS(obj) \
    G_TYPE_INSTANCE_GET_CLASS((obj), GBINDER_TYPE_SERVICEMANAGER, \
    GBinderServiceManagerClass)
#define GBINDER_IS_SERVICEMANAGER_TYPE(klass) \
    G_TYPE_CHECK_CLASS_TYPE(klass, GBINDER_TYPE_SERVICEMANAGER)

enum gbinder_servicemanager_signal {
    SIGNAL_PRESENCE,
    SIGNAL_REGISTRATION,
    SIGNAL_COUNT
};

static const char SIGNAL_PRESENCE_NAME[] = "servicemanager-presence";
static const char SIGNAL_REGISTRATION_NAME[] = "servicemanager-registration";
#define DETAIL_LEN 32

static guint gbinder_servicemanager_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
GBinderServiceManagerClass*
gbinder_servicemanager_class_ref(
    GType type)
{
    if (G_LIKELY(type)) {
        GTypeClass* klass = g_type_class_ref(type);
        if (klass) {
            if (GBINDER_IS_SERVICEMANAGER_TYPE(klass)) {
                return GBINDER_SERVICEMANAGER_CLASS(klass);
            }
            g_type_class_unref(klass);
        }
    }
    return NULL;
}

static
GBinderServiceManagerWatch*
gbinder_servicemanager_watch_new(
    const char* name)
{
    GBinderServiceManagerWatch* watch = g_new0(GBinderServiceManagerWatch, 1);

    watch->name = g_strdup(name);
    watch->detail = g_compute_checksum_for_string(G_CHECKSUM_MD5, name, -1);
    watch->quark = g_quark_from_string(watch->detail);
    return watch;
}

static
void
gbinder_servicemanager_watch_free(
    gpointer data)
{
    GBinderServiceManagerWatch* watch = data;

    g_free(watch->name);
    g_free(watch->detail);
    g_free(watch);
}

typedef struct gbinder_servicemanager_list_tx_data {
    GBinderServiceManager* sm;
    GBinderServiceManagerListFunc func;
    char** result;
    void* user_data;
} GBinderServiceManagerListTxData;

static
void
gbinder_servicemanager_list_tx_exec(
    const GBinderIpcTx* tx)
{
    GBinderServiceManagerListTxData* data = tx->user_data;

    data->result = GBINDER_SERVICEMANAGER_GET_CLASS(data->sm)->list(data->sm);
}

static
void
gbinder_servicemanager_list_tx_done(
    const GBinderIpcTx* tx)
{
    GBinderServiceManagerListTxData* data = tx->user_data;

    if (!data->func(data->sm, data->result, data->user_data)) {
        g_strfreev(data->result);
    }
    data->result = NULL;
}

static
void
gbinder_servicemanager_list_tx_free(
    gpointer user_data)
{
    GBinderServiceManagerListTxData* data = user_data;

    g_strfreev(data->result);
    gbinder_servicemanager_unref(data->sm);
    g_slice_free(GBinderServiceManagerListTxData, data);
}

typedef struct gbinder_servicemanager_get_service_tx {
    GBinderServiceManager* sm;
    GBinderServiceManagerGetServiceFunc func;
    GBinderRemoteObject* obj;
    int status;
    char* name;
    void* user_data;
} GBinderServiceManagerGetServiceTxData;

static
void
gbinder_servicemanager_get_service_tx_exec(
    const GBinderIpcTx* tx)
{
    GBinderServiceManagerGetServiceTxData* data = tx->user_data;

    data->obj = GBINDER_SERVICEMANAGER_GET_CLASS(data->sm)->get_service
            (data->sm, data->name, &data->status);
}

static
void
gbinder_servicemanager_get_service_tx_done(
    const GBinderIpcTx* tx)
{
    GBinderServiceManagerGetServiceTxData* data = tx->user_data;

    data->func(data->sm, data->obj, data->status, data->user_data);
}

static
void
gbinder_servicemanager_get_service_tx_free(
    gpointer user_data)
{
    GBinderServiceManagerGetServiceTxData* data = user_data;

    gbinder_servicemanager_unref(data->sm);
    gbinder_remote_object_unref(data->obj);
    g_free(data->name);
    g_slice_free(GBinderServiceManagerGetServiceTxData, data);
}

typedef struct gbinder_servicemanager_add_service_tx {
    GBinderServiceManager* sm;
    GBinderServiceManagerAddServiceFunc func;
    GBinderLocalObject* obj;
    int status;
    char* name;
    void* user_data;
} GBinderServiceManagerAddServiceTxData;

static
void
gbinder_servicemanager_add_service_tx_exec(
    const GBinderIpcTx* tx)
{
    GBinderServiceManagerAddServiceTxData* data = tx->user_data;

    data->status = GBINDER_SERVICEMANAGER_GET_CLASS(data->sm)->add_service
            (data->sm, data->name, data->obj);
}

static
void
gbinder_servicemanager_add_service_tx_done(
    const GBinderIpcTx* tx)
{
    GBinderServiceManagerAddServiceTxData* data = tx->user_data;

    data->func(data->sm, data->status, data->user_data);
}

static
void
gbinder_servicemanager_add_service_tx_free(
    gpointer user_data)
{
    GBinderServiceManagerAddServiceTxData* data = user_data;

    gbinder_servicemanager_unref(data->sm);
    gbinder_local_object_unref(data->obj);
    g_free(data->name);
    g_slice_free(GBinderServiceManagerAddServiceTxData, data);
}

static
void
gbinder_servicemanager_reanimated(
    GBinderServiceManager* self)
{
    GBinderServiceManagerPriv* priv = self->priv;

    if (priv->presence_check) {
        gbinder_timeout_remove(priv->presence_check);
        priv->presence_check = NULL;
    }
    GINFO("Service manager %s has appeared", self->dev);
    /* Re-arm the watches */
    if (g_hash_table_size(priv->watch_table) > 0) {
        gpointer value;
        GHashTableIter it;
        GBinderServiceManagerClass* klass =
            GBINDER_SERVICEMANAGER_GET_CLASS(self);

        g_hash_table_iter_init(&it, priv->watch_table);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            GBinderServiceManagerWatch* watch = value;

            GASSERT(!watch->watched);
            watch->watched = klass->watch(self, watch->name);
            if (watch->watched) {
                GDEBUG("Watching %s", watch->name);
            } else {
                GWARN("Failed to watch %s", watch->name);
            }
        }
    }
    g_signal_emit(self, gbinder_servicemanager_signals[SIGNAL_PRESENCE], 0);
}

static
gboolean
gbinder_servicemanager_presense_check_timer(
    gpointer user_data)
{
    GBinderServiceManager* self = GBINDER_SERVICEMANAGER(user_data);
    GBinderRemoteObject* remote = self->client->remote;
    GBinderServiceManagerPriv* priv = self->priv;
    gboolean result;

    GASSERT(remote->dead);
    gbinder_servicemanager_ref(self);
    if (gbinder_remote_object_reanimate(remote)) {
        /* Done */
        priv->presence_check = NULL;
        gbinder_servicemanager_reanimated(self);
        result = G_SOURCE_REMOVE;
    } else if (priv->presence_check_delay_ms < PRESENSE_WAIT_MS_MAX) {
        priv->presence_check_delay_ms += PRESENSE_WAIT_MS_STEP;
        priv->presence_check =
            gbinder_timeout_add(priv->presence_check_delay_ms,
                gbinder_servicemanager_presense_check_timer, self);
        result = G_SOURCE_REMOVE;
    } else {
        result = G_SOURCE_CONTINUE;
    }
    gbinder_servicemanager_unref(self);
    return result;
}

static
void
gbinder_servicemanager_presence_check_start(
    GBinderServiceManager* self)
{
    GBinderServiceManagerPriv* priv = self->priv;

    GASSERT(!priv->presence_check);
    priv->presence_check_delay_ms = PRESENSE_WAIT_MS_MIN;
    priv->presence_check = gbinder_timeout_add(PRESENSE_WAIT_MS_MIN,
        gbinder_servicemanager_presense_check_timer, self);
}

static
void
gbinder_servicemanager_died(
    GBinderRemoteObject* remote,
    void* user_data)
{
    GBinderServiceManager* self = GBINDER_SERVICEMANAGER(user_data);
    GBinderServiceManagerPriv* priv = self->priv;

    GWARN("Service manager %s has died", self->dev);
    gbinder_servicemanager_presence_check_start(self);

    /* Will re-arm watches after servicemanager gets restarted */
    if (g_hash_table_size(priv->watch_table) > 0) {
        gpointer value;
        GHashTableIter it;
        GBinderServiceManagerClass* klass =
            GBINDER_SERVICEMANAGER_GET_CLASS(self);

        g_hash_table_iter_init(&it, priv->watch_table);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            GBinderServiceManagerWatch* watch = value;

            if (watch->watched) {
                GDEBUG("Unwatching %s", watch->name);
                watch->watched = FALSE;
                klass->unwatch(self, watch->name);
            }
        }
    }
    g_signal_emit(self, gbinder_servicemanager_signals[SIGNAL_PRESENCE], 0);
}

static
void
gbinder_servicemanager_sleep_ms(
    gulong ms)
{
    struct timespec wait;

    wait.tv_sec = ms/1000;                /* seconds */
    wait.tv_nsec = (ms % 1000) * 1000000; /* nanoseconds */
    while (nanosleep(&wait, &wait) == -1 && errno == EINTR &&
        (wait.tv_sec > 0 || wait.tv_nsec > 0));
}

static
void
gbinder_servicemanager_autorelease_cb(
    gpointer data)
{
    GBinderServiceManager* self = GBINDER_SERVICEMANAGER(data);
    GBinderServiceManagerPriv* priv = self->priv;
    GSList* list = priv->autorelease;

    priv->autorelease_cb = NULL;
    priv->autorelease = NULL;
    g_slist_free_full(list, g_object_unref);
}

static
void
gbinder_servicemanager_map_add_default(
    GHashTable* map,
    const char* dev,
    const GBinderServiceManagerType* type)
{
    if (!g_hash_table_contains(map, dev)) {
        g_hash_table_insert(map, g_strdup(dev), (gpointer) type);
    }
}

static
gconstpointer
gbinder_servicemanager_value_map(
    const char* name)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(gbinder_servicemanager_types); i++) {
        const GBinderServiceManagerType* t = gbinder_servicemanager_types + i;

        if (!g_strcmp0(name, t->name)) {
            return t;
        }
    }
    return NULL;
}

static
GHashTable*
gbinder_servicemanager_load_config()
{
    GHashTable* map = gbinder_config_load(CONF_GROUP,
        gbinder_servicemanager_value_map);

    /* Add default configuration if it's not overridden */
    gbinder_servicemanager_map_add_default(map,
        GBINDER_DEFAULT_BINDER, SERVICEMANAGER_TYPE_AIDL);
    gbinder_servicemanager_map_add_default(map,
        GBINDER_DEFAULT_HWBINDER, SERVICEMANAGER_TYPE_HIDL);

    return map;
}

/* Runs at exit */
void
gbinder_servicemanager_exit(
    void)
{
    if (gbinder_servicemanager_map) {
        g_hash_table_destroy(gbinder_servicemanager_map);
        gbinder_servicemanager_map = NULL;
    }
    /* Reset the default too, mostly for unit testing */
    gbinder_servicemanager_default = SERVICEMANAGER_TYPE_DEFAULT;
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

GBinderServiceManager*
gbinder_servicemanager_new_with_type(
    GType type,
    const char* dev)
{
    GBinderServiceManager* self = NULL;
    GBinderServiceManagerClass* klass = gbinder_servicemanager_class_ref(type);

    if (klass) {
        GBinderIpc* ipc;

        if (!dev) dev = klass->default_device;
        ipc = gbinder_ipc_new(dev);
        if (ipc) {
            /* Create a possibly dead remote object */
            GBinderRemoteObject* object = gbinder_ipc_get_remote_object
                (ipc, GBINDER_SERVICEMANAGER_HANDLE, TRUE);

            if (object) {
                gboolean first_ref;

                /* Lock */
                g_mutex_lock(&klass->mutex);
                if (klass->table) {
                    self = g_hash_table_lookup(klass->table, dev);
                }
                if (self) {
                    first_ref = FALSE;
                    gbinder_servicemanager_ref(self);
                } else {
                    char* key = g_strdup(dev); /* Owned by the hashtable */

                    first_ref = TRUE;
                    self = g_object_new(type, NULL);
                    self->client = gbinder_client_new(object, klass->iface);
                    self->dev = gbinder_remote_object_dev(object);
                    if (!klass->table) {
                        klass->table = g_hash_table_new_full(g_str_hash,
                            g_str_equal, g_free, NULL);
                    }
                    g_hash_table_replace(klass->table, key, self);
                }
                g_mutex_unlock(&klass->mutex);
                /* Unlock */
                if (first_ref) {
                    GBinderServiceManagerPriv* priv = self->priv;

                    priv->death_id =
                        gbinder_remote_object_add_death_handler(object,
                            gbinder_servicemanager_died, self);
                    /* Query the actual state if necessary */
                    gbinder_remote_object_reanimate(object);
                    if (object->dead) {
                        gbinder_servicemanager_presence_check_start(self);
                    }
                    GDEBUG("%s has %sservice manager", dev,
                        object->dead ? "no " : "");
                }
                gbinder_remote_object_unref(object);
            }
            gbinder_ipc_unref(ipc);
        }
        g_type_class_unref(klass);
    }
    return self;
}

void
gbinder_servicemanager_service_registered(
    GBinderServiceManager* self,
    const char* name)
{
    GBinderServiceManagerClass* klass = GBINDER_SERVICEMANAGER_GET_CLASS(self);
    GBinderServiceManagerPriv* priv = self->priv;
    GBinderServiceManagerWatch* watch = NULL;
    const char* normalized_name;
    char* tmp_name = NULL;

    switch (klass->check_name(self, name)) {
    case GBINDER_SERVICEMANAGER_NAME_OK:
        normalized_name = name;
        break;
    case GBINDER_SERVICEMANAGER_NAME_NORMALIZE:
        normalized_name = tmp_name = klass->normalize_name(self, name);
        break;
    default:
        normalized_name = NULL;
        break;
    }
    if (normalized_name) {
        watch = g_hash_table_lookup(priv->watch_table, normalized_name);
    }
    g_free(tmp_name);
    g_signal_emit(self, gbinder_servicemanager_signals[SIGNAL_REGISTRATION],
        watch ? watch->quark : 0, name);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

GBinderServiceManager*
gbinder_servicemanager_new(
    const char* dev)
{
    if (dev) {
        const GBinderServiceManagerType* type = NULL;

        if (!gbinder_servicemanager_map) {
            const GBinderServiceManagerType* t;

            /* One-time initialization */
            gbinder_servicemanager_map = gbinder_servicemanager_load_config();

            /* "Default" is a special value stored in a special variable */
            t = g_hash_table_lookup(gbinder_servicemanager_map, CONF_DEFAULT);
            if (t) {
                g_hash_table_remove(gbinder_servicemanager_map, CONF_DEFAULT);
                gbinder_servicemanager_default = t;
            } else {
                gbinder_servicemanager_default = SERVICEMANAGER_TYPE_DEFAULT;
            }
        }

        type = g_hash_table_lookup(gbinder_servicemanager_map, dev);
        if (type) {
            GDEBUG("Using %s service manager for %s", type->name, dev);
        } else {
            type = gbinder_servicemanager_default;
            GDEBUG("Using default service manager %s for %s", type->name, dev);
        }
        return gbinder_servicemanager_new_with_type(type->get_type(), dev);
    }
    return NULL;
}

GBinderLocalObject*
gbinder_servicemanager_new_local_object(
    GBinderServiceManager* self,
    const char* iface,
    GBinderLocalTransactFunc txproc,
    void* user_data)
{
    const char* ifaces[2];

    ifaces[0] = iface;
    ifaces[1] = NULL;
    return gbinder_servicemanager_new_local_object2
        (self, ifaces, txproc, user_data);
}

GBinderLocalObject*
gbinder_servicemanager_new_local_object2(
    GBinderServiceManager* self,
    const char* const* ifaces,
    GBinderLocalTransactFunc txproc,
    void* user_data)
{
    if (G_LIKELY(self)) {
        return gbinder_local_object_new(gbinder_client_ipc(self->client),
            ifaces, txproc, user_data);
    }
    return NULL;
}

GBinderServiceManager*
gbinder_servicemanager_ref(
    GBinderServiceManager* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(GBINDER_SERVICEMANAGER(self));
    }
    return self;
}

void
gbinder_servicemanager_unref(
    GBinderServiceManager* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(GBINDER_SERVICEMANAGER(self));
    }
}

gboolean
gbinder_servicemanager_is_present(
    GBinderServiceManager* self) /* Since 1.0.25 */
{
    return G_LIKELY(self) && !self->client->remote->dead;
}

gboolean
gbinder_servicemanager_wait(
    GBinderServiceManager* self,
    long max_wait_ms) /* Since 1.0.25 */
{
    if (G_LIKELY(self)) {
        GBinderRemoteObject* remote = self->client->remote;

        if (!remote->dead) {
            return TRUE;
        } else if (gbinder_remote_object_reanimate(remote)) {
            gbinder_servicemanager_reanimated(self);
            return TRUE;
        } else if (max_wait_ms != 0) {
            /* Zero timeout means a singe check and it's already done */
            long delay_ms = PRESENSE_WAIT_MS_MIN;

            while (max_wait_ms != 0) {
                if (max_wait_ms > 0) {
                    if (max_wait_ms < delay_ms) {
                        delay_ms = max_wait_ms;
                        max_wait_ms = 0;
                    } else {
                        max_wait_ms -= delay_ms;
                    }
                }
                gbinder_servicemanager_sleep_ms(delay_ms);
                if (gbinder_remote_object_reanimate(remote)) {
                    gbinder_servicemanager_reanimated(self);
                    return TRUE;
                }
                if (delay_ms < PRESENSE_WAIT_MS_MAX) {
                    delay_ms += PRESENSE_WAIT_MS_STEP;
                    if (delay_ms > PRESENSE_WAIT_MS_MAX) {
                        delay_ms = PRESENSE_WAIT_MS_MAX;
                    }
                }
            }
            /* Timeout */
            GWARN("Timeout waiting for service manager %s", self->dev);
        }
    }
    return FALSE;
}

gulong
gbinder_servicemanager_list(
    GBinderServiceManager* self,
    GBinderServiceManagerListFunc func,
    void* user_data)
{
    if (G_LIKELY(self) && func) {
        GBinderServiceManagerListTxData* data =
            g_slice_new0(GBinderServiceManagerListTxData);

        data->sm = gbinder_servicemanager_ref(self);
        data->func = func;
        data->user_data = user_data;

        return gbinder_ipc_transact_custom(gbinder_client_ipc(self->client),
            gbinder_servicemanager_list_tx_exec,
            gbinder_servicemanager_list_tx_done,
            gbinder_servicemanager_list_tx_free, data);
    }
    return 0;
}

char**
gbinder_servicemanager_list_sync(
    GBinderServiceManager* self)
{
    if (G_LIKELY(self)) {
        return GBINDER_SERVICEMANAGER_GET_CLASS(self)->list(self);
    }
    return NULL;
}

gulong
gbinder_servicemanager_get_service(
    GBinderServiceManager* self,
    const char* name,
    GBinderServiceManagerGetServiceFunc func,
    void* user_data)
{
    if (G_LIKELY(self) && func && name) {
        GBinderServiceManagerGetServiceTxData* data =
            g_slice_new0(GBinderServiceManagerGetServiceTxData);

        data->sm = gbinder_servicemanager_ref(self);
        data->func = func;
        data->name = g_strdup(name);
        data->user_data = user_data;
        data->status = (-EFAULT);

        return gbinder_ipc_transact_custom(gbinder_client_ipc(self->client),
            gbinder_servicemanager_get_service_tx_exec,
            gbinder_servicemanager_get_service_tx_done,
            gbinder_servicemanager_get_service_tx_free, data);
    }
    return 0;
}

GBinderRemoteObject* /* autoreleased */
gbinder_servicemanager_get_service_sync(
    GBinderServiceManager* self,
    const char* name,
    int* status)
{
    GBinderRemoteObject* obj = NULL;

    if (G_LIKELY(self) && name) {
        obj = GBINDER_SERVICEMANAGER_GET_CLASS(self)->get_service
            (self, name, status);
        if (obj) {
            GBinderServiceManagerPriv* priv = self->priv;

            priv->autorelease = g_slist_prepend(priv->autorelease, obj);
            if (!priv->autorelease_cb) {
                priv->autorelease_cb = gbinder_idle_callback_schedule_new
                    (gbinder_servicemanager_autorelease_cb, self, NULL);
            }
        }
    } else if (status) {
        *status = (-EINVAL);
    }
    return obj;
}

gulong
gbinder_servicemanager_add_service(
    GBinderServiceManager* self,
    const char* name,
    GBinderLocalObject* obj,
    GBinderServiceManagerAddServiceFunc func,
    void* user_data)
{
    if (G_LIKELY(self) && func && name) {
        GBinderServiceManagerAddServiceTxData* data =
            g_slice_new0(GBinderServiceManagerAddServiceTxData);

        data->sm = gbinder_servicemanager_ref(self);
        data->obj = gbinder_local_object_ref(obj);
        data->func = func;
        data->name = g_strdup(name);
        data->user_data = user_data;
        data->status = (-EFAULT);

        return gbinder_ipc_transact_custom(gbinder_client_ipc(self->client),
            gbinder_servicemanager_add_service_tx_exec,
            gbinder_servicemanager_add_service_tx_done,
            gbinder_servicemanager_add_service_tx_free, data);
    }
    return 0;
}

int
gbinder_servicemanager_add_service_sync(
    GBinderServiceManager* self,
    const char* name,
    GBinderLocalObject* obj)
{
    if (G_LIKELY(self) && name && obj) {
        return GBINDER_SERVICEMANAGER_GET_CLASS(self)->add_service
            (self, name, obj);
    } else {
        return (-EINVAL);
    }
}

void
gbinder_servicemanager_cancel(
    GBinderServiceManager* self,
    gulong id)
{
    if (G_LIKELY(self)) {
        gbinder_ipc_cancel(gbinder_client_ipc(self->client), id);
    }
}

gulong
gbinder_servicemanager_add_presence_handler(
    GBinderServiceManager* self,
    GBinderServiceManagerFunc func,
    void* user_data) /* Since 1.0.25 */
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_PRESENCE_NAME, G_CALLBACK(func), user_data) : 0;
}

gulong
gbinder_servicemanager_add_registration_handler(
    GBinderServiceManager* self,
    const char* name,
    GBinderServiceManagerRegistrationFunc func,
    void* data) /* Since 1.0.13 */
{
    gulong id = 0;

    if (G_LIKELY(self) && G_LIKELY(func)) {
        char* tmp_name = NULL;
        GBinderServiceManagerClass* klass =
            GBINDER_SERVICEMANAGER_GET_CLASS(self);

        switch (klass->check_name(self, name)) {
        case GBINDER_SERVICEMANAGER_NAME_OK:
            break;
        case GBINDER_SERVICEMANAGER_NAME_NORMALIZE:
            name = tmp_name = klass->normalize_name(self, name);
            break;
        default:
            name = NULL;
            break;
        }
        if (name) {
            GBinderServiceManagerPriv* priv = self->priv;
            GBinderServiceManagerWatch* watch = NULL;

            watch = g_hash_table_lookup(priv->watch_table, name);
            if (!watch) {
                watch = gbinder_servicemanager_watch_new(name);
                g_hash_table_insert(priv->watch_table, watch->name, watch);
            }
            if (!watch->watched && !self->client->remote->dead) {
                watch->watched = klass->watch(self, watch->name);
                if (watch->watched) {
                    GDEBUG("Watching %s", watch->name);
                } else {
                    GWARN("Failed to watch %s", watch->name);
                }
            }

            id = g_signal_connect_closure_by_id(self,
                gbinder_servicemanager_signals[SIGNAL_REGISTRATION],
                watch->quark, g_cclosure_new(G_CALLBACK(func), data, NULL),
                FALSE);
        }
        g_free(tmp_name);
    }
    return id;
}

void
gbinder_servicemanager_remove_handler(
    GBinderServiceManager* self,
    gulong id) /* Since 1.0.13 */
{
    gbinder_servicemanager_remove_handlers(self, &id, 1);
}

void
gbinder_servicemanager_remove_handlers(
    GBinderServiceManager* self,
    gulong* ids,
    guint count) /* Since 1.0.25 */
{
    if (G_LIKELY(self) && G_LIKELY(ids) && G_LIKELY(count)) {
        guint i, disconnected = 0;

        for (i = 0; i < count; i++) {
            if (ids[i]) {
                g_signal_handler_disconnect(self, ids[i]);
                disconnected++;
                ids[i] = 0;
            }
        }

        if (disconnected) {
            GBinderServiceManagerClass* klass =
                GBINDER_SERVICEMANAGER_GET_CLASS(self);
            GBinderServiceManagerPriv* priv = self->priv;
            GHashTableIter it;
            gpointer value;

            g_hash_table_iter_init(&it, priv->watch_table);
            while (disconnected && g_hash_table_iter_next(&it, NULL, &value)) {
                GBinderServiceManagerWatch* watch = value;

                if (watch->watched && !g_signal_has_handler_pending(self,
                    gbinder_servicemanager_signals[SIGNAL_REGISTRATION],
                    watch->quark, TRUE)) {
                    /* This must be one of those we have just removed */
                    GDEBUG("Unwatching %s", watch->name);
                    watch->watched = FALSE;
                    klass->unwatch(self, watch->name);
                    disconnected--;
                }
            }
        }
    }
}

/*
 * These two exist mostly for backward compatibility. Normally,
 * gbinder_servicemanager_new() should be used, to allow the type of
 * service manager to be configurable per device via /etc/gbinder.conf
 */

GBinderServiceManager*
gbinder_defaultservicemanager_new(
    const char* dev)
{
    return gbinder_servicemanager_new_with_type
        (gbinder_servicemanager_aidl_get_type(), dev);
}

GBinderServiceManager*
gbinder_hwservicemanager_new(
    const char* dev)
{
    return gbinder_servicemanager_new_with_type
        (gbinder_servicemanager_hidl_get_type(), dev);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
gbinder_servicemanager_init(
    GBinderServiceManager* self)
{
    GBinderServiceManagerPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        GBINDER_TYPE_SERVICEMANAGER, GBinderServiceManagerPriv);

    self->priv = priv;
    priv->watch_table = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, gbinder_servicemanager_watch_free);
}

static
void
gbinder_servicemanager_dispose(
    GObject* object)
{
    GBinderServiceManager* self = GBINDER_SERVICEMANAGER(object);
    GBinderServiceManagerClass* klass = GBINDER_SERVICEMANAGER_GET_CLASS(self);

    GVERBOSE_("%s", self->dev);
    /* Lock */
    g_mutex_lock(&klass->mutex);

    /*
     * The follow can happen:
     *
     * 1. Last reference goes away.
     * 2. gbinder_servicemanager_dispose() is invoked by glib
     * 3. Before gbinder_servicemanager_dispose() grabs the
     *    lock, gbinder_servicemanager_new() gets there first,
     *    finds the object in the hashtable, bumps its refcount
     *    (under the lock) and returns the reference to the caller.
     * 4. gbinder_servicemanager_dispose() gets its lock, finds
     *    that the object's refcount is greater than zero and leaves
     *    the object in the table.
     *
     * It's OK for a GObject to get re-referenced in dispose.
     * glib will recheck the refcount once dispose returns,
     * gbinder_servicemanager_finalize() will not be called
     * this time around.
     */
    if (klass->table && g_atomic_int_get(&object->ref_count) <= 1) {
        g_hash_table_remove(klass->table, self->dev);
        if (g_hash_table_size(klass->table) == 0) {
            g_hash_table_unref(klass->table);
            klass->table = NULL;
        }
    }
    g_mutex_unlock(&klass->mutex);
    /* Unlock */
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
}

static
void
gbinder_servicemanager_finalize(
    GObject* object)
{
    GBinderServiceManager* self = GBINDER_SERVICEMANAGER(object);
    GBinderServiceManagerPriv* priv = self->priv;

    gbinder_timeout_remove(priv->presence_check);
    gbinder_remote_object_remove_handler(self->client->remote, priv->death_id);
    gbinder_idle_callback_destroy(priv->autorelease_cb);
    g_slist_free_full(priv->autorelease, g_object_unref);
    g_hash_table_destroy(priv->watch_table);
    gbinder_client_unref(self->client);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
gbinder_servicemanager_class_init(
    GBinderServiceManagerClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GType type = G_OBJECT_CLASS_TYPE(klass);

    g_mutex_init(&klass->mutex);
    g_type_class_add_private(klass, sizeof(GBinderServiceManagerPriv));
    object_class->dispose = gbinder_servicemanager_dispose;
    object_class->finalize = gbinder_servicemanager_finalize;
    gbinder_servicemanager_signals[SIGNAL_PRESENCE] =
        g_signal_new(SIGNAL_PRESENCE_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    gbinder_servicemanager_signals[SIGNAL_REGISTRATION] =
        g_signal_new(SIGNAL_REGISTRATION_NAME, type,
            G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_STRING);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
