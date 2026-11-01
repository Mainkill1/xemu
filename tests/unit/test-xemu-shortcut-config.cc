// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemu/osdep.h"
#include "glib/gstdio.h"
#include <cnode.h>
#include "ui/xemu-settings.h"
#include "ui/xemu-shortcut-config.h"
#include "ui/xemu-tweak-overrides.h"
#define DEFINE_CONFIG_TREE
#include "xemu-config.h"

struct config g_config;

static bool child()
{
    if (g_test_subprocess()) {
        config_tree.reset_to_defaults();
        config_tree.store_to_struct(&g_config);
        return true;
    }
    g_test_trap_subprocess(nullptr, 0, static_cast<GTestSubprocessFlags>(0));
    g_test_trap_assert_passed();
    return false;
}

static char *input_file(size_t size)
{
    char *path = nullptr;
    int fd = g_file_open_tmp("xemu-shortcut-input-XXXXXX", &path, nullptr);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_autofree char *bytes = static_cast<char *>(g_malloc0(size));
    g_assert_true(g_file_set_contents(path, bytes, size, nullptr));
    return path;
}

static void test_private_copy()
{
    if (!child()) {
        return;
    }
    auto tree_before = config_tree.generate_delta_toml();
    g_autofree char *before = xemu_shortcut_base_config_sha256();
    g_assert_nonnull(before);
    g_assert_cmpuint(strlen(before), ==, 64);
    g_autofree char *again = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, again);
    g_config.general.skip_boot_anim = !g_config.general.skip_boot_anim;
    g_autofree char *changed = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, !=, changed);
    g_assert_true(config_tree.generate_delta_toml() == tree_before);
    g_assert_true(g_config.general.skip_boot_anim);
    g_config.general.skip_boot_anim = !g_config.general.skip_boot_anim;
    g_autofree char *restored = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, restored);
}

static void test_excludes_process_overlay()
{
    if (!child()) {
        return;
    }
    g_autofree char *before = xemu_shortcut_base_config_sha256();
    g_assert_nonnull(before);
    XemuTweakOverride values[2];
    g_assert_true(xemu_tweak_parse_override("pgraph_bulk_packets=disabled",
                                            &values[0], &error_abort));
    g_assert_true(xemu_tweak_parse_override("cache_shaders=disabled",
                                            &values[1], &error_abort));
    g_assert_true(xemu_tweaks_set_overrides(values, 2, &error_abort));
    xemu_tweaks_apply(true);
    g_assert_false(xemu_tweaks_cache_requested_enabled());
    g_assert_true(g_config.perf.cache_shaders);
    g_assert_true(g_config.tweaks.pgraph_bulk_packets);
    g_autofree char *after = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, after);
    config_tree.update_from_struct(&g_config);
    auto saved = config_tree.generate_delta_toml();
    g_assert_true(saved.find("cache_shaders = false") == std::string::npos);
    g_autofree char *after_save = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, after_save);
}

static void test_private_input_paths()
{
    if (!child()) {
        return;
    }
    g_autofree char *first = input_file(256);
    g_autofree char *second = input_file(256);
    g_config.sys.files.eeprom_path = first;
    g_config.general.screenshot_dir = "/results/first/screenshots";
    g_autofree char *raw = xemu_shortcut_base_config_sha256();
    g_autofree char *comparison =
        xemu_shortcut_comparison_config_sha256(nullptr);
    g_autoptr(QDict) paths =
        xemu_shortcut_input_paths(0, nullptr, &error_abort);
    g_config.sys.files.eeprom_path = second;
    g_config.general.screenshot_dir = "/results/second/screenshots";
    g_autofree char *other_raw = xemu_shortcut_base_config_sha256();
    g_autofree char *other_comparison =
        xemu_shortcut_comparison_config_sha256(nullptr);
    g_assert_cmpstr(raw, !=, other_raw);
    g_assert_cmpstr(comparison, ==, other_comparison);
    g_assert_cmpstr(qdict_get_str(paths, "eeprom"), ==, first);
    g_assert_true(g_config.sys.files.eeprom_path == second);
    g_config.general.skip_boot_anim = !g_config.general.skip_boot_anim;
    g_autofree char *changed = xemu_shortcut_comparison_config_sha256(nullptr);
    g_assert_cmpstr(comparison, !=, changed);
    g_config.general.skip_boot_anim = !g_config.general.skip_boot_anim;
    char empty[] = "";
    g_config.sys.files.eeprom_path = empty;
    g_autofree char *unbound = xemu_shortcut_comparison_config_sha256(nullptr);
    g_assert_cmpstr(comparison, !=, unbound);
    g_unlink(first);
    g_unlink(second);
}

