/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "gbinder_config.h"
#include "gbinder_eventloop_p.h"
#include "gbinder_log.h"

#include <gutil_strv.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * The contents of the config file is queried from (at least) two places,
 * and pretty much always this happens the same stack. Which means that
 * we can avoid reading the same file twice if we delay dereferencing of
 * GKeyFile until the next idle loop.
 */

static GKeyFile* gbinder_config_keyfile = NULL;
static GBinderEventLoopCallback* gbinder_config_autorelease = NULL;

static const char gbinder_config_suffix[] = ".conf";
static const char gbinder_config_default_file[] = "/etc/gbinder.conf";
static const char gbinder_config_default_dir[] = "/etc/gbinder.d";

const char* gbinder_config_file = gbinder_config_default_file;
const char* gbinder_config_dir = gbinder_config_default_dir;

/*
 * Presets for particular API level can be chosen with ApiLevel
 * setting, e.g.
 *
 * [General]
 * ApiLevel=29
 *
 */

static const char CONF_GENERAL[] = "General";
static const char CONG_API_LEVEL[] = "ApiLevel";

typedef struct gbinder_config_preset_entry {
    const char* key;
    const char* value;
} GBinderConfigPresetEntry;

typedef struct gbinder_config_preset_group {
    const char* name;
    const GBinderConfigPresetEntry* entries;
} GBinderConfigPresetGroup;

typedef struct gbinder_config_preset {
    guint api_level;
    const GBinderConfigPresetGroup* groups;
} GBinderConfigPreset;

/* API level 28 */

static const GBinderConfigPresetEntry gbinder_config_28_servicemanager[] = {
    { "/dev/binder", "aidl2" },
    { "/dev/vndbinder", "aidl2" },
    { NULL, NULL }
};

static const GBinderConfigPresetGroup gbinder_config_28[] = {
    { GBINDER_CONFIG_GROUP_SERVICEMANAGER, gbinder_config_28_servicemanager },
    { NULL, NULL }
};

/* API level 29 */

static const GBinderConfigPresetEntry gbinder_config_29_protocol[] = {
    { "/dev/binder", "aidl2" },
    { "/dev/vndbinder", "aidl2" },
    { NULL, NULL }
};

#define gbinder_config_29_servicemanager gbinder_config_28_servicemanager

static const GBinderConfigPresetGroup gbinder_config_29[] = {
    { GBINDER_CONFIG_GROUP_PROTOCOL, gbinder_config_29_protocol },
    { GBINDER_CONFIG_GROUP_SERVICEMANAGER, gbinder_config_29_servicemanager },
    { NULL, NULL }
};

/* Presets sorted by API level in descending order */

static const GBinderConfigPreset gbinder_config_presets[] = {
    { 29, gbinder_config_29 },
    { 28, gbinder_config_28 }
};

static
char**
gbinder_config_collect_files(
    const char* path,
    const char* suffix)
{
    /*
     * Returns sorted list of regular files in the directory,
     * optionally having the specified suffix (e.g. ".conf").
     * Returns NULL if nothing appropriate has been found.
     */
    char** files = NULL;

    if (path) {
        GDir* dir = g_dir_open(path, 0, NULL);

        if (dir) {
            GPtrArray* list = g_ptr_array_new();
            const gchar* name;

            while ((name = g_dir_read_name(dir)) != NULL) {
                if (g_str_has_suffix(name, suffix)) {
                    char* fullname = g_build_filename(path, name, NULL);
                    struct stat st;

                    if (!stat(fullname, &st) && S_ISREG(st.st_mode)) {
                        g_ptr_array_add(list, fullname);
                    } else {
                        g_free(fullname);
                    }
                }
            }

            if (list->len > 0) {
                g_ptr_array_add(list, NULL);
                files = (char**) g_ptr_array_free(list, FALSE);
                gutil_strv_sort(files, TRUE);
            } else {
                g_ptr_array_free(list, TRUE);
            }

            g_dir_close(dir);
        }
    }

    return files;
}

static
GKeyFile*
gbinder_config_merge_keyfiles(
    GKeyFile* dest,
    GKeyFile* src)
{
    gsize i, ngroups;
    gchar** groups = g_key_file_get_groups(src, &ngroups);

    for (i = 0; i < ngroups; i++) {
        gsize k, nkeys;
        const char* group = groups[i];
        char** keys = g_key_file_get_keys(src, group, &nkeys, NULL);

        for (k = 0; k < nkeys; k++) {
            const char* key = keys[k];
            char* value = g_key_file_get_value(src, group, key, NULL);

            g_key_file_set_value(dest, group, key, value);
            g_free(value);
        }

        g_strfreev(keys);
    }

    g_strfreev(groups);
    return dest;
}

