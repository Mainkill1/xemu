// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemu/osdep.h"
#include <cnode.h>
#include "ui/xemu-settings.h"
#include "ui/xemu-shortcut-config.h"

extern CNode config_tree;

char *xemu_shortcut_base_config_sha256(void)
{
    CNode copy(config_tree);
    copy.update_from_struct(&g_config);
    auto text = copy.generate_delta_toml();
    return g_compute_checksum_for_data(
        G_CHECKSUM_SHA256, reinterpret_cast<const guchar *>(text.data()),
        text.size());
}
