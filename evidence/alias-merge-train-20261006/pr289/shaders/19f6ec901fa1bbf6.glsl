#version 450
layout(local_size_x = 16, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform PushConstants { uint width_in, width_out; };
layout(set = 0, binding = 0) buffer DepthOut { float depth_out[]; };
layout(set = 0, binding = 1) buffer StencilOut { uint stencil_out[]; };
layout(set = 0, binding = 2) buffer DepthStencilIn { uint depth_stencil_in[]; };
uint get_input_idx(uint idx_out) {
    uint scale = width_out / width_in;
    uint y = (idx_out / width_out) / scale;
    uint x = (idx_out % width_out) / scale;
    return y * width_in + x;
}
void main() {
    uint idx_out = gl_GlobalInvocationID.x;
    uint idx_in = get_input_idx(idx_out);
    depth_out[idx_out] = uintBitsToFloat(floatBitsToUint(float(depth_stencil_in[idx_in] >> 8) / 16777216.0) + 1u);
    if (idx_out % 4 == 0) {
       uint stencil_value = 0;
       for (int i = 0; i < 4; i++) {
           uint v = depth_stencil_in[get_input_idx(idx_out + i)] & 0xff;
           stencil_value |= v << (i * 8);
       }
       stencil_out[idx_out / 4] = stencil_value;
    }
}
