// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemu/osdep.h"
#include "glib/gstdio.h"
#include <cnode.h>
extern "C" {
#include "qapi/error.h"
#include "qemu/option.h"
}
#include "ui/xemu-settings.h"
#include "ui/xemu-shortcut-config.h"

extern CNode config_tree;

static char *hash_config(struct config *configuration)
{
    CNode copy(config_tree);
    copy.update_from_struct(configuration);
    auto text = copy.generate_delta_toml();
    return g_compute_checksum_for_data(
        G_CHECKSUM_SHA256, reinterpret_cast<const guchar *>(text.data()),
        text.size());
}

char *xemu_shortcut_base_config_sha256(void)
{
    return hash_config(&g_config);
}

char *xemu_shortcut_comparison_config_sha256(const QDict *input_paths)
{
    struct config configuration = g_config;
    char bound[] = "@verified-runner-input";
    if (configuration.general.screenshot_dir &&
        *configuration.general.screenshot_dir) {
        configuration.general.screenshot_dir = "@verified-runner-output";
    }
    const char **paths[] = {
        &configuration.sys.files.bootrom_path,
        &configuration.sys.files.flashrom_path,
        &configuration.sys.files.eeprom_path,
        &configuration.sys.files.hdd_path,
        &configuration.sys.files.dvd_path,
    };
    const char *roles[] = { "bootrom", "flashrom", "eeprom", "hdd", "dvd" };
    for (size_t i = 0; input_paths && i < sizeof(paths) / sizeof(paths[0]);
         i++) {
        *paths[i] = qdict_get_str(input_paths, roles[i]);
    }
    for (const char **path : paths) {
        if (*path && **path) {
            *path = bound;
        }
    }
    return hash_config(&configuration);
}

QDict *xemu_shortcut_input_paths(int argc, char **argv, Error **errp)
{
    g_autoptr(QDict) paths = qdict_new();
    qdict_put_str(paths, "bootrom", g_config.sys.files.bootrom_path ?: "");
    qdict_put_str(paths, "flashrom", g_config.sys.files.flashrom_path ?: "");
    qdict_put_str(paths, "eeprom", g_config.sys.files.eeprom_path ?: "");
    qdict_put_str(paths, "hdd", g_config.sys.files.hdd_path ?: "");
    qdict_put_str(paths, "dvd", g_config.sys.files.dvd_path ?: "");
    if (!*qdict_get_str(paths, "eeprom")) {
        error_setg(errp,
                   "Shortcut evidence requires an explicit existing EEPROM");
        return nullptr;
    }
    bool dvd_override = false;
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) {
            continue;
        }
        const char *option = argv[i];
        if (!strcmp(option, "-S") || !strcmp(option, "-no-shutdown") ||
            !strcmp(option, "-no-reboot")) {
            continue;
        }
        if (i + 1 >= argc || !argv[i + 1]) {
            error_setg(errp, "Missing shortcut evidence launch argument: %s",
                       option);
            return nullptr;
        }
        const char *value = argv[++i];
        if (!strcmp(option, "-qmp") || !strcmp(option, "-name") ||
            !strcmp(option, "-msg") || !strcmp(option, "-loadvm")) {
            continue;
        }
        if (!strcmp(option, "-dvd_path") && !dvd_override) {
            dvd_override = true;
            qdict_put_str(paths, "dvd", value);
            continue;
        }
        if (!strcmp(option, "-drive")) {
            /* Use the production legacy parser, including doubled commas.
             * Restrict the evidence route to one ordinary HDD drive. */
            auto *list = static_cast<QemuOptsList *>(
                g_malloc0(sizeof(QemuOptsList) + sizeof(QemuOptDesc)));
            list->name = "shortcut-drive";
            QTAILQ_INIT(&list->head);
            QemuOpts *drive = qemu_opts_parse(list, value, false, errp);
            QDict *parsed =
                drive ? qemu_opts_to_qdict(drive, nullptr) : nullptr;
            if (drive) {
                qemu_opts_del(drive);
            }
            g_free(list);
            g_autoptr(QDict) owned = parsed;
            const char *file =
                parsed ? qdict_get_try_str(parsed, "file") : nullptr;
            bool supported =
                parsed && file && *file &&
                !strcmp(qdict_get_try_str(parsed, "index") ?: "", "0") &&
                !strcmp(qdict_get_try_str(parsed, "media") ?: "", "disk") &&
                (!qdict_haskey(parsed, "locked") ||
                 !strcmp(qdict_get_try_str(parsed, "locked") ?: "", "on"));
            const QDictEntry *entry;
            if (parsed) {
                for (entry = qdict_first(parsed); entry;
                     entry = qdict_next(parsed, entry)) {
                    if (strcmp(entry->key, "index") &&
                        strcmp(entry->key, "media") &&
                        strcmp(entry->key, "file") &&
                        strcmp(entry->key, "locked")) {
                        supported = false;
                    }
                }
            }
            if (supported && !*qdict_get_str(paths, "hdd")) {
                qdict_put_str(paths, "hdd", file);
                continue;
            }
            if (!drive) {
                return nullptr;
            }
        }
        error_setg(errp, "Unsupported shortcut evidence launch option: %s",
                   option);
        return nullptr;
    }
    const QDictEntry *entry;
    for (entry = qdict_first(paths); entry; entry = qdict_next(paths, entry)) {
        const char *path = qdict_get_str(paths, entry->key);
        if (!*path) {
            continue;
        }
        FILE *file = g_fopen(path, "rb");
        bool readable = file != nullptr;
        if (file) {
            fclose(file);
        }
        GStatBuf info;
        if (!readable || g_stat(path, &info) || !S_ISREG(info.st_mode) ||
            (!strcmp(entry->key, "bootrom") && info.st_size != 512) ||
            (!strcmp(entry->key, "eeprom") && info.st_size != 256)) {
            error_setg(errp, "Unresolved shortcut evidence input %s: %s",
                       entry->key, path);
            return nullptr;
        }
    }
    return static_cast<QDict *>(g_steal_pointer(&paths));
}
