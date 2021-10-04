/*
 * Copyright (C) 2021 Jolla Ltd.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <gutil_log.h>

#define BINDER_CONTROL "/dev/binderfs/binder-control"
#define BINDERFS_DEVICE_PATH "/dev/binderfs/"
#define BINDERFS_MAX_NAME 255

struct binderfs_device_legacy {
    char name[BINDERFS_MAX_NAME + 1];
    uint8_t major;
    uint8_t minor;
};
#define BINDER_CTL_ADD_LEGACY _IOWR('b', 1, struct binderfs_device_legacy)

struct binderfs_device {
    char name[BINDERFS_MAX_NAME + 1];
    uint32_t major;
    uint32_t minor;
};
#define BINDER_CTL_ADD _IOWR('b', 1, struct binderfs_device)

static const char pname[] = "binder-add";

int main(int argc, char *argv[])
{
    int remove = 0;
    GOptionEntry entries[] = {
        { "remove", 'r', 0, G_OPTION_ARG_NONE, &remove,
          "Reomve the given binder device instead of adding it.", NULL },
        { NULL }
    };
    GOptionContext* options = g_option_context_new("[options] NAME");
    GError* error = NULL;
    char *binder_device_name;
    int ret;
    gboolean ok = FALSE;

    g_option_context_add_main_entries(options, entries, NULL);

    gutil_log_timestamp = FALSE;
    gutil_log_set_type(GLOG_TYPE_STDERR, pname);
    gutil_log_default.level = GLOG_LEVEL_DEFAULT;

    if (g_option_context_parse(options, &argc, &argv, &error)) {
        char* help;

        if (argc == 2) {
            binder_device_name = argv[1];
            ok = TRUE;
        } else {
            help = g_option_context_get_help(options, TRUE, NULL);
            fprintf(stderr, "%s", help);
            g_free(help);
        }
    } else {
        GERR("%s", error->message);
        g_error_free(error);
    }
    g_option_context_free(options);

    if (ok) {
        if (!remove) {
            struct binderfs_device device = { 0 };
            struct binderfs_device legacy_device = { 0 };
            int fd;
            memcpy(device.name, binder_device_name, strlen(binder_device_name));
            memcpy(legacy_device.name, binder_device_name, strlen(binder_device_name));

            fd = open(BINDER_CONTROL, O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                GERR("Failed to open " BINDER_CONTROL);
                return -1;
            }

            ret = ioctl(fd, BINDER_CTL_ADD, &device);
            if (ret < 0) {
                ret = ioctl(fd, BINDER_CTL_ADD_LEGACY, &legacy_device);
            }
            if (ret < 0) {
                GERR("Failed to add binder device %s: %s", binder_device_name, strerror(errno));
                close(fd);
                return -1;
            }

            GINFO("Added binder device: %s", binder_device_name);

            close(fd);
        } else {
            char binder_device_path[PATH_MAX];
            snprintf(binder_device_path, PATH_MAX, BINDERFS_DEVICE_PATH "%s", binder_device_name);
            ret = unlink(binder_device_path);
            if (ret < 0) {
                GERR("Failed to remove binder device: %s: %s", binder_device_name, strerror(errno));
                return -1;
            }
            GINFO("Success");
        }
    }
    return 0;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
