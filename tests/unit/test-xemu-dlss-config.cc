/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cnode.h>
#include "ui/xemu-settings-menu.h"
#define DEFINE_CONFIG_TREE
#include "xemu-config.h"

static struct config values{};
static int failures;
#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
    ++failures; } } while (0)

static void load(const char *text)
{
    config_tree.free_allocations(&values);
    config_tree.reset_to_defaults();
    config_tree.update_from_table(toml::parse(text));
    config_tree.store_to_struct(&values);
    values.general.last_viewed_menu_index = xemu_settings_menu_migrate_index(
        values.general.last_viewed_menu_index,
        values.general.settings_menu_version);
    values.general.settings_menu_version = XEMU_SETTINGS_MENU_VERSION;
}

int main()
{
    load("");
    CHECK(values.display.dlss.mode == CONFIG_DISPLAY_DLSS_MODE_OFF);
    CHECK(values.display.dlss.adapter_selection ==
          CONFIG_DISPLAY_DLSS_ADAPTER_SELECTION_AUTO);
    CHECK(values.display.dlss.processing_resolution ==
          CONFIG_DISPLAY_DLSS_PROCESSING_RESOLUTION_AUTO);
    CHECK(!values.display.dlss.adapter_path[0]);

    load("[general]\nlast_viewed_menu_index = 5\n"
         "[display.dlss]\nmode = 'validation'\nadapter_selection = 'custom'\n"
         "adapter_path = 'C:/adapter.dll'\nprocessing_resolution = '1080p'\n"
         "[display.window]\nvsync = false\n");
    CHECK(values.display.dlss.mode == CONFIG_DISPLAY_DLSS_MODE_VALIDATION);
    CHECK(values.display.dlss.adapter_selection ==
          CONFIG_DISPLAY_DLSS_ADAPTER_SELECTION_CUSTOM);
    CHECK(values.general.last_viewed_menu_index == XEMU_SETTINGS_TAB_SNAPSHOTS);
    CHECK(!values.display.window.vsync);
    config_tree.update_from_struct(&values);
    std::string saved = config_tree.generate_delta_toml();
    load(saved.c_str());
    CHECK(values.display.dlss.mode == CONFIG_DISPLAY_DLSS_MODE_VALIDATION);
    CHECK(std::strcmp(values.display.dlss.adapter_path, "C:/adapter.dll") == 0);
    CHECK(values.display.dlss.processing_resolution ==
          CONFIG_DISPLAY_DLSS_PROCESSING_RESOLUTION_1080P);
    CHECK(values.general.last_viewed_menu_index == XEMU_SETTINGS_TAB_SNAPSHOTS);
    CHECK(!values.display.window.vsync);

    load("[display.dlss]\nmode = 'neural_rendering'\n");
    CHECK(values.display.dlss.mode == CONFIG_DISPLAY_DLSS_MODE_NEURAL_RENDERING);
    load("[display.dlss]\nmode = 'off'\n");
    CHECK(values.display.dlss.mode == CONFIG_DISPLAY_DLSS_MODE_OFF);
    config_tree.free_allocations(&values);
    std::puts(failures ? "DLSS generated-config checks failed" :
                        "DLSS generated-config checks passed");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