static void test_default_eeprom_rejected()
{
    if (!child()) {
        return;
    }
    g_autoptr(Error) error = nullptr;
    g_autoptr(QDict) paths = xemu_shortcut_input_paths(0, nullptr, &error);
    g_assert_null(paths);
    g_assert_nonnull(error);
}

static void test_cli_dvd_selection()
{
    if (!child()) {
        return;
    }
    g_autofree char *saved = input_file(16);
    g_autofree char *selected = input_file(16);
    g_autofree char *eeprom = input_file(256);
    g_config.sys.files.eeprom_path = eeprom;
    g_config.sys.files.dvd_path = saved;
    char option[] = "-dvd_path";
    char *args[] = { nullptr, option, selected };
    g_autoptr(QDict) paths = xemu_shortcut_input_paths(3, args, &error_abort);
    g_assert_cmpstr(qdict_get_str(paths, "dvd"), ==, selected);
    g_assert_true(g_config.sys.files.dvd_path == saved);
    g_assert_true(args[1] == option && args[2] == selected);
    g_unlink(saved);
    g_unlink(selected);
    g_unlink(eeprom);
}

static void test_launch_routes()
{
    if (!child()) {
        return;
    }
    g_autofree char *eeprom = input_file(256);
    g_autofree char *hdd = input_file(16);
    g_config.sys.files.eeprom_path = eeprom;
    char option[] = "-drive";
    g_autofree char *value =
        g_strdup_printf("index=0,media=disk,file=%s,locked=on", hdd);
    char *args[] = { nullptr, option, value };
    g_autoptr(QDict) paths = xemu_shortcut_input_paths(3, args, &error_abort);
    g_assert_cmpstr(qdict_get_str(paths, "hdd"), ==, hdd);
    g_assert_cmpstr(g_config.sys.files.hdd_path, ==, "");
    g_autoptr(Error) error = nullptr;
    char bios[] = "-bios";
    args[1] = bios;
    g_autoptr(QDict) rejected = xemu_shortcut_input_paths(3, args, &error);
    g_assert_null(rejected);
    g_assert_nonnull(error);
    error_free(error);
    error = nullptr;
    g_config.sys.files.bootrom_path = hdd;
    rejected = xemu_shortcut_input_paths(0, nullptr, &error);
    g_assert_null(rejected);
    g_assert_nonnull(error);
    error_free(error);
    error = nullptr;
    g_config.sys.files.bootrom_path = "";
    g_config.sys.files.eeprom_path = hdd;
    rejected = xemu_shortcut_input_paths(0, nullptr, &error);
    g_assert_null(rejected);
    g_assert_nonnull(error);
    g_unlink(eeprom);
    g_unlink(hdd);
}

