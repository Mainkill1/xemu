#version 450
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform PushConstants { uint width_in, width_out; };
layout(set = 0, binding = 0) buffer DepthIn { float depth_in[]; };
layout(set = 0, binding = 1) buffer StencilIn { uint stencil_in[]; };
layout(set = 0, binding = 2) buffer DepthStencilOut { uint depth_stencil_out[]; };
uint get_input_idx(uint idx_out) {
    uint scale = width_in / width_out;
    uint y = (idx_out / width_out) * scale;
    uint x = (idx_out % width_out) * scale;
    return y * width_in + x;
}
void main() {
    uint idx_out = gl_GlobalInvocationID.x;
    uint idx_in = get_input_idx(idx_out);
    uint depth_value = int(depth_in[idx_in] * float(0xffffff));
    uint stencil_value = (stencil_in[idx_in / 4] >> ((idx_in % 4) * 8)) & 0xff;
    depth_stencil_out[idx_out] = depth_value << 8 | stencil_value;
}
