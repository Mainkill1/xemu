#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/texture.c"
void fixture_copy_zeta_texture(PGRAPHState *pg, SurfaceBinding *surface,
                               TextureBinding *texture);
void fixture_copy_zeta_texture(PGRAPHState *pg, SurfaceBinding *surface,
                               TextureBinding *texture)
{
    copy_zeta_surface_to_texture(pg, surface, texture);
}
