/*
 * Dynamic loader for optional neural-presentation adapters.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "hw/xbox/nv2a/pgraph/neural-present-loader.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32)
#include <windows.h>
#elif defined(XEMU_NEURAL_PLUGIN_ENABLE_POSIX_LOADER)
#include <dlfcn.h>
#endif

static void set_error(XemuNeuralPluginLibrary *library, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vsnprintf(library->error, sizeof(library->error), format, ap);
    va_end(ap);
}

static void *open_library(const char *path, char *error, size_t error_size)
{
#if defined(_WIN32) || defined(WIN32)
    int wide_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (wide_length <= 0) {
        DWORD code = GetLastError();
        snprintf(error, error_size,
                 "adapter path is not valid UTF-8 (error %lu)",
                 (unsigned long)code);
        return NULL;
    }

    wchar_t *wide_path = malloc((size_t)wide_length * sizeof(*wide_path));
    if (wide_path == NULL) {
        snprintf(error, error_size,
                 "unable to allocate the Windows adapter path");
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            wide_path, wide_length) <= 0) {
        DWORD code = GetLastError();
        free(wide_path);
        snprintf(error, error_size,
                 "adapter path conversion failed (error %lu)",
                 (unsigned long)code);
        return NULL;
    }

    HMODULE module = LoadLibraryExW(
        wide_path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                         LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    DWORD code = module == NULL ? GetLastError() : ERROR_SUCCESS;
    free(wide_path);
    if (module == NULL) {
        snprintf(error, error_size,
                 "LoadLibraryExW failed for '%s' (error %lu)",
                 path, (unsigned long)code);
    }
    return module;
#elif defined(XEMU_NEURAL_PLUGIN_ENABLE_POSIX_LOADER)
    void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (module == NULL) {
        const char *message = dlerror();
        snprintf(error, error_size, "dlopen failed for '%s': %s", path,
                 message != NULL ? message : "unknown error");
    }
    return module;
#else
    snprintf(error, error_size,
             "external neural adapters are currently enabled only on Windows");
    return NULL;
#endif
}

static void close_library(void *handle)
{
#if defined(_WIN32) || defined(WIN32)
    FreeLibrary((HMODULE)handle);
#elif defined(XEMU_NEURAL_PLUGIN_ENABLE_POSIX_LOADER)
    dlclose(handle);
#else
    (void)handle;
#endif
}

static void *load_symbol(void *handle, const char *name)
{
#if defined(_WIN32) || defined(WIN32)
    return (void *)(uintptr_t)GetProcAddress((HMODULE)handle, name);
#elif defined(XEMU_NEURAL_PLUGIN_ENABLE_POSIX_LOADER)
    return dlsym(handle, name);
#else
    (void)handle;
    (void)name;
    return NULL;
#endif
}

bool xemu_neural_plugin_library_open(XemuNeuralPluginLibrary *library,
                                     const char *path)
{
    memset(library, 0, sizeof(*library));
    if (path == NULL || path[0] == '\0') {
        set_error(library, "plug-in path is empty");
        return false;
    }

    library->handle = open_library(path, library->error,
                                   sizeof(library->error));
    if (library->handle == NULL) {
        return false;
    }

    void *symbol = load_symbol(library->handle,
                               XEMU_NEURAL_PLUGIN_ENTRYPOINT);
    if (symbol == NULL) {
        set_error(library, "missing export '%s'",
                  XEMU_NEURAL_PLUGIN_ENTRYPOINT);
        xemu_neural_plugin_library_close(library);
        return false;
    }

    if (sizeof(symbol) != sizeof(XemuNeuralPluginGetApiFn)) {
        set_error(library, "function-pointer size is unsupported");
        xemu_neural_plugin_library_close(library);
        return false;
    }

    XemuNeuralPluginGetApiFn get_api = NULL;
    memcpy(&get_api, &symbol, sizeof(get_api));
    library->api = get_api(XEMU_NEURAL_PLUGIN_ABI_VERSION);
    XemuNeuralPluginApiValidation validation =
        xemu_neural_plugin_api_validate(library->api);
    if (validation != XEMU_NEURAL_PLUGIN_API_VALID) {
        set_error(library, "invalid plug-in ABI: %s",
                  xemu_neural_plugin_api_validation_string(validation));
        xemu_neural_plugin_library_close(library);
        return false;
    }

    library->error[0] = '\0';
    return true;
}

void xemu_neural_plugin_library_close(XemuNeuralPluginLibrary *library)
{
    if (library->handle != NULL) {
        close_library(library->handle);
    }
    library->handle = NULL;
    library->api = NULL;
}