static void test_runner_launch_routes()
{
    if (!child()) {
        return;
    }
    g_autofree char *eeprom = input_file(256);
    g_config.sys.files.eeprom_path = eeprom;
    char memory[] = "64";
    char device[] = "lpc47m157";
    char serial[] = "file:/tmp/xemu-shortcut-serial.log";
    char *args[] = { nullptr, const_cast<char *>("-m"),
                     memory,  const_cast<char *>("-device"),
                     device,  const_cast<char *>("-serial"),
                     serial,  const_cast<char *>("-no-shutdown") };
    g_autofree char *before = xemu_shortcut_base_config_sha256();
    g_autoptr(QDict) paths =
        xemu_shortcut_input_paths(G_N_ELEMENTS(args), args, &error_abort);
    g_assert_cmpstr(qdict_get_str(paths, "eeprom"), ==, eeprom);
    g_assert_cmpint(qdict_size(paths), ==, 5);
    g_assert_cmpstr(args[2], ==, "64");
    g_assert_cmpstr(args[4], ==, "lpc47m157");
    g_assert_cmpstr(args[6], ==, "file:/tmp/xemu-shortcut-serial.log");
    g_autofree char *after = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, after);
    g_assert_cmpint(g_config.sys.mem_limit, ==, CONFIG_SYS_MEM_LIMIT_64);
    g_config.sys.mem_limit = CONFIG_SYS_MEM_LIMIT_128;
    args[2] = const_cast<char *>("128");
    g_autoptr(QDict) larger =
        xemu_shortcut_input_paths(G_N_ELEMENTS(args), args, &error_abort);
    g_assert_nonnull(larger);
    g_unlink(eeprom);
}

static void test_runner_launch_rejections()
{
    if (!child()) {
        return;
    }
    g_autofree char *eeprom = input_file(256);
    g_config.sys.files.eeprom_path = eeprom;
    const char *invalid[][4] = {
        { "-m", "128" },
        { "-m", "64M" },
        { "-m", "0" },
        { "-m", "64,slots=1" },
        { "-m", "64", "-m", "64" },
        { "-device", "lpc47m157,id=serial" },
        { "-device", "virtio-blk-pci,drive=hidden" },
        { "-device", "lpc47m157", "-device", "lpc47m157" },
        { "-serial", "file:" },
        { "-serial", "file:relative.log" },
        { "-serial", "tcp:127.0.0.1:1234" },
        { "-serial", "stdio" },
        { "-serial", "pipe:/tmp/input" },
        { "-serial", "file:/tmp/out", "-serial", "file:/tmp/out2" },
        { "-chardev", "file,id=x,path=/tmp/out,input-path=/tmp/in" },
        { "-m", nullptr },
        { "-device", nullptr },
        { "-serial", nullptr },
    };
    for (const auto &row : invalid) {
        char *args[] = { nullptr, const_cast<char *>(row[0]),
                         const_cast<char *>(row[1]), const_cast<char *>(row[2]),
                         const_cast<char *>(row[3]) };
        int count = row[3] ? 5 : row[1] ? 3 : 2;
        g_autoptr(Error) error = nullptr;
        g_autoptr(QDict) paths = xemu_shortcut_input_paths(count, args, &error);
        g_assert_null(paths);
        g_assert_nonnull(error);
    }
    g_config.sys.mem_limit = CONFIG_SYS_MEM_LIMIT_128;
    char *mismatch[] = { nullptr, const_cast<char *>("-m"),
                         const_cast<char *>("64") };
    g_autoptr(Error) error = nullptr;
    g_autoptr(QDict) paths =
        xemu_shortcut_input_paths(G_N_ELEMENTS(mismatch), mismatch, &error);
    g_assert_null(paths);
    g_assert_nonnull(error);
    g_unlink(eeprom);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/xemu/shortcut-config/private-copy", test_private_copy);
    g_test_add_func("/xemu/shortcut-config/excludes-overlay",
                    test_excludes_process_overlay);
    g_test_add_func("/xemu/shortcut-config/private-input-paths",
                    test_private_input_paths);
    g_test_add_func("/xemu/shortcut-config/default-eeprom-rejected",
                    test_default_eeprom_rejected);
    g_test_add_func("/xemu/shortcut-config/cli-dvd-selection",
                    test_cli_dvd_selection);
    g_test_add_func("/xemu/shortcut-config/launch-routes", test_launch_routes);
    g_test_add_func("/xemu/shortcut-config/runner-launch-routes",
                    test_runner_launch_routes);
    g_test_add_func("/xemu/shortcut-config/runner-launch-rejections",
                    test_runner_launch_rejections);
    return g_test_run();
}
