from pathlib import Path
r=Path.cwd();o=r/'.scratch/alias-train-20261006/pr290/native-ownership';s=(o/'native.c').read_text();s=s.replace('#include <shaderc/shaderc.h>','#ifndef _WIN32\n#include <shaderc/shaderc.h>\n#endif')
a=s.index('    shaderc_compiler_t compiler =');b=s.index('    shader_compiles++;',a)
s=s[:a]+'''    const char *dir = getenv("XEMU_NATIVE_SHADER_DIR");
    assert(dir);
    g_mkdir_with_parents(dir, 0700);
    uint64_t hash = fast_hash((const uint8_t *)glsl, strlen(glsl));
    g_autofree char *source_path = g_strdup_printf("%s/%016" PRIx64 ".glsl", dir, hash);
    g_autofree char *binary_path = g_strdup_printf("%s/%016" PRIx64 ".spv", dir, hash);
    char *binary = NULL;
    gsize bytes = 0;
#ifndef _WIN32
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler, glsl, strlen(glsl), shaderc_compute_shader,
        "production-surface-compute.glsl", "main", NULL);
    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
        fprintf(stderr, "%s", shaderc_result_get_error_message(result)); abort();
    }
    bytes = shaderc_result_get_length(result);
    binary = g_memdup2(shaderc_result_get_bytes(result), bytes);
    assert(g_file_set_contents(source_path, glsl, -1, NULL));
    assert(g_file_set_contents(binary_path, binary, bytes, NULL));
    shaderc_result_release(result); shaderc_compiler_release(compiler);
#else
    char *saved_source = NULL;
    assert(g_file_get_contents(source_path, &saved_source, NULL, NULL));
    assert(!strcmp(saved_source, glsl)); g_free(saved_source);
    assert(g_file_get_contents(binary_path, &binary, &bytes, NULL));
#endif
    ShaderModuleInfo *module = g_new0(ShaderModuleInfo, 1);
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes, .pCode = (const uint32_t *)binary,
    };
    VK_CHECK(vkCreateShaderModule(r->device, &info, NULL, &module->module));
    g_free(binary);
'''+s[b:]
a=s.index('    uint32_t count = 1;');b=s.index('    PGRAPHState *pg',a)
s=s[:a]+'''    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL)); assert(count);
    VkPhysicalDevice *devices = g_new(VkPhysicalDevice, count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    physical = devices[0];
    const char *preferred = getenv("XEMU_NATIVE_DEVICE");
    if (preferred) {
        physical = VK_NULL_HANDLE;
        for (unsigned i = 0; i < count; i++) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(devices[i], &properties);
            if (strstr(properties.deviceName, preferred)) { physical = devices[i]; break; }
        }
        assert(physical);
    }
    g_free(devices);
    NV2AState *d = g_new0(NV2AState, 1);
'''+s[b:]
s=s.replace('static unsigned validation_errors;', 'static unsigned validation_errors;\nstatic unsigned loader_manifest_errors;')
s=s.replace('        validation_errors++;', '\n'.join([
 '        if ((type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) &&',
 '            strstr(data->pMessage, "loader_get_json: Failed to open JSON file") &&',
 '            strstr(data->pMessage, "EOSOverlayVkLayer-Win64.json")) {',
 '            loader_manifest_errors++;',
 '            fprintf(stderr, "HOST LOADER ERROR: %s\\n", data->pMessage);',
 '            return VK_FALSE;',
 '        }',
 '        validation_errors++;']))
s=s.replace('validation_errors);assert(!validation_errors);','validation_errors);printf("HOST loader_missing_EOS_manifest_errors=%u\\n",loader_manifest_errors);assert(!validation_errors);')
(o/'native-portable.c').write_text(s)
s=(o/'build.py').read_text().replace("out/'native.c'","out/'native-portable.c'").replace("out/'commands.json'","out/'native-portable-commands.json'")
(o/'build-portable.py').write_text(s)
