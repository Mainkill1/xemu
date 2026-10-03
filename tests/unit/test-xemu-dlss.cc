/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ui/xemu-dlss.h"
#include "ui/xemu-settings-menu.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

static int failures;
static int cases;
#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
    ++failures; } } while (0)
#define CASE(name, ...) do { int before = failures; ++cases; __VA_ARGS__; \
    std::printf("%s %d - %s\n", failures == before ? "ok" : "not ok", \
                cases, name); } while (0)

static XemuDLSSLaunch resolve(XemuDLSSRequest request,
                              XemuDLSSEnvironment env = {})
{
    XemuDLSSLaunch result{};
    xemu_dlss_resolve(&request, &env, "C:/xemu/", true, &result);
    return result;
}

int main()
{
    std::puts("TAP version 13");
    XemuDLSSRequest request{XEMU_DLSS_OFF, XEMU_DLSS_ADAPTER_AUTO,
                            XEMU_DLSS_RESOLUTION_AUTO, ""};
    CASE("off needs no adapter", {
        auto result = resolve(request);
        CHECK(result.valid && result.mode == XEMU_DLSS_OFF);
        CHECK(!result.environment_overrides);
    });
    request.mode = XEMU_DLSS_NEURAL_RENDERING;
    CASE("automatic adapter has one deterministic executable-relative path", {
        auto result = resolve(request);
        CHECK(result.valid);
        CHECK(std::strcmp(result.adapter_path,
                          "C:/xemu/xemu-neural-present.dll") == 0);
    });
    request.adapter_selection = XEMU_DLSS_ADAPTER_CUSTOM;
    request.adapter_path = "C:/adapters/test.dll";
    CASE("custom absolute path is retained", {
        auto result = resolve(request);
        CHECK(result.valid);
        CHECK(std::strcmp(result.adapter_path, request.adapter_path) == 0);
    });
    CASE("relative or empty custom paths never fall back to another DLL", {
        auto bad = request;
        bad.adapter_path = "test.dll";
        CHECK(!resolve(bad).valid);
        bad.adapter_path = "";
        CHECK(!resolve(bad).valid);
        bad.adapter_path = "C:test.dll";
        CHECK(!resolve(bad).valid);
    });
    CASE("overlong paths are rejected rather than loaded as truncated paths", {
        std::string path = "C:/" + std::string(XEMU_DLSS_PATH_SIZE, 'a');
        auto bad = request;
        bad.adapter_path = path.c_str();
        auto result = resolve(bad);
        CHECK(!result.valid && result.adapter_path[0] == '\0');
    });
    CASE("unsupported hosts expose settings without enabling a DLL", {
        XemuDLSSLaunch launch{};
        xemu_dlss_resolve(&request, nullptr, "/opt/xemu/", false, &launch);
        CHECK(!launch.valid && launch.reason[0]);
        auto off = request;
        off.mode = XEMU_DLSS_OFF;
        xemu_dlss_resolve(&off, nullptr, "/opt/xemu/", false, &launch);
        CHECK(launch.valid && launch.mode == XEMU_DLSS_OFF);
    });
    CASE("invalid enum values fail closed", {
        auto bad = request;
        bad.mode = 99;
        CHECK(!resolve(bad).valid);
        bad = request;
        bad.adapter_selection = -1;
        CHECK(!resolve(bad).valid);
        bad = request;
        bad.processing_resolution = 99;
        CHECK(!resolve(bad).valid);
    });
    CASE("environment off overrides the saved request without mutating it", {
        XemuDLSSEnvironment env{"off", nullptr, nullptr, nullptr};
        auto result = resolve(request, env);
        CHECK(result.valid && result.mode == XEMU_DLSS_OFF);
        CHECK(result.environment_overrides & XEMU_DLSS_OVERRIDE_ENABLE);
        CHECK(request.mode == XEMU_DLSS_NEURAL_RENDERING);
    });
    CASE("environment enable preserves validation mode", {
        auto validation = request;
        validation.mode = XEMU_DLSS_VALIDATION;
        XemuDLSSEnvironment env{"1", nullptr, nullptr, nullptr};
        CHECK(resolve(validation, env).mode == XEMU_DLSS_VALIDATION);
        validation.mode = XEMU_DLSS_OFF;
        CHECK(resolve(validation, env).mode == XEMU_DLSS_NEURAL_RENDERING);
    });
    CASE("invalid Boolean override is not silently enabled", {
        XemuDLSSEnvironment env{"probably", nullptr, nullptr, nullptr};
        CHECK(!resolve(request, env).valid);
    });
    CASE("path and exact processing overrides are reported", {
        XemuDLSSEnvironment env{nullptr, "C:/override.dll", "800", "600"};
        auto result = resolve(request, env);
        CHECK(result.valid && result.work_width == 800 && result.work_height == 600);
        CHECK(std::strcmp(result.adapter_path, "C:/override.dll") == 0);
        CHECK(result.environment_overrides ==
              (XEMU_DLSS_OVERRIDE_PATH | XEMU_DLSS_OVERRIDE_EXTENT));
    });
    CASE("unpaired, negative, overflowing and huge dimensions are rejected", {
        for (const char *value : {"-1", "8193", "4294967296", "200x", ""}) {
            XemuDLSSEnvironment env{nullptr, nullptr, value, "600"};
            CHECK(!resolve(request, env).valid);
        }
        XemuDLSSEnvironment env{nullptr, nullptr, "800", nullptr};
        CHECK(!resolve(request, env).valid);
        env = {nullptr, nullptr, "0", "0"};
        CHECK(resolve(request, env).valid);
    });
    CASE("automatic processing retains aspect ratio and never upscales", {
        auto launch = resolve(request);
        uint32_t w = 0, h = 0;
        xemu_dlss_processing_extent(&launch, 640, 480, &w, &h);
        CHECK(w == 640 && h == 480);
        xemu_dlss_processing_extent(&launch, 1920, 1080, &w, &h);
        CHECK(w == 1280 && h == 720);
        xemu_dlss_processing_extent(&launch, 2560, 1920, &w, &h);
        CHECK(w == 960 && h == 720);
        xemu_dlss_processing_extent(&launch, 0, 480, &w, &h);
        CHECK(w == 0 && h == 0);
    });
    CASE("display resolution and explicit developer dimensions are distinct", {
        auto full = request;
        full.processing_resolution = XEMU_DLSS_RESOLUTION_DISPLAY;
        auto launch = resolve(full);
        uint32_t w = 0, h = 0;
        xemu_dlss_processing_extent(&launch, 2560, 1920, &w, &h);
        CHECK(w == 2560 && h == 1920);
        XemuDLSSEnvironment env{nullptr, nullptr, "800", "600"};
        launch = resolve(full, env);
        xemu_dlss_processing_extent(&launch, 1920, 1080, &w, &h);
        CHECK(w == 800 && h == 600);
    });
    CASE("old saved menu positions migrate exactly once", {
        for (int i = 0; i < 9; ++i) {
            int expected = i < 3 ? i : i + 1;
            CHECK(xemu_settings_menu_migrate_index(i, 0) == expected);
            CHECK(xemu_settings_menu_migrate_index(expected, 1) == expected);
        }
        CHECK(xemu_settings_menu_migrate_index(-1, 0) == 0);
        CHECK(xemu_settings_menu_migrate_index(99, 1) == 0);
        CHECK(XEMU_SETTINGS_TAB_DLSS == 3);
        CHECK(XEMU_SETTINGS_TAB_SNAPSHOTS == 6);
        CHECK(XEMU_SETTINGS_TAB_SYSTEM == 7);
        CHECK(XEMU_SETTINGS_TAB_ABOUT == 9);
    });
    CASE("validation mode rejects inference adapters before bootstrap", {
        CHECK(!xemu_dlss_adapter_allowed(XEMU_DLSS_VALIDATION,
                                         XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY));
        CHECK(xemu_dlss_adapter_allowed(XEMU_DLSS_VALIDATION,
                                        XEMU_NEURAL_PLUGIN_CAP_VALIDATION_ONLY));
        CHECK(!xemu_dlss_adapter_allowed(XEMU_DLSS_OFF, UINT64_MAX));
        CHECK(!xemu_dlss_adapter_allowed(99, UINT64_MAX));
        CHECK(xemu_dlss_adapter_allowed(XEMU_DLSS_NEURAL_RENDERING, 0));
    });
    CASE("startup inputs are owned and initialization never relatches", {
        char path[] = "C:/startup.dll";
        XemuDLSSEnvironment env{nullptr, path, nullptr, nullptr};
        CHECK(xemu_dlss_initialize(&request, &env, "C:/xemu/", true));
        path[3] = 'X';
        CHECK(!xemu_dlss_initialize(&request, nullptr, "D:/other/", true));
        XemuDLSSSnapshot snap{};
        xemu_dlss_get_snapshot(&snap);
        CHECK(std::strcmp(snap.startup.adapter_path, "C:/startup.dll") == 0);
        CHECK(!snap.restart_required);
        CHECK(snap.runtime.feature == XEMU_NEURAL_PLUGIN_FEATURE_NONE);
    });
    CASE("saved edits do not alter the startup request or environment", {
        auto changed = request;
        changed.adapter_path = "C:/new.dll";
        xemu_dlss_set_requested(&changed);
        XemuDLSSSnapshot snap{};
        xemu_dlss_get_snapshot(&snap);
        CHECK(!snap.restart_required); /* path is masked by the startup override */
        changed.processing_resolution = XEMU_DLSS_RESOLUTION_1080;
        xemu_dlss_set_requested(&changed);
        xemu_dlss_get_snapshot(&snap);
        CHECK(snap.restart_required);
        CHECK(snap.startup.processing_resolution == XEMU_DLSS_RESOLUTION_AUTO);
        CHECK(snap.next_launch.processing_resolution == XEMU_DLSS_RESOLUTION_1080);
        xemu_dlss_set_requested(&request);
        xemu_dlss_get_snapshot(&snap);
        CHECK(!snap.restart_required);
    });
    CASE("a ready DLL cannot report an active produced feature", {
        uint64_t token = xemu_dlss_begin_runtime();
        XemuDLSSRuntime runtime{};
        runtime.state = XEMU_DLSS_RUNTIME_READY;
        runtime.feature = XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR;
        xemu_dlss_publish_runtime(token, &runtime);
        XemuDLSSSnapshot snap{};
        xemu_dlss_get_snapshot(&snap);
        CHECK(snap.runtime.feature == XEMU_NEURAL_PLUGIN_FEATURE_NONE);
        runtime.state = XEMU_DLSS_RUNTIME_ACTIVE;
        xemu_dlss_publish_runtime(token, &runtime);
        xemu_dlss_get_snapshot(&snap);
        CHECK(snap.runtime.feature == XEMU_NEURAL_PLUGIN_FEATURE_NONE);
        runtime.processed_frames = runtime.nr_frames = 1;
        xemu_dlss_publish_runtime(token, &runtime);
        xemu_dlss_get_snapshot(&snap);
        CHECK(snap.runtime.feature == XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR);
        xemu_dlss_end_runtime(token);
        xemu_dlss_publish_runtime(token, &runtime);
        xemu_dlss_get_snapshot(&snap);
        CHECK(snap.runtime.feature == XEMU_NEURAL_PLUGIN_FEATURE_NONE);
    });
    CASE("concurrent snapshots are coherent and old producers are rejected", {
        uint64_t old = xemu_dlss_begin_runtime();
        uint64_t token = xemu_dlss_begin_runtime();
        XemuDLSSRuntime stale{};
        stale.state = XEMU_DLSS_RUNTIME_FAILED;
        xemu_dlss_publish_runtime(old, &stale);
        XemuDLSSSnapshot snap{};
        xemu_dlss_get_snapshot(&snap);
        CHECK(snap.runtime.state == XEMU_DLSS_RUNTIME_LOADING);
        std::atomic<bool> done{false};
        std::thread writer([&] {
            for (unsigned i = 1; i <= 3000; ++i) {
                XemuDLSSRuntime value{};
                value.state = XEMU_DLSS_RUNTIME_ACTIVE;
                value.feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
                value.processed_frames = i;
                value.bypassed_frames = i * 2;
                std::memset(value.message, 'x', sizeof(value.message));
                xemu_dlss_publish_runtime(token, &value);
            }
            done = true;
        });
        do {
            xemu_dlss_get_snapshot(&snap);
            CHECK(snap.runtime.bypassed_frames == snap.runtime.processed_frames * 2);
            CHECK(snap.runtime.message[sizeof(snap.runtime.message) - 1] == '\0');
        } while (!done);
        writer.join();
        xemu_dlss_end_runtime(token);
    });
    std::printf("1..%d\n", cases);
    return failures != 0;
}
