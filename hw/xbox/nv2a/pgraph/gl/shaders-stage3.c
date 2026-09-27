#include "shaders-stage3-part1.inc"
void pgraph_gl_init_shaders(PGRAPHState *pg);
void pgraph_gl_finalize_shaders(PGRAPHState *pg);
void pgraph_gl_bind_shaders(PGRAPHState *pg);
bool pgraph_gl_shader_override_program_active(PGRAPHState *pg);
uint32_t pgraph_gl_shader_override_effective_action(PGRAPHState *pg);
void pgraph_gl_shader_override_prepare_draw(
    PGRAPHState *pg, const XemuShaderOverrideDrawFacts *facts);
void pgraph_gl_shader_override_report_draw(PGRAPHState *pg,
                                           bool condition_matched);
#include "shaders-stage3-part2.inc"
#include "shaders-stage3-part3.inc"
#include "shaders-stage3-part4.inc"