static
void
gbinder_config_apply_presets(
    GKeyFile* config,
    const GBinderConfigPreset* preset)
{
    const GBinderConfigPresetGroup* g;

    GDEBUG("Applying presets for API level %d", preset->api_level);
    for (g = preset->groups; g->name; g++) {
        const GBinderConfigPresetEntry* e;

        for (e = g->entries; e->key; e++) {
            if (!g_key_file_has_key(config, g->name, e->key, NULL)) {
                g_key_file_set_value(config, g->name, e->key, e->value);
            }
        }
    }
}

static
GKeyFile*
gbinder_config_load_files()
{
    GError* error = NULL;
    GKeyFile* out = NULL;
    char** files = gbinder_config_collect_files(gbinder_config_dir,
        gbinder_config_suffix);

    if (gbinder_config_file &&
        g_file_test(gbinder_config_file, G_FILE_TEST_EXISTS)) {
        out = g_key_file_new();
        if (g_key_file_load_from_file(out, gbinder_config_file,
            G_KEY_FILE_NONE, &error)) {
            GDEBUG("Loaded %s", gbinder_config_file);
        } else {
            GERR("Error loading %s: %s", gbinder_config_file, error->message);
            g_error_free(error);
            error = NULL;
            gbinder_config_file = NULL; /* Don't retry */
            g_key_file_unref(out);
            out = NULL;
        }
    }

    /* Files in the config directory overwrite /etc/gbinder.conf */
    if (files) {
        char** ptr;
        GKeyFile* override = NULL;

        for (ptr = files; *ptr; ptr++) {
            const char* file = *ptr;

            if (!override) {
                override = g_key_file_new();
            }
            if (g_key_file_load_from_file(override, file,
                G_KEY_FILE_NONE, &error)) {
                GDEBUG("Loaded %s", file);
                if (!out) {
                    out = override;
                    override = NULL;
                } else {
                    out = gbinder_config_merge_keyfiles(out, override);
                }
            } else {
                GERR("Error loading %s: %s", file, error->message);
                g_error_free(error);
                error = NULL;
            }
        }

        g_strfreev(files);
        if (override) {
            g_key_file_unref(override);
        }
    }

    if (out) {
        /* Apply presets */
        const int api_level = g_key_file_get_integer(out,
            CONF_GENERAL, CONG_API_LEVEL, NULL);

        if (api_level > 0) {
            int i;

            GDEBUG("API level %d", api_level);
            for (i = 0; i < G_N_ELEMENTS(gbinder_config_presets); i++) {
                const GBinderConfigPreset* preset = gbinder_config_presets + i;

                if (api_level >= preset->api_level) {
                    gbinder_config_apply_presets(out, preset);
                    break;
                }
            }
        }
    }

    return out;
}

static
void
gbinder_config_autorelease_cb(
    gpointer data)
{
    GASSERT(gbinder_config_keyfile == data);
    gbinder_config_keyfile = NULL;
    g_key_file_unref(data);
}

GKeyFile* /* autoreleased */
gbinder_config_get()
{
    if (!gbinder_config_keyfile &&
        (gbinder_config_file || gbinder_config_dir)) {
        gbinder_config_keyfile = gbinder_config_load_files();
        if (gbinder_config_keyfile) {
            /* See the comment at the top of the file why this is needed */
            gbinder_config_autorelease = gbinder_idle_callback_schedule_new
               (gbinder_config_autorelease_cb, gbinder_config_keyfile, NULL);
        }
    }
    return gbinder_config_keyfile;
}

/* Helper for loading config group in device = ident format */
GHashTable*
gbinder_config_load(
    const char* group,
    GBinderConfigValueMapFunc mapper)
{
    GKeyFile* k = gbinder_config_get();
    GHashTable* map = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, NULL);

    if (k) {
        gsize n;
        char** devs = g_key_file_get_keys(k, group, &n, NULL);

        if (devs) {
            gsize i;

            for (i = 0; i < n; i++) {
                char* dev = devs[i];
                char* sval = g_key_file_get_value(k, group, dev, NULL);
                gconstpointer val = mapper(sval);

                if (val) {
                    g_hash_table_replace(map, dev, (gpointer) val);
                } else {
                    GWARN("Unknown gbinder config '%s' for %s in group [%s]",
                        sval, dev, group);
                    g_free(dev);
                }
                g_free(sval);
            }

            /* Shallow delete (contents got stolen or freed) */
            g_free(devs);
        }
    }
    return map;
}

void
gbinder_config_exit()
{
    if (gbinder_config_autorelease) {
        gbinder_idle_callback_destroy(gbinder_config_autorelease);
        gbinder_config_autorelease = NULL;
    }
    if (gbinder_config_keyfile) {
        g_key_file_unref(gbinder_config_keyfile);
        gbinder_config_keyfile = NULL;
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
