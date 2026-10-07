from pathlib import Path
r=Path.cwd();o=r/'.scratch/alias-train-20261006/pr287';s=(r/'.scratch/pr290-current-20261006/native-compute.c').read_text();v=(r/'.scratch/alias-train-20261006/pr284/native.c').read_text()
s=s.replace('#include "hw/xbox/nv2a/pgraph/vk/surface.c"\n#include "native-alias.inc"','#include "unswizzle.inc"')
a=v.index('static unsigned validation_errors;');b=v.index('uint64_t fast_hash',a);s=s.replace('uint64_t fast_hash',v[a:b]+'uint64_t fast_hash',1)
a=s.index('    VkInstanceCreateInfo instance_info');b=s.index('    uint32_t count = 1;',a);va=v.index('    const char *layer=');vb=v.index('    uint32_t count = 1;',va);s=s[:a]+v[va:vb]+s[b:]
s=s.replace('    run_native_alias_cases(pg);','    run_unswizzle(pg,32,32);run_unswizzle(pg,16,32);run_unswizzle(pg,32,16);run_unswizzle(pg,1,1);run_unswizzle(pg,2,4);')
s=s.replace('    vkDestroyInstance(instance, NULL);','    if(validation)vkDestroyDebugUtilsMessengerEXT(instance,messenger,NULL);\n    vkDestroyInstance(instance, NULL);\n    printf("RESULT validation=%s errors=%u\\n",validation?"enabled":"unavailable",validation_errors);assert(!validation_errors);')
(o/'native.c').write_text(s)
s=(r/'.scratch/alias-train-20261006/pr284/build.py').read_text().replace('pr284','pr287').replace("('image',root/'xemu-pr287/hw/xbox/nv2a/pgraph/vk/image.c'),","('map',root/'xemu-pr287/hw/xbox/nv2a/pgraph/vk/surface-alias-map.c'),").replace("'native','compute','image','swizzle'","'native','compute','map','swizzle'")
(o/'build.py').write_text(s)
