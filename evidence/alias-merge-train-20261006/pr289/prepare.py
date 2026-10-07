from pathlib import Path
r=Path.cwd();o=r/'.scratch/alias-train-20261006/pr289'
s=(r/'.scratch/alias-train-20261006/pr287/native.c').read_text().replace('#include "unswizzle.inc"','#include "native-alias.inc"')
s=s.replace('    run_unswizzle(pg,32,32);run_unswizzle(pg,16,32);run_unswizzle(pg,32,16);run_unswizzle(pg,1,1);run_unswizzle(pg,2,4);','    run_native_alias_cases(pg);')
# The 32 pack checks already qualified287; this fixture exercises289 images.
a=s.index('    unsigned checked = 0;');b=s.index('    r->device_props = actual;',a);s=s[:a]+s[b:];s=s.replace('    printf("PASS %u actual adapter dispatches\\n", checked);','')
a=s.index('static void run_case(');b=s.index('#include \"native-alias.inc\"',a);s=s[:a]+s[b:]
(o/'native.c').write_text(s)
s=(r/'.scratch/pr290-current-20261006/prefix/native-alias.inc').read_text().replace('        producer.depth_write_generation = generation;\n','').replace('update_depth_alias_view(pg, &producer, &view)','pgraph_vk_convert_depth_alias(pg, &producer, &view)').replace('descriptors + (reuse ? 0 : 3)','descriptors + 3')
s=s.replace('reuse', 'repeat')
(o/'native-alias.inc').write_text(s)
s=(r/'.scratch/alias-train-20261006/pr287/build.py').read_text().replace('pr287','pr289').replace("('swizzle',root/", "('image',root/'xemu-pr289/hw/xbox/nv2a/pgraph/vk/image.c'),('convert',root/'xemu-pr289/hw/xbox/nv2a/pgraph/vk/surface-alias-convert.c'),('swizzle',root/").replace("'native','compute','map','swizzle'","'native','compute','map','image','convert','swizzle'")
(o/'build.py').write_text(s)
s=(r/'.scratch/alias-train-20261006/pr288/check.py').read_text().replace('pr288','pr289');(o/'check.py').write_text(s)
