/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "ui/xemu-input.h"
#include "hw/xbox/xid.h"

static void test_host_detach_keeps_guest_topology(void)
{
    ControllerState physical = { .bound = -1 };
    ControllerState replacement = { .bound = -1 };
    int hub, gamepad, xmu;
    XemuVirtualControllerPort port = {
        .connected = true,
        .hub = &hub,
        .gamepad = &gamepad,
        .peripheral_types = { PERIPHERAL_XMU, PERIPHERAL_NONE },
        .peripherals = { &xmu, NULL },
    };

    g_assert_null(xemu_input_port_assign_provider(&port, &physical, 2));
    g_assert_cmpint(physical.bound, ==, 2);

    g_assert_true(xemu_input_port_assign_provider(&port, NULL, 2) == &physical);
    g_assert_cmpint(physical.bound, ==, -1);
    g_assert_true(port.connected);
    g_assert_true(port.hub == &hub);
    g_assert_true(port.gamepad == &gamepad);
    g_assert_true(port.peripherals[0] == &xmu);
    g_assert_cmpint(port.peripheral_types[0], ==, PERIPHERAL_XMU);

    g_assert_null(xemu_input_port_assign_provider(&port, &replacement, 2));
    g_assert_true(port.provider == &replacement);
    g_assert_cmpint(replacement.bound, ==, 2);
    g_assert_true(port.hub == &hub);
    g_assert_true(port.peripherals[0] == &xmu);
}

static void test_reassignment_does_not_touch_guest_devices(void)
{
    ControllerState first = { .bound = -1 };
    ControllerState second = { .bound = -1 };
    int hub;
    XemuVirtualControllerPort port = { .connected = true, .hub = &hub };

    xemu_input_port_assign_provider(&port, &first, 0);
    g_assert_true(xemu_input_port_assign_provider(&port, &second, 0) == &first);
    g_assert_cmpint(first.bound, ==, -1);
    g_assert_cmpint(second.bound, ==, 0);
    g_assert_true(port.connected);
    g_assert_true(port.hub == &hub);
}

static void test_legacy_presence_migration(void)
{
    g_assert_true(xemu_input_port_should_exist(-1, "keyboard"));
    g_assert_false(xemu_input_port_should_exist(-1, ""));
    g_assert_false(xemu_input_port_should_exist(-1, NULL));
    g_assert_true(xemu_input_port_should_exist(1, NULL));
    g_assert_false(xemu_input_port_should_exist(0, "keyboard"));
}

static void test_providerless_xid_report_is_neutral(void)
{
    XIDGamepadReport report;
    memset(&report, 0xff, sizeof(report));

    xid_gamepad_report_neutral(&report);

    g_assert_cmpuint(sizeof(report), ==, 20);
    g_assert_cmpuint(report.bReportId, ==, 0);
    g_assert_cmpuint(report.bLength, ==, 20);
    g_assert_cmpuint(report.wButtons, ==, 0);
    for (size_t i = 0; i < sizeof(report.bAnalogButtons); i++) {
        g_assert_cmpuint(report.bAnalogButtons[i], ==, 0);
    }
    g_assert_cmpint(report.sThumbLX, ==, 0);
    g_assert_cmpint(report.sThumbLY, ==, 0);
    g_assert_cmpint(report.sThumbRX, ==, 0);
    g_assert_cmpint(report.sThumbRY, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/input/host-detach-keeps-guest-topology",
                    test_host_detach_keeps_guest_topology);
    g_test_add_func("/xemu/input/provider-reassignment",
                    test_reassignment_does_not_touch_guest_devices);
    g_test_add_func("/xemu/input/legacy-presence-migration",
                    test_legacy_presence_migration);
    g_test_add_func("/xemu/input/providerless-xid-report",
                    test_providerless_xid_report_is_neutral);
    return g_test_run();
}
